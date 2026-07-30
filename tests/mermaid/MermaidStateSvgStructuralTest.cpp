#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
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
template <typename T>
const T* findById(const QVector<T>& values, const QString& id) {
  const auto it = std::find_if(values.cbegin(), values.cend(),
      [&](const T& value) { return value.id == id; });
  return it == values.cend() ? nullptr : &*it;
}
QStringList strings(const QJsonArray& values) {
  QStringList result;
  for (const QJsonValue& value : values) result.append(value.toString());
  return result;
}
QStringList expectedNodeTags(const QString& shape) {
  if (shape == QLatin1String("stateStart")) return {QStringLiteral("circle")};
  if (shape == QLatin1String("stateEnd") || shape == QLatin1String("fork") ||
      shape == QLatin1String("join") || shape == QLatin1String("choice") ||
      shape == QLatin1String("note") || shape == QLatin1String("rectWithTitle"))
    return {QStringLiteral("g")};
  return {QStringLiteral("rect"), QStringLiteral("g")};
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("Expected state structural fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("406a561cb5a124331b36978ff1a02502d74befc02785466329da9e83e15b63a9"),
          QStringLiteral("State structural fixture drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() ==
              QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("State structural font oracle drifted"));

  editor::MermaidRenderCache cache;
  int nodes = 0, clusters = 0, edges = 0, foreignObjects = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr,
            id + QStringLiteral(": native state scene failed: ") + entry.errorMessage);
    const state::StateScene& scene = *stateScene;
    const QJsonObject structure = fixture.value(QStringLiteral("structure")).toObject();
    const QJsonObject browserRoot = structure.value(QStringLiteral("root")).toObject();
    require(scene.role == browserRoot.value(QStringLiteral("role")).toString() &&
                scene.ariaRoleDescription ==
                    browserRoot.value(QStringLiteral("ariaRoledescription")).toString(),
            id + QStringLiteral(": SVG accessibility root mismatch"));

    const QJsonArray markers = structure.value(QStringLiteral("markers")).toArray();
    require(markers.size() == 1, id + QStringLiteral(": marker definition count drifted"));
    const QJsonObject marker = markers.at(0).toObject();
    require(marker.value(QStringLiteral("markerWidth")).toString().toDouble() ==
                scene.arrowMarkerSize.width() &&
                marker.value(QStringLiteral("markerHeight")).toString().toDouble() ==
                scene.arrowMarkerSize.height() &&
                marker.value(QStringLiteral("refX")).toString().toDouble() ==
                scene.arrowMarkerRef.x() &&
                marker.value(QStringLiteral("refY")).toString().toDouble() ==
                scene.arrowMarkerRef.y() &&
                marker.value(QStringLiteral("orient")).toString() == scene.arrowMarkerOrient &&
                marker.value(QStringLiteral("childTag")).toString() == QLatin1String("path"),
            id + QStringLiteral(": arrow marker structure mismatch"));

    const QJsonArray browserNodes = structure.value(QStringLiteral("nodes")).toArray();
    require(browserNodes.size() == scene.nodes.size(),
            id + QStringLiteral(": node DOM count mismatch"));
    for (const QJsonValue& nodeValue : browserNodes) {
      const QJsonObject expected = nodeValue.toObject();
      const QString nodeId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneNode* actual = findById(scene.nodes, nodeId);
      require(actual, id + QStringLiteral(": missing native node ") + nodeId);
      const QStringList browserTags = strings(expected.value(QStringLiteral("childTags")).toArray());
      const QStringList requiredTags = expectedNodeTags(actual->shape);
      for (const QString& tag : requiredTags)
        require(browserTags.contains(tag),
                id + QLatin1Char('/') + nodeId + QStringLiteral(": missing browser shape tag ") + tag);
      const int browserForeignObjects =
          expected.value(QStringLiteral("foreignObjectCount")).toInt();
      const bool labelBearingShape =
          actual->shape != QLatin1String("stateStart") &&
          actual->shape != QLatin1String("stateEnd") &&
          actual->shape != QLatin1String("fork") &&
          actual->shape != QLatin1String("join") &&
          actual->shape != QLatin1String("choice");
      require((browserForeignObjects > 0) == labelBearingShape &&
                  (!labelBearingShape || !actual->labelDocument.text.isEmpty()),
              id + QLatin1Char('/') + nodeId + QStringLiteral(": label container mismatch"));
      foreignObjects += browserForeignObjects;
      ++nodes;
    }

    const QJsonArray browserClusters = structure.value(QStringLiteral("clusters")).toArray();
    require(browserClusters.size() == scene.clusters.size(),
            id + QStringLiteral(": cluster DOM count mismatch"));
    for (const QJsonValue& clusterValue : browserClusters) {
      const QJsonObject expected = clusterValue.toObject();
      const QString clusterId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneNode* actual = findById(scene.clusters, clusterId);
      require(actual && actual->group,
              id + QStringLiteral(": missing native cluster ") + clusterId);
      const QStringList tags = strings(expected.value(QStringLiteral("childTags")).toArray());
      require(tags.contains(actual->shape == QLatin1String("noteGroup")
                                ? QStringLiteral("rect") : QStringLiteral("g")),
              id + QLatin1Char('/') + clusterId + QStringLiteral(": cluster outline mismatch"));
      ++clusters;
    }

    const QJsonArray browserEdges = structure.value(QStringLiteral("edges")).toArray();
    require(browserEdges.size() == scene.edges.size(),
            id + QStringLiteral(": edge DOM count mismatch"));
    for (const QJsonValue& edgeValue : browserEdges) {
      const QJsonObject expected = edgeValue.toObject();
      const QString edgeId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneEdge* actual = findById(scene.edges, edgeId);
      require(actual && actual->points.size() >= 2,
              id + QStringLiteral(": missing native edge ") + edgeId);
      const bool browserArrow =
          expected.value(QStringLiteral("markerEnd")).toString() == QLatin1String("barbEnd");
      require(browserArrow == (actual->markerEnd == QLatin1String("arrow_barb")),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": marker-end mismatch"));
      const QString classes = expected.value(QStringLiteral("classes")).toString();
      require(classes.contains(QLatin1String("transition")) &&
                  (!classes.contains(QLatin1String("note-edge")) ||
                   actual->classes.contains(QLatin1String("note-edge"))),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": edge class mismatch"));
      require(!expected.value(QStringLiteral("pathCommands")).toString().isEmpty() &&
                  (!actual->path.isEmpty() || actual->points.size() >= 2),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": edge path structure missing"));
      ++edges;
    }
  }
  require(nodes >= 15 && clusters >= 4 && edges >= 9 && foreignObjects >= 10,
          QStringLiteral("State SVG structural coverage regressed"));
  qDebug() << "MermaidStateSvgStructuralTest:" << nodes << "nodes," << clusters
           << "clusters," << edges << "edges passed";
  return 0;
}
