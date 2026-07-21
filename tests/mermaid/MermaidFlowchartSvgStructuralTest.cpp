#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowMarkers.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
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

qreal number(const QJsonObject& attributes, const QString& name) {
  bool ok = false;
  const qreal result = attributes.value(name).toString().toDouble(&ok);
  require(ok, QStringLiteral("Invalid numeric SVG attribute %1").arg(name));
  return result;
}

flowtheme::FlowThemeId themeId(const QString& name) {
  static const QHash<QString, flowtheme::FlowThemeId> themes = {
      {QStringLiteral("default"), flowtheme::FlowThemeId::Default},
      {QStringLiteral("dark"), flowtheme::FlowThemeId::Dark},
      {QStringLiteral("neo"), flowtheme::FlowThemeId::Neo},
      {QStringLiteral("neo-dark"), flowtheme::FlowThemeId::NeoDark},
  };
  const auto found = themes.constFind(name);
  require(found != themes.cend(), QStringLiteral("Unsupported structural theme: %1").arg(name));
  return found.value();
}

bool containsLayer(const QJsonArray& layers, const QString& cssClass) {
  return std::any_of(layers.cbegin(), layers.cend(), [&](const QJsonValue& value) {
    return value.toString().split(QLatin1Char(' ')).contains(cssClass) ||
           value.toString().contains(QLatin1Char(':') + cssClass);
  });
}

int layerIndex(const QJsonArray& layers, const QString& cssClass) {
  for (qsizetype index = 0; index < layers.size(); ++index)
    if (layers.at(index).toString().contains(cssClass))
      return static_cast<int>(index);
  return -1;
}

QString markerReference(const QString& value) {
  static const QRegularExpression type(
      QStringLiteral(R"((point|circle|cross)(Start|End))"));
  const auto match = type.match(value);
  return match.hasMatch() ? match.captured(1) + match.captured(2) : QString();
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected flowchart SVG structural manifest"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open flowchart SVG structural manifest"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Flowchart SVG structural upstream drifted"));
  MermaidFontRegistry::ensureLoaded();

  int structuralCases = 0;
  int markerCases = 0;
  int edgeLabelCases = 0;
  int clusterLabelCases = 0;
  int ariaCases = 0;
  int orderedEntries = 0;
  QSet<QString> markerTypes;

  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    if (!fixture.contains(QStringLiteral("svgStructural"))) continue;
    ++structuralCases;
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject structure = fixture.value(QStringLiteral("content")).toObject()
                                      .value(QStringLiteral("svgStructure")).toObject();
    require(!structure.isEmpty(), QStringLiteral("%1 has no SVG structure").arg(id));

    const QJsonObject rootAttributes = structure.value(QStringLiteral("root")).toObject();
    require(rootAttributes.value(QStringLiteral("class")).toString()
                    .split(QLatin1Char(' ')).contains(QStringLiteral("flowchart")) &&
                rootAttributes.value(QStringLiteral("xmlns")).toString() ==
                    QLatin1String("http://www.w3.org/2000/svg") &&
                rootAttributes.value(QStringLiteral("role")).toString() ==
                    QLatin1String("graphics-document document") &&
                rootAttributes.value(QStringLiteral("aria-roledescription")).toString() ==
                    QLatin1String("flowchart-v2"),
            QStringLiteral("%1 root SVG role/class drifted").arg(id));
    const QStringList viewBox = rootAttributes.value(QStringLiteral("viewBox")).toString()
                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QJsonObject content = fixture.value(QStringLiteral("content")).toObject();
    require(viewBox.size() == 4 && viewBox.at(2).toDouble() > 0.0 &&
                viewBox.at(3).toDouble() > 0.0 &&
                std::abs(viewBox.at(2).toDouble() -
                         content.value(QStringLiteral("width")).toDouble()) < 0.001 &&
                std::abs(viewBox.at(3).toDouble() -
                         content.value(QStringLiteral("height")).toDouble()) < 0.001,
            QStringLiteral("%1 root viewBox is invalid").arg(id));

    const flowchart::Flowchart chart = flowchart::Flowchart::parse(source);
    const auto& data = chart.data();
    const QString labelledBy = rootAttributes.value(QStringLiteral("aria-labelledby")).toString();
    const QString describedBy = rootAttributes.value(QStringLiteral("aria-describedby")).toString();
    if (!labelledBy.isEmpty()) {
      ++ariaCases;
      require(!data.accTitle.isEmpty() && !data.accDescription.isEmpty() &&
                  structure.value(QStringLiteral("ariaTitle")).toString() == data.accTitle &&
                  structure.value(QStringLiteral("ariaDescription")).toString() ==
                      data.accDescription &&
                  labelledBy.contains(QLatin1String("chart-title-diagram")) &&
                  describedBy.contains(QLatin1String("chart-desc-diagram")),
              QStringLiteral("%1 accessibility structure drifted").arg(id));
    } else {
      require(data.accTitle.isEmpty() && data.accDescription.isEmpty(),
              QStringLiteral("%1 lost accessibility references").arg(id));
    }

    const QJsonArray layers = structure.value(QStringLiteral("layerOrder")).toArray();
    require(containsLayer(layers, QStringLiteral("clusters")) &&
                containsLayer(layers, QStringLiteral("edgePaths")) &&
                containsLayer(layers, QStringLiteral("edgeLabels")) &&
                containsLayer(layers, QStringLiteral("nodes")) &&
                layerIndex(layers, QStringLiteral("clusters")) <
                    layerIndex(layers, QStringLiteral("edgePaths")) &&
                layerIndex(layers, QStringLiteral("edgePaths")) <
                    layerIndex(layers, QStringLiteral("edgeLabels")) &&
                layerIndex(layers, QStringLiteral("edgeLabels")) <
                    layerIndex(layers, QStringLiteral("nodes")),
            QStringLiteral("%1 SVG layer order drifted").arg(id));
    const QJsonArray domOrder = structure.value(QStringLiteral("domOrder")).toArray();
    require(!domOrder.isEmpty(), QStringLiteral("%1 DOM order is empty").arg(id));
    orderedEntries += domOrder.size();

    flowchart::FlowTextOptions textOptions;
    const bool fixedNoto = fixture.value(QStringLiteral("fontMode")).toString() ==
                           QLatin1String("noto");
    textOptions.fontFamily = fixedNoto ? MermaidFontRegistry::primaryFamily()
                                       : QStringLiteral("Arial");
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        fixture.value(QStringLiteral("look")).toString(QStringLiteral("classic")));
    textOptions.look = look;
    const auto theme = flowtheme::resolveFlowTheme(
        themeId(fixture.value(QStringLiteral("theme")).toString()),
        {{QStringLiteral("fontFamily"), fixedNoto
              ? MermaidFontRegistry::cssFamilyStack() : QStringLiteral("Arial")}});
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(data, textOptions);
    flowchart::FlowLayoutOptions options;
    options.look = look;
    for (const flowchart::FlowEdge& edge : data.edges) {
      if (edge.text.isEmpty()) continue;
      const auto prepared = flowchart::layoutFlowchartEdgeLabel(edge, textOptions);
      options.measuredEdgeLabels.insert(edge.id, prepared.size);
      options.preparedEdgeLabels.insert(edge.id, prepared);
    }
    for (const flowchart::FlowSubgraph& cluster : data.subgraphs)
      if (!cluster.title.isEmpty())
        options.measuredClusterLabels.insert(
            cluster.id, flowchart::measureFlowchartClusterLabel(cluster, textOptions));
    const auto layout = flowchart::layoutFlowchartNodes(data, sizes, options);
    const flowscene::FlowScene scene = flowscene::buildFlowScene(
        data, layout, theme, look);

    const QJsonObject counts = structure.value(QStringLiteral("counts")).toObject();
    require(counts.value(QStringLiteral("defs")).toInt() >= 2 &&
                counts.value(QStringLiteral("nodes")).toInt() == scene.nodes.size() &&
                counts.value(QStringLiteral("edgePaths")).toInt() == scene.edges.size() &&
                counts.value(QStringLiteral("clusters")).toInt() == scene.clusters.size() &&
                counts.value(QStringLiteral("foreignObject")).toInt() == scene.nodes.size(),
            QStringLiteral("%1 browser/native structural counts differ").arg(id));

    const QJsonArray browserNodes = structure.value(QStringLiteral("nodes")).toArray();
    require(browserNodes.size() == scene.nodes.size(),
            QStringLiteral("%1 node attribute array size drifted").arg(id));
    for (qsizetype index = 0; index < browserNodes.size(); ++index) {
      const QJsonObject node = browserNodes.at(index).toObject();
      const QJsonObject attributes = node.value(QStringLiteral("attributes")).toObject();
      require(attributes.value(QStringLiteral("class")).toString()
                      .split(QLatin1Char(' ')).contains(QStringLiteral("node")) &&
                  attributes.value(QStringLiteral("id")).toString()
                      .contains(QLatin1Char('-') + scene.nodes.at(index).id +
                                QLatin1Char('-')) &&
                  !node.value(QStringLiteral("shapeTag")).toString().isEmpty() &&
                  node.value(QStringLiteral("shapeAttributes")).toObject()
                      .value(QStringLiteral("class")).toString()
                      .contains(QLatin1String("label-container")) &&
                  node.value(QStringLiteral("labelTag")).toString() ==
                      QLatin1String("foreignObject") &&
                  !node.value(QStringLiteral("labelText")).toString().isEmpty(),
              QStringLiteral("%1 node %2 container structure drifted")
                  .arg(id, scene.nodes.at(index).id));
    }

    const QJsonArray browserEdges = structure.value(QStringLiteral("edges")).toArray();
    require(browserEdges.size() == scene.edges.size(),
            QStringLiteral("%1 edge attribute array size drifted").arg(id));
    for (qsizetype index = 0; index < browserEdges.size(); ++index) {
      const QJsonObject attributes = browserEdges.at(index).toObject()
                                         .value(QStringLiteral("attributes")).toObject();
      const auto& edge = scene.edges.at(index);
      require(attributes.value(QStringLiteral("data-edge")).toString() ==
                      QLatin1String("true") &&
                  attributes.value(QStringLiteral("class")).toString()
                      .contains(QLatin1String("flowchart-link")) &&
                  attributes.value(QStringLiteral("data-id")).toString() == edge.id &&
                  attributes.value(QStringLiteral("data-look")).toString() ==
                      (look == flowchart::FlowLook::Neo ? QLatin1String("neo")
                                                       : QLatin1String("classic")) &&
                  !attributes.value(QStringLiteral("d")).toString().isEmpty() &&
                  markerReference(attributes.value(QStringLiteral("marker-start")).toString()) ==
                      edge.markerStart &&
                  markerReference(attributes.value(QStringLiteral("marker-end")).toString()) ==
                      edge.markerEnd,
              QStringLiteral("%1 edge %2 structural attributes differ").arg(id, edge.id));
    }

    const QJsonArray markers = structure.value(QStringLiteral("markers")).toArray();
    if (!markers.isEmpty()) ++markerCases;
    for (const QJsonValue& markerValue : markers) {
      const QJsonObject marker = markerValue.toObject();
      const QString type = marker.value(QStringLiteral("type")).toString();
      if (type.isEmpty()) continue;
      markerTypes.insert(type);
      const QJsonObject attributes = marker.value(QStringLiteral("attributes")).toObject();
      const flowscene::MarkerGeometry geometry = flowscene::markerGeometry(type);
      const QJsonObject child = marker.value(QStringLiteral("childAttributes")).toObject();
      const bool childGeometryMatches = geometry.tag == QLatin1String("path")
          ? child.value(QStringLiteral("d")).toString() == geometry.pathData
          : std::abs(number(child, QStringLiteral("cx")) - geometry.cx) < 0.001 &&
                std::abs(number(child, QStringLiteral("cy")) - geometry.cy) < 0.001 &&
                std::abs(number(child, QStringLiteral("r")) - geometry.r) < 0.001;
      require(!geometry.tag.isEmpty() &&
                  attributes.value(QStringLiteral("viewBox")).toString() == geometry.viewBox &&
                  std::abs(number(attributes, QStringLiteral("refX")) - geometry.refX) < 0.001 &&
                  std::abs(number(attributes, QStringLiteral("refY")) - geometry.refY) < 0.001 &&
                  std::abs(number(attributes, QStringLiteral("markerWidth")) -
                           geometry.markerWidth) < 0.001 &&
                  std::abs(number(attributes, QStringLiteral("markerHeight")) -
                           geometry.markerHeight) < 0.001 &&
                  attributes.value(QStringLiteral("orient")).toString() == QLatin1String("auto") &&
                  attributes.value(QStringLiteral("markerUnits")).toString() ==
                      QLatin1String("userSpaceOnUse") &&
                  marker.value(QStringLiteral("childTag")).toString() == geometry.tag &&
                  child.value(QStringLiteral("class")).toString()
                      .contains(QLatin1String("arrowMarkerPath")) &&
                  childGeometryMatches,
              QStringLiteral("%1 marker %2 geometry drifted").arg(id, type));
    }

    const QJsonArray edgeLabels = structure.value(QStringLiteral("edgeLabels")).toArray();
    if (!edgeLabels.isEmpty()) ++edgeLabelCases;
    require(edgeLabels.size() == scene.edges.size(),
            QStringLiteral("%1 edge label count drifted").arg(id));
    for (qsizetype index = 0; index < edgeLabels.size(); ++index) {
      const QJsonValue& labelValue = edgeLabels.at(index);
      const QJsonObject label = labelValue.toObject();
      require(label.value(QStringLiteral("contentTag")).toString() == QLatin1String("g") &&
                  label.value(QStringLiteral("contentAttributes")).toObject()
                      .value(QStringLiteral("class")).toString().contains(QLatin1String("label")) &&
                  !label.value(QStringLiteral("contentAttributes")).toObject()
                      .value(QStringLiteral("data-id")).toString().isEmpty() &&
                  label.value(QStringLiteral("backgroundTag")).toString().isEmpty() &&
                  label.value(QStringLiteral("tspanCount")).toInt() > 0,
              QStringLiteral("%1 edge label container drifted").arg(id));
      const bool hasText = !scene.edges.at(index).label.text.isEmpty();
      require(label.value(QStringLiteral("text")).toString().isEmpty() == !hasText &&
                  (!hasText || !label.value(QStringLiteral("attributes")).toObject()
                                    .value(QStringLiteral("transform")).toString().isEmpty()),
              QStringLiteral("%1 edge label visibility/transform drifted").arg(id));
    }

    const QJsonArray clusters = structure.value(QStringLiteral("clusters")).toArray();
    if (!clusters.isEmpty()) ++clusterLabelCases;
    for (const QJsonValue& clusterValue : clusters) {
      const QJsonObject cluster = clusterValue.toObject();
      require(cluster.value(QStringLiteral("shapeTag")).toString() == QLatin1String("rect") &&
                  cluster.value(QStringLiteral("attributes")).toObject()
                      .value(QStringLiteral("class")).toString().contains(QLatin1String("cluster")) &&
                  cluster.value(QStringLiteral("labelTag")).toString() == QLatin1String("g") &&
                  cluster.value(QStringLiteral("labelAttributes")).toObject()
                      .value(QStringLiteral("class")).toString().contains(QLatin1String("cluster-label")) &&
                  cluster.value(QStringLiteral("labelContentTag")).toString() ==
                      QLatin1String("text") &&
                  !cluster.value(QStringLiteral("labelAttributes")).toObject()
                      .value(QStringLiteral("transform")).toString().isEmpty() &&
                  !cluster.value(QStringLiteral("labelText")).toString().isEmpty(),
              QStringLiteral("%1 cluster label structure drifted").arg(id));
    }
  }

  require(structuralCases >= 9 && markerCases >= 4 && edgeLabelCases >= 5 &&
              clusterLabelCases >= 3 && ariaCases >= 1 && orderedEntries >= 120 &&
              markerTypes.contains(QStringLiteral("pointEnd")) &&
              markerTypes.contains(QStringLiteral("circleStart")) &&
              markerTypes.contains(QStringLiteral("circleEnd")) &&
              markerTypes.contains(QStringLiteral("crossStart")) &&
              markerTypes.contains(QStringLiteral("crossEnd")),
          QStringLiteral("Flowchart SVG structural coverage regressed"));
  qDebug() << "MermaidFlowchartSvgStructuralTest:" << structuralCases
           << "cases," << orderedEntries << "ordered DOM entries passed";
  return 0;
}
