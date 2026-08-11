#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
constexpr qreal kGeometryTolerance = 0.02;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool value, const QString& message) {
  if (!value) fail(message);
}

void near(qreal actual, qreal expected, qreal tolerance, const QString& path) {
  if (std::abs(actual - expected) > tolerance) {
    fail(QStringLiteral("%1: %2 != %3")
             .arg(path)
             .arg(actual, 0, 'g', 17)
             .arg(expected, 0, 'g', 17));
  }
}

QVector<qreal> numbers(const QString& text) {
  QVector<qreal> result;
  static const QRegularExpression re(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[-+]?\d+)?)"),
      QRegularExpression::CaseInsensitiveOption);
  auto it = re.globalMatch(text);
  while (it.hasNext()) result.append(it.next().captured().toDouble());
  return result;
}

bool samePaint(const QString& actual, const QString& expected) {
  if (color::isParsableColor(actual) && color::isParsableColor(expected))
    return color::toQColor(actual).rgba() == color::toQColor(expected).rgba();
  return actual.trimmed().compare(expected.trimmed(), Qt::CaseInsensitive) == 0;
}

const flowscene::FlowSceneCluster* clusterById(
    const flowscene::FlowScene& scene, const QString& id) {
  const auto it = std::find_if(
      scene.clusters.cbegin(), scene.clusters.cend(), [&](const auto& cluster) {
        return cluster.id == id || id.endsWith(QLatin1Char('-') + cluster.id);
      });
  return it == scene.clusters.cend() ? nullptr : &*it;
}

int moveCount(const QString& path) {
  int count = 0;
  for (const QChar ch : path)
    if (ch == QLatin1Char('M')) ++count;
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Swimlane config fixture"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("52410f4b84193acad7174638fac0e80d7acc7d22d5beb05863ab05b009b2b37c"),
          QStringLiteral("Swimlane config bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("c3e612d90d9d086d5dbb99cb8f2198de7464de19fc5ca38a94af806f34d348d2"),
          QStringLiteral("Swimlane config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 20, QStringLiteral("Swimlane config case count"));

  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    editor::MermaidRenderCache cache;
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral("/ready: ") + entry.errorMessage);
    const auto* scene = dynamic_cast<const flowscene::FlowScene*>(entry.scene.get());
    require(scene, id + QStringLiteral("/scene"));

    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const bool lineHopCase = id.startsWith(QLatin1String("line-hops-"));
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    require(entry.metadata.svgUseMaxWidth ==
                (rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    const qreal padding = entry.metadata.diagramPadding;
    if (!lineHopCase) {
      near(scene->bounds.x(), viewBox[0] + padding, kGeometryTolerance,
           id + QStringLiteral("/bounds-x"));
      near(scene->bounds.y(), viewBox[1] + padding, kGeometryTolerance,
           id + QStringLiteral("/bounds-y"));
      near(scene->bounds.width(), viewBox[2] - 2 * padding, kGeometryTolerance,
           id + QStringLiteral("/bounds-w"));
      near(scene->bounds.height(), viewBox[3] - 2 * padding, kGeometryTolerance,
           id + QStringLiteral("/bounds-h"));
    }

    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    require(scene->nodes.size() == expectedNodes.size(), id + QStringLiteral("/node-count"));
    for (qsizetype i = 0; !lineHopCase && i < scene->nodes.size(); ++i) {
      const auto& node = scene->nodes.at(i);
      const QJsonObject oracle = expectedNodes.at(i).toObject();
      const QVector<qreal> transform = numbers(oracle.value(QStringLiteral("transform")).toString());
      const QJsonObject box = oracle.value(QStringLiteral("bbox")).toObject();
      near(node.cx, transform.value(0), kGeometryTolerance,
           id + QStringLiteral("/node%1-x").arg(i));
      near(node.cy, transform.value(1), kGeometryTolerance,
           id + QStringLiteral("/node%1-y").arg(i));
      near(node.width, box.value(QStringLiteral("width")).toDouble(), kGeometryTolerance,
           id + QStringLiteral("/node%1-w").arg(i));
      near(node.height, box.value(QStringLiteral("height")).toDouble(), kGeometryTolerance,
           id + QStringLiteral("/node%1-h").arg(i));
    }

    const QJsonArray expectedClusters = expected.value(QStringLiteral("clusters")).toArray();
    require(scene->clusters.size() == expectedClusters.size(),
            id + QStringLiteral("/cluster-count"));
    for (const QJsonValue& clusterValue : expectedClusters) {
      const QJsonObject oracle = clusterValue.toObject();
      const auto* cluster = clusterById(*scene, oracle.value(QStringLiteral("id")).toString());
      require(cluster, id + QStringLiteral("/cluster-id"));
      if (!lineHopCase) {
        const QJsonObject box = oracle.value(QStringLiteral("bbox")).toObject();
        const QRectF actual = cluster->paintedBounds.isValid()
            ? cluster->paintedBounds
            : QRectF(cluster->cx - cluster->width / 2.0,
                     cluster->cy - cluster->height / 2.0,
                     cluster->width, cluster->height);
        near(actual.x(), box.value(QStringLiteral("x")).toDouble(),
             kGeometryTolerance, id + QStringLiteral("/cluster-x"));
        near(actual.y(), box.value(QStringLiteral("y")).toDouble(),
             kGeometryTolerance, id + QStringLiteral("/cluster-y"));
        near(actual.width(), box.value(QStringLiteral("width")).toDouble(),
             kGeometryTolerance, id + QStringLiteral("/cluster-w"));
        near(actual.height(), box.value(QStringLiteral("height")).toDouble(),
             kGeometryTolerance, id + QStringLiteral("/cluster-h"));
      }
      const QJsonArray rects = oracle.value(QStringLiteral("rects")).toArray();
      if (!rects.isEmpty()) {
        const QJsonObject computed = rects.first().toObject()
                                         .value(QStringLiteral("computed")).toObject();
        require(samePaint(cluster->fill, computed.value(QStringLiteral("fill")).toString()),
                id + QStringLiteral("/cluster-fill"));
        require(samePaint(cluster->stroke, computed.value(QStringLiteral("stroke")).toString()),
                id + QStringLiteral("/cluster-stroke"));
      }
    }

    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    require(scene->edges.size() == expectedEdges.size(), id + QStringLiteral("/edge-count"));
    for (qsizetype i = 0; i < scene->edges.size(); ++i) {
      const auto& edge = scene->edges.at(i);
      const QJsonObject oracle = expectedEdges.at(i).toObject();
      const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
      const QJsonObject computed = oracle.value(QStringLiteral("computed")).toObject();
      require(samePaint(edge.stroke, computed.value(QStringLiteral("stroke")).toString()),
              id + QStringLiteral("/edge%1-stroke").arg(i));
      if (!lineHopCase) {
        const QVector<qreal> actualPath = numbers(edge.path);
        const QVector<qreal> expectedPath = numbers(attrs.value(QStringLiteral("d")).toString());
        require(actualPath.size() == expectedPath.size(),
                id + QStringLiteral("/edge%1-structure: %2 != %3\n%4\n%5")
                         .arg(i).arg(actualPath.size()).arg(expectedPath.size())
                         .arg(edge.path, attrs.value(QStringLiteral("d")).toString()));
        for (qsizetype j = 0; j < actualPath.size(); ++j) {
          near(actualPath[j], expectedPath[j], 0.001,
               id + QStringLiteral("/edge%1/%2").arg(i).arg(j));
        }
      }
    }

    if (id == QLatin1String("line-hops-arc")) {
      require(std::any_of(scene->edges.cbegin(), scene->edges.cend(), [](const auto& edge) {
                return edge.path.contains(QLatin1String("A6,6"));
              }), id + QStringLiteral("/arc"));
    } else if (id == QLatin1String("line-hops-gap")) {
      require(std::any_of(scene->edges.cbegin(), scene->edges.cend(), [](const auto& edge) {
                return moveCount(edge.path) > 1 && !edge.path.contains(QLatin1String("A6,6"));
              }), id + QStringLiteral("/gap"));
    } else if (id == QLatin1String("line-hops-false")) {
      require(std::none_of(scene->edges.cbegin(), scene->edges.cend(), [](const auto& edge) {
                return moveCount(edge.path) > 1 || edge.path.contains(QLatin1String("A6,6"));
              }), id + QStringLiteral("/disabled"));
    } else if (id == QLatin1String("curve-linear")) {
      require(std::none_of(scene->edges.cbegin(), scene->edges.cend(), [](const auto& edge) {
                return edge.path.contains(QLatin1Char('Q')) || edge.path.contains(QLatin1Char('C'));
              }), id + QStringLiteral("/linear"));
    }

    if (id == QLatin1String("layout-dagre")) {
      require(std::none_of(scene->clusters.cbegin(), scene->clusters.cend(), [](const auto& cluster) {
                return cluster.swimlane;
              }), id + QStringLiteral("/dagre-clusters"));
    } else if (!scene->clusters.isEmpty()) {
      require(std::all_of(scene->clusters.cbegin(), scene->clusters.cend(), [](const auto& cluster) {
                return cluster.swimlane;
              }), id + QStringLiteral("/swimlane-clusters"));
    }
  }

  std::puts("MermaidSwimlaneEdgeParityTest: 20/20 passed");
  return 0;
}
