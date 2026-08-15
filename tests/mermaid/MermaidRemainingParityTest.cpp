#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QXmlStreamReader>

#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

bool close(qreal actual, qreal expected, qreal tolerance = 0.02) {
  return std::abs(actual - expected) <= tolerance;
}

bool jsonContains(const QJsonValue& actual, const QJsonValue& expected) {
  if (expected.isObject()) {
    if (!actual.isObject()) return false;
    const QJsonObject actualObject = actual.toObject();
    const QJsonObject expectedObject = expected.toObject();
    for (auto it = expectedObject.constBegin(); it != expectedObject.constEnd(); ++it)
      if (!actualObject.contains(it.key()) ||
          !jsonContains(actualObject.value(it.key()), it.value()))
        return false;
    return true;
  }
  if (expected.isArray()) {
    if (!actual.isArray()) return false;
    const QJsonArray actualArray = actual.toArray();
    const QJsonArray expectedArray = expected.toArray();
    if (actualArray.size() != expectedArray.size()) return false;
    for (qsizetype i = 0; i < expectedArray.size(); ++i)
      if (!jsonContains(actualArray.at(i), expectedArray.at(i))) return false;
    return true;
  }
  return actual == expected;
}

const QJsonObject& fixtureCase(const QHash<QString, QJsonObject>& cases,
                               const QString& id) {
  const auto found = cases.constFind(id);
  require(found != cases.cend(), QStringLiteral("Missing fixture case %1").arg(id));
  return found.value();
}

void requireSize(const QSizeF& actual, const QJsonObject& expected,
                 const QString& id, qreal tolerance = 0.001) {
  require(close(actual.width(), expected.value(QStringLiteral("width")).toDouble(),
                tolerance) &&
              close(actual.height(), expected.value(QStringLiteral("height")).toDouble(),
                    tolerance),
          QStringLiteral("%1 size differs: native=%2x%3 upstream=%4x%5")
              .arg(id).arg(actual.width(), 0, 'g', 17)
              .arg(actual.height(), 0, 'g', 17)
              .arg(expected.value(QStringLiteral("width")).toDouble(), 0, 'g', 17)
              .arg(expected.value(QStringLiteral("height")).toDouble(), 0, 'g', 17));
}

QJsonObject viewBoxSize(const QJsonObject& test) {
  const QStringList values = test.value(QStringLiteral("viewBox"))
                                 .toString()
                                 .split(QLatin1Char(' '), Qt::SkipEmptyParts);
  require(values.size() == 4, QStringLiteral("Invalid fixture viewBox"));
  return {{QStringLiteral("width"), values.at(2).toDouble()},
          {QStringLiteral("height"), values.at(3).toDouble()}};
}

QJsonObject loadFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open %1").arg(path));
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid remaining-parity fixture: %1")
              .arg(error.errorString()));
  return document.object();
}

QJsonArray svgMarkerReferences(const QByteArray& svg) {
  QJsonArray result;
  QXmlStreamReader reader(svg);
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement()) continue;
    const auto attributes = reader.attributes();
    if (!attributes.hasAttribute(QStringLiteral("marker-start")) &&
        !attributes.hasAttribute(QStringLiteral("marker-end"))) continue;
    QJsonObject marker;
    marker[QStringLiteral("start")] =
        attributes.value(QStringLiteral("marker-start")).toString().isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(attributes.value(QStringLiteral("marker-start")).toString());
    marker[QStringLiteral("end")] =
        attributes.value(QStringLiteral("marker-end")).toString().isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(attributes.value(QStringLiteral("marker-end")).toString());
    result.append(marker);
  }
  require(!reader.hasError(), QStringLiteral("Native marker SVG is malformed"));
  return result;
}

QHash<QString, QJsonObject> svgMarkerDefinitions(const QByteArray& svg) {
  QHash<QString, QJsonObject> result;
  QXmlStreamReader reader(svg);
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement() || reader.name() != QLatin1String("marker"))
      continue;
    const auto attributes = reader.attributes();
    QJsonObject marker;
    for (const QString& name : {
             QStringLiteral("viewBox"), QStringLiteral("refX"),
             QStringLiteral("refY"), QStringLiteral("markerWidth"),
             QStringLiteral("markerHeight"), QStringLiteral("markerUnits"),
             QStringLiteral("orient")}) {
      const QString value = attributes.value(name).toString();
      marker[name] = value.isEmpty() ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(value);
    }
    QJsonArray children;
    while (reader.readNextStartElement()) {
      const auto childObject = [&]() {
        QJsonObject child;
        child[QStringLiteral("tag")] = reader.name().toString();
        QJsonObject attributesObject;
        for (const auto& attribute : reader.attributes())
          attributesObject[attribute.qualifiedName().toString()] =
              attribute.value().toString();
        child[QStringLiteral("attributes")] = attributesObject;
        QJsonArray nested;
        while (reader.readNextStartElement()) {
          QJsonObject nestedChild;
          nestedChild[QStringLiteral("tag")] = reader.name().toString();
          QJsonObject nestedAttributes;
          for (const auto& attribute : reader.attributes())
            nestedAttributes[attribute.qualifiedName().toString()] =
                attribute.value().toString();
          nestedChild[QStringLiteral("attributes")] = nestedAttributes;
          nested.append(nestedChild);
          reader.skipCurrentElement();
        }
        child[QStringLiteral("children")] = nested;
        return child;
      }();
      children.append(childObject);
    }
    marker[QStringLiteral("children")] = children;
    result.insert(attributes.value(QStringLiteral("id")).toString(), marker);
  }
  require(!reader.hasError(), QStringLiteral("Native marker definitions are malformed"));
  return result;
}

editor::MermaidRenderEntry render(const QString& source) {
  editor::MermaidRenderCache cache;
  return cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
}

const flowscene::FlowScene& flowScene(const editor::MermaidRenderEntry& entry,
                                      const QString& id) {
  const auto* scene = dynamic_cast<const flowscene::FlowScene*>(entry.scene.get());
  require(scene != nullptr, QStringLiteral("%1 did not produce FlowScene").arg(id));
  return *scene;
}

const flowscene::FlowSceneNode& node(const flowscene::FlowScene& scene,
                                    const QString& id) {
  for (const auto& item : scene.nodes)
    if (item.id == id) return item;
  fail(QStringLiteral("Missing Flow node %1").arg(id));
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected remaining-parity fixture path"));
  const QJsonObject fixture = loadFixture(QString::fromLocal8Bit(argv[1]));
  const QJsonObject upstream = fixture.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("moduleSha256")).toString() ==
              QLatin1String("fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b") &&
              fixture.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("f333623a2862bec57c82c7b40fb0bb4c6fbd45a7956f08f0a63e2d821fc633c0"),
          QStringLiteral("Remaining-parity upstream provenance drifted"));

  QHash<QString, editor::MermaidRenderEntry> entries;
  QHash<QString, QJsonObject> casesById;
  const QJsonArray cases = fixture.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 57, QStringLiteral("Expected 57 remaining parity cases"));
  for (const QJsonValue& value : cases) {
    const QJsonObject test = value.toObject();
    const QString id = test.value(QStringLiteral("id")).toString();
    const QString expectedStatus = test.value(QStringLiteral("status")).toString();
    casesById.insert(id, test);
    editor::MermaidRenderEntry entry = render(
        test.value(QStringLiteral("source")).toString());
    const bool expectedReady = expectedStatus == QLatin1String("ready");
    require((entry.status == editor::MermaidRenderStatus::Ready) == expectedReady,
            QStringLiteral("%1 status differs: native=%2 upstream=%3 (%4)")
                .arg(id).arg(static_cast<int>(entry.status)).arg(expectedStatus,
                                                               entry.errorMessage));
    if (!expectedReady) {
      require(entry.diagnostic.message ==
                  test.value(QStringLiteral("message")).toString(),
              QStringLiteral("%1 error differs: %2")
                  .arg(id, entry.diagnostic.message));
    }
    entries.insert(id, std::move(entry));
  }

  // theme-css: beyond status, the themeCSS overlay must reach the built
  // FlowScene node paint (fill red + 9px stroke via !important).
  const auto& themeCss = flowScene(entries.value(QStringLiteral("theme-css")),
                                   QStringLiteral("theme-css"));
  require(themeCss.nodes.size() == 2 &&
              std::all_of(themeCss.nodes.cbegin(), themeCss.nodes.cend(),
                          [](const flowscene::FlowSceneNode& item) {
                            const QColor fill = color::resolveSvgPaint(
                                item.fill, color::SvgPaintKind::Fill,
                                QColor(Qt::black)).color;
                            return fill == QColor(QLatin1String("#ff0000")) &&
                                   item.strokeWidth ==
                                       QLatin1String("9px");
                          }),
          QStringLiteral("theme-css overlay did not reach FlowScene paint"));

  // Cases that previously had status-only coverage get structural
  // client-size + scene-shape assertions here.
  const auto requireClientSize = [&](const QString& id) {
    const QJsonObject expected = fixtureCase(casesById, id);
    const QJsonObject client = expected.value(QStringLiteral("client")).toObject();
    const QSize native = entries.value(id).naturalSize;
    require(native == QSize(std::lround(client.value(QStringLiteral("width")).toDouble()),
                            std::lround(client.value(QStringLiteral("height")).toDouble())),
            QStringLiteral("%1 naturalSize drifted: %2x%3 vs client %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(client.value(QStringLiteral("width")).toDouble())
                .arg(client.value(QStringLiteral("height")).toDouble()));
  };
  const auto requireFlowShape = [&](const QString& id, int nodes, int edges,
                                    bool compareSize = true) {
    const auto& scene = flowScene(entries.value(id), id);
    require(scene.nodes.size() == nodes && scene.edges.size() == edges,
            QStringLiteral("%1 structure drifted: nodes=%2 edges=%3 (want %4/%5)")
                .arg(id).arg(scene.nodes.size()).arg(scene.edges.size())
                .arg(nodes).arg(edges));
    // The bare-frontmatter cases pin no fontFamily, so both stacks fall back
    // (Chrome -> Helvetica-metrics, Qt -> Noto) and the absolute client width
    // carries ~3px of fallback-metric noise rather than config signal.
    if (compareSize) requireClientSize(id);
  };
  // maxEdges is an upstream warning, not an enforcement: both edges still
  // render through the %%init%% and frontmatter entry points.
  requireFlowShape(QStringLiteral("max-edges-init"), 3, 2);
  requireFlowShape(QStringLiteral("max-edges-frontmatter"), 3, 2, false);
  requireFlowShape(QStringLiteral("max-text-init"), 2, 1);
  requireFlowShape(QStringLiteral("max-text-frontmatter"), 2, 1, false);
  // Unknown defaultRenderer falls back to the dagre-wrapper path (ready).
  requireFlowShape(QStringLiteral("flow-renderer-unknown"), 2, 1);
  requireFlowShape(QStringLiteral("flow-inherit-dir-false"), 3, 2);
  requireFlowShape(QStringLiteral("flow-inherit-dir-true"), 3, 2);

  const auto& html = flowScene(entries.value(QStringLiteral("flow-html-global-true")),
                               QStringLiteral("flow-html-global-true"));
  const auto& svg = flowScene(entries.value(QStringLiteral("flow-html-global-false")),
                              QStringLiteral("flow-html-global-false"));
  const auto& mixed = flowScene(entries.value(QStringLiteral("flow-html-alias-false")),
                                QStringLiteral("flow-html-alias-false"));
  require(html.nodes.size() == 3 && html.clusters.size() == 1 &&
              std::all_of(html.nodes.cbegin(), html.nodes.cend(),
                          [](const auto& item) { return item.label.htmlLabels; }) &&
              html.clusters.first().label.htmlLabels,
          QStringLiteral("Global htmlLabels:true branch drifted"));
  require(std::none_of(svg.nodes.cbegin(), svg.nodes.cend(),
                       [](const auto& item) { return item.label.htmlLabels; }) &&
              !svg.clusters.first().label.htmlLabels,
          QStringLiteral("Global htmlLabels:false branch drifted"));
  require(std::all_of(mixed.nodes.cbegin(), mixed.nodes.cend(),
                      [](const auto& item) { return item.label.htmlLabels; }) &&
              !mixed.clusters.first().label.htmlLabels,
          QStringLiteral("Legacy flowchart.htmlLabels mixed branch drifted"));

  const auto& wrapped = flowScene(entries.value(QStringLiteral("flow-wrap-80")),
                                  QStringLiteral("flow-wrap-80"));
  const auto& alpha = node(wrapped, QStringLiteral("A"));
  const auto& second = node(wrapped, QStringLiteral("B"));
  require(close(alpha.width, 140.0) && close(alpha.height, 126.0) &&
              close(second.width, 140.0) && close(second.height, 78.0) &&
              close(alpha.label.maximumLineWidth, 80.0),
          QStringLiteral("flowchart.wrappingWidth=80 geometry drifted: "
                         "A=%1x%2 B=%3x%4")
              .arg(alpha.width).arg(alpha.height)
              .arg(second.width).arg(second.height));

  const auto& margin = flowScene(entries.value(QStringLiteral("flow-subgraph-margin")),
                                 QStringLiteral("flow-subgraph-margin"));
  require(margin.clusters.size() == 1 &&
              close(margin.clusters.first().height, 310.0),
          QStringLiteral("subGraphTitleMargin geometry drifted: %1")
              .arg(margin.clusters.isEmpty() ? -1.0
                                             : margin.clusters.first().height));

  const auto* requirementHtml = dynamic_cast<const requirement::RequirementScene*>(
      entries.value(QStringLiteral("requirement-html-true")).scene.get());
  const auto* requirementSvg = dynamic_cast<const requirement::RequirementScene*>(
      entries.value(QStringLiteral("requirement-html-false")).scene.get());
  require(requirementHtml && requirementSvg &&
              requirementHtml->style.htmlLabels &&
              !requirementSvg->style.htmlLabels &&
              requirementHtml->nodes.size() == 2 &&
              requirementSvg->nodes.size() == 2 &&
              close(requirementHtml->nodes.at(0).size.width(), 228.140625) &&
              close(requirementHtml->nodes.at(0).size.height(), 184.0) &&
              close(requirementHtml->nodes.at(1).size.width(), 129.578125) &&
              close(requirementHtml->nodes.at(1).size.height(), 136.0) &&
              close(requirementSvg->nodes.at(0).size.width(), 229.369568) &&
              close(requirementSvg->nodes.at(0).size.height(), 202.0) &&
              close(requirementSvg->nodes.at(1).size.width(), 129.59375) &&
              close(requirementSvg->nodes.at(1).size.height(), 146.0),
          QStringLiteral("Requirement htmlLabels branch drifted: html=%1x%2/%3x%4 "
                         "svg=%5x%6/%7x%8")
              .arg(requirementHtml ? requirementHtml->nodes.value(0).size.width() : -1)
              .arg(requirementHtml ? requirementHtml->nodes.value(0).size.height() : -1)
              .arg(requirementHtml ? requirementHtml->nodes.value(1).size.width() : -1)
              .arg(requirementHtml ? requirementHtml->nodes.value(1).size.height() : -1)
              .arg(requirementSvg ? requirementSvg->nodes.value(0).size.width() : -1)
              .arg(requirementSvg ? requirementSvg->nodes.value(0).size.height() : -1)
              .arg(requirementSvg ? requirementSvg->nodes.value(1).size.width() : -1)
              .arg(requirementSvg ? requirementSvg->nodes.value(1).size.height() : -1));

  const auto& inheritedOff = flowScene(
      entries.value(QStringLiteral("flow-inherit-isolated-false")),
      QStringLiteral("flow-inherit-isolated-false"));
  const auto& inheritedOn = flowScene(
      entries.value(QStringLiteral("flow-inherit-isolated-true")),
      QStringLiteral("flow-inherit-isolated-true"));
  require(inheritedOff.clusters.size() == 1 &&
              inheritedOn.clusters.size() == 1 &&
              inheritedOff.nodes.size() == 3 && inheritedOn.nodes.size() == 3 &&
              close(inheritedOff.bounds.width(), 140.40625) &&
              close(inheritedOff.bounds.height(), 362.0) &&
              close(inheritedOff.clusters.first().width, 140.40625) &&
              close(inheritedOff.clusters.first().height, 258.0) &&
              close(node(inheritedOff, QStringLiteral("A")).cx,
                    node(inheritedOff, QStringLiteral("B")).cx) &&
              entries.value(QStringLiteral("flow-inherit-isolated-false"))
                      .naturalSize == QSize(156, 378) &&
              close(inheritedOn.bounds.width(), 290.640625) &&
              close(inheritedOn.bounds.height(), 228.0) &&
              close(inheritedOn.clusters.first().width, 290.640625) &&
              close(inheritedOn.clusters.first().height, 124.0) &&
              close(node(inheritedOn, QStringLiteral("A")).cy,
                    node(inheritedOn, QStringLiteral("B")).cy) &&
              entries.value(QStringLiteral("flow-inherit-isolated-true"))
                      .naturalSize == QSize(307, 244),
          QStringLiteral("flowchart.inheritDir parser/DB/layout projection drifted: "
                         "off=%1x%2 cluster=%3x%4 A=%5,%6 B=%7,%8; "
                         "on=%9x%10 cluster=%11x%12 A=%13,%14 B=%15,%16")
              .arg(inheritedOff.bounds.width())
              .arg(inheritedOff.bounds.height())
              .arg(inheritedOff.clusters.first().width)
              .arg(inheritedOff.clusters.first().height)
              .arg(node(inheritedOff, QStringLiteral("A")).cx)
              .arg(node(inheritedOff, QStringLiteral("A")).cy)
              .arg(node(inheritedOff, QStringLiteral("B")).cx)
              .arg(node(inheritedOff, QStringLiteral("B")).cy)
              .arg(inheritedOn.bounds.width())
              .arg(inheritedOn.bounds.height())
              .arg(inheritedOn.clusters.first().width)
              .arg(inheritedOn.clusters.first().height)
              .arg(node(inheritedOn, QStringLiteral("A")).cx)
              .arg(node(inheritedOn, QStringLiteral("A")).cy)
              .arg(node(inheritedOn, QStringLiteral("B")).cx)
              .arg(node(inheritedOn, QStringLiteral("B")).cy));

  for (const QString& id : {QStringLiteral("class-layout-elk"),
                            QStringLiteral("class-layout-unknown"),
                            QStringLiteral("class-renderer-elk"),
                            QStringLiteral("class-renderer-dagre-d3")}) {
    require(dynamic_cast<const classdiagram::ClassScene*>(entries.value(id).scene.get()),
            QStringLiteral("%1 did not take Class fallback route").arg(id));
  }
  for (const QString& id : {QStringLiteral("state-renderer-elk"),
                            QStringLiteral("state-renderer-unknown")}) {
    require(dynamic_cast<const state::StateScene*>(entries.value(id).scene.get()),
            QStringLiteral("%1 did not take State fallback route").arg(id));
  }
  for (const QString& id : {
           QStringLiteral("class-look-classic"),
           QStringLiteral("class-look-neo"),
           QStringLiteral("class-look-handdrawn-7"),
           QStringLiteral("class-look-handdrawn-9"),
           QStringLiteral("class-look-wrong-case")}) {
    const auto* value = dynamic_cast<const classdiagram::ClassScene*>(
        entries.value(id).scene.get());
    require(value, QStringLiteral("%1 did not produce ClassScene").arg(id));
    const bool expectedHandDrawn = id.contains(QLatin1String("handdrawn-"));
    require(value->handDrawn == expectedHandDrawn &&
                (!expectedHandDrawn ||
                 (value->nodes.size() == 2 &&
                  std::all_of(value->nodes.cbegin(), value->nodes.cend(),
                              [](const auto& node) {
                                return !node.roughDrawables.isEmpty() &&
                                       node.paintedBounds.isValid();
                              }) &&
                  !value->edges.isEmpty() &&
                  !value->edges.first().roughDrawables.isEmpty())),
            QStringLiteral("%1 Class look/Rough projection drifted").arg(id));
  }
  for (const QString& id : {
           QStringLiteral("state-look-classic"),
           QStringLiteral("state-look-neo"),
           QStringLiteral("state-look-handdrawn-7"),
           QStringLiteral("state-look-handdrawn-9"),
           QStringLiteral("state-look-wrong-case")}) {
    const auto* value = dynamic_cast<const state::StateScene*>(
        entries.value(id).scene.get());
    require(value, QStringLiteral("%1 did not produce StateScene").arg(id));
    const bool expectedHandDrawn = id.contains(QLatin1String("handdrawn-"));
    require(value->handDrawn == expectedHandDrawn &&
                (!expectedHandDrawn ||
                 (value->nodes.size() == 4 &&
                  std::all_of(value->nodes.cbegin(), value->nodes.cend(),
                              [](const auto& node) {
                                return !node.roughDrawables.isEmpty() &&
                                       node.paintedBounds.isValid();
                              }) &&
                  value->edges.size() == 3 &&
                  std::all_of(value->edges.cbegin(), value->edges.cend(),
                              [](const auto& edge) {
                                return !edge.roughDrawable.sets.isEmpty();
                              }))),
            QStringLiteral("%1 State look/Rough projection drifted").arg(id));
  }
  const auto* classSeed7 = dynamic_cast<const classdiagram::ClassScene*>(
      entries.value(QStringLiteral("class-look-handdrawn-7")).scene.get());
  const auto* classSeed9 = dynamic_cast<const classdiagram::ClassScene*>(
      entries.value(QStringLiteral("class-look-handdrawn-9")).scene.get());
  const auto* stateSeed7 = dynamic_cast<const state::StateScene*>(
      entries.value(QStringLiteral("state-look-handdrawn-7")).scene.get());
  const auto* stateSeed9 = dynamic_cast<const state::StateScene*>(
      entries.value(QStringLiteral("state-look-handdrawn-9")).scene.get());
  require(classSeed7 && classSeed9 && stateSeed7 && stateSeed9 &&
              classSeed7->bounds != classSeed9->bounds &&
              stateSeed7->bounds != stateSeed9->bounds,
          QStringLiteral("handDrawnSeed no longer changes Rough geometry"));
  const auto requireHandDrawnGeometry = [&](const QString& id,
                                             const auto& scene,
                                             bool recursiveSvgFrame) {
    const QJsonObject& expected = fixtureCase(casesById, id);
    requireSize(scene.bounds.size(),
                recursiveSvgFrame
                    ? viewBoxSize(expected)
                    : expected.value(QStringLiteral("bbox")).toObject(),
                id + QStringLiteral(" content"));
    const QJsonArray clusters = expected.value(QStringLiteral("clusters")).toArray();
    const QJsonArray stateClusters =
        expected.value(QStringLiteral("stateClusters")).toArray();
    const QJsonArray expectedClusters = clusters.isEmpty() ? stateClusters : clusters;
    require(scene.clusters.size() == expectedClusters.size(),
            QStringLiteral("%1 cluster count differs").arg(id));
    for (qsizetype index = 0; index < scene.clusters.size(); ++index)
      requireSize(scene.clusters.at(index).paintedBounds.size(),
                  expectedClusters.at(index).toObject(),
                  QStringLiteral("%1 cluster %2").arg(id).arg(index));

    const QJsonArray expectedEdges =
        expected.value(QStringLiteral("roughEdges")).toArray();
    require(scene.edges.size() == expectedEdges.size(),
            QStringLiteral("%1 edge count differs").arg(id));
    QVector<bool> matched(expectedEdges.size(), false);
    for (qsizetype index = 0; index < scene.edges.size(); ++index) {
      const QSizeF actual = scene.edges.at(index).pathBounds.size();
      qsizetype match = -1;
      for (qsizetype candidate = 0; candidate < expectedEdges.size(); ++candidate) {
        if (matched.at(candidate)) continue;
        const QJsonObject expectedEdge = expectedEdges.at(candidate).toObject();
        if (close(actual.width(), expectedEdge.value(QStringLiteral("width")).toDouble(),
                  0.001) &&
            close(actual.height(), expectedEdge.value(QStringLiteral("height")).toDouble(),
                  0.001)) {
          match = candidate;
          break;
        }
      }
      require(match >= 0,
              QStringLiteral("%1 edge %2 has no one-to-one upstream match: %3x%4")
                  .arg(id).arg(index).arg(actual.width(), 0, 'g', 17)
                  .arg(actual.height(), 0, 'g', 17));
      matched[match] = true;
    }
  };
  requireHandDrawnGeometry(QStringLiteral("class-look-handdrawn-7"), *classSeed7,
                           false);
  requireHandDrawnGeometry(QStringLiteral("class-look-handdrawn-9"), *classSeed9,
                           false);
  requireHandDrawnGeometry(QStringLiteral("state-look-handdrawn-7"), *stateSeed7,
                           true);
  requireHandDrawnGeometry(QStringLiteral("state-look-handdrawn-9"), *stateSeed9,
                           true);
  const auto requireStateTerminal = [&](const QString& id,
                                         const state::StateScene& scene) {
    const QJsonArray expected = fixtureCase(casesById, id)
                                    .value(QStringLiteral("stateTerminals"))
                                    .toArray();
    const auto terminal = std::find_if(scene.nodes.cbegin(), scene.nodes.cend(),
                                       [](const auto& item) {
                                         return item.shape == QLatin1String("stateStart");
                                       });
    require(terminal != scene.nodes.cend() && expected.size() == 1,
            QStringLiteral("%1 state terminal projection differs").arg(id));
    requireSize(terminal->paintedBounds.size(), expected.first().toObject(),
                id + QStringLiteral(" terminal"));
  };
  requireStateTerminal(QStringLiteral("state-look-handdrawn-7"), *stateSeed7);
  requireStateTerminal(QStringLiteral("state-look-handdrawn-9"), *stateSeed9);
  const auto& swimlane = flowScene(
      entries.value(QStringLiteral("swimlane-layout-unknown")),
      QStringLiteral("swimlane-layout-unknown"));
  require(swimlane.clusters.size() == 2 &&
              std::all_of(swimlane.clusters.cbegin(), swimlane.clusters.cend(),
                          [](const auto& item) { return item.swimlane; }),
          QStringLiteral("Swimlane unknown-layout fallback drifted"));

  const QUrl upstreamDocumentUrl(
      QStringLiteral("file:///G:/github/mermaid-cli/index.html"));
  for (const QJsonValue& value : cases) {
    const QJsonObject expected = value.toObject();
    const QString id = expected.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QLatin1String("marker-"))) continue;
    const QString diagramId = QStringLiteral("remaining-") + id;
    const auto rendered = editor::MermaidRenderCache::renderMermaidSourceToSvg(
        expected.value(QStringLiteral("source")).toString(), 0,
        upstreamDocumentUrl, diagramId);
    require(!rendered.svg.isEmpty(), id + QStringLiteral(" native SVG is empty"));

    QJsonArray expectedReferences;
    const QJsonValue markerValue = expected.value(QStringLiteral("markers"));
    const QJsonArray markers = markerValue.isArray()
        ? markerValue.toArray() : QJsonArray{markerValue};
    for (const QJsonValue& markerItem : markers) {
      const QJsonObject marker = markerItem.toObject();
      expectedReferences.append(QJsonObject{
          {QStringLiteral("start"), marker.value(QStringLiteral("start"))},
          {QStringLiteral("end"), marker.value(QStringLiteral("end"))}});
    }
    const QJsonArray actualReferences = svgMarkerReferences(rendered.svg);
    require(actualReferences == expectedReferences,
            QStringLiteral("%1 marker references differ:\nnative=%2\nupstream=%3")
                .arg(id,
                     QString::fromUtf8(QJsonDocument(actualReferences)
                                           .toJson(QJsonDocument::Compact)),
                     QString::fromUtf8(QJsonDocument(expectedReferences)
                                           .toJson(QJsonDocument::Compact))));

    const QHash<QString, QJsonObject> actualDefinitions =
        svgMarkerDefinitions(rendered.svg);
    const QJsonArray expectedDefinitions =
        expected.value(QStringLiteral("markerDefinitions")).toArray();
    require(actualDefinitions.size() == expectedDefinitions.size(),
            QStringLiteral("%1 marker definition count differs: native=%2 upstream=%3")
                .arg(id).arg(actualDefinitions.size()).arg(expectedDefinitions.size()));
    for (const QJsonValue& definitionValue : expectedDefinitions) {
      const QJsonObject definition = definitionValue.toObject();
      const QString markerId = definition.value(QStringLiteral("id")).toString();
      require(actualDefinitions.contains(markerId),
              QStringLiteral("%1 missing marker definition %2").arg(id, markerId));
      QJsonObject expectedAttributes;
      for (const QString& name : {
               QStringLiteral("viewBox"), QStringLiteral("refX"),
               QStringLiteral("refY"), QStringLiteral("markerWidth"),
               QStringLiteral("markerHeight"), QStringLiteral("markerUnits"),
               QStringLiteral("orient")})
        expectedAttributes[name] = definition.value(name);
      const QJsonObject actualDefinition = actualDefinitions.value(markerId);
      QJsonObject actualAttributes = actualDefinition;
      actualAttributes.remove(QStringLiteral("children"));
      require(actualAttributes == expectedAttributes &&
                  jsonContains(actualDefinition.value(QStringLiteral("children")),
                               definition.value(QStringLiteral("children"))),
              QStringLiteral("%1 marker %2 attributes differ:\nnative=%3\nupstream=%4")
                  .arg(id, markerId,
                       QString::fromUtf8(QJsonDocument(actualDefinition)
                                             .toJson(QJsonDocument::Compact)),
                       QString::fromUtf8(QJsonDocument(expectedAttributes)
                                             .toJson(QJsonDocument::Compact))));
    }
  }

  qDebug() << "MermaidRemainingParityTest:" << cases.size()
           << "upstream-driven cases passed";
  return 0;
}
