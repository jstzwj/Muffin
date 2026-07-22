#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/MermaidFontRegistry.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace muffin::mermaid::state;

namespace {
[[noreturn]] void fail(const QString& message) { qFatal("%s", qPrintable(message)); }
bool finiteRect(const QRectF& rect) {
  return std::isfinite(rect.x()) && std::isfinite(rect.y()) &&
      std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
      rect.width() >= 0.0 && rect.height() >= 0.0;
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("fixture path required"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonArray cases = QJsonDocument::fromJson(file.readAll())
      .object().value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const StateDiagram diagram = StateDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    const StateLayoutInput input = buildStateLayoutInput(diagram.data());
    const StateLayoutMeasurements measurements = measureStateLayoutInput(input);
    const StatePlacementResult placement = layoutStateDiagramDagre(input, measurements);
    const StateScene scene = buildStateScene(input, placement);

    const int semanticNodes = static_cast<int>(std::count_if(
        input.nodes.cbegin(), input.nodes.cend(),
        [](const StateLayoutNodeInput& node) { return !node.isGroup; }));
    if (scene.nodes.size() != semanticNodes)
      fail(QStringLiteral("%1: scene node count %2 != input %3")
          .arg(id).arg(scene.nodes.size()).arg(semanticNodes));
    if (scene.edges.size() != input.edges.size())
      fail(QStringLiteral("%1: scene edge count mismatch").arg(id));
    QSet<QString> nodeIds;
    for (const StateSceneNode& node : scene.nodes) {
      if (node.id.isEmpty() || nodeIds.contains(node.id) || !finiteRect(node.bounds) ||
          node.bounds.isEmpty())
        fail(QStringLiteral("%1: invalid or duplicate node '%2'").arg(id, node.id));
      nodeIds.insert(node.id);
    }
    for (const StateSceneNode& cluster : scene.clusters) {
      if (cluster.id.isEmpty() || nodeIds.contains(cluster.id) ||
          !finiteRect(cluster.bounds) || cluster.bounds.isEmpty())
        fail(QStringLiteral("%1: invalid or duplicate cluster '%2'").arg(id, cluster.id));
      nodeIds.insert(cluster.id);
    }
    QSet<QString> edgeIds;
    for (const StateSceneEdge& edge : scene.edges) {
      if (edge.id.isEmpty() || edgeIds.contains(edge.id) || edge.points.size() < 2 ||
          !nodeIds.contains(edge.start) || !nodeIds.contains(edge.end))
        fail(QStringLiteral("%1: invalid edge '%2'").arg(id, edge.id));
      edgeIds.insert(edge.id);
    }
    if ((!scene.nodes.isEmpty() || !scene.clusters.isEmpty()) &&
        (!finiteRect(scene.bounds) || scene.bounds.isEmpty()))
      fail(QStringLiteral("%1: invalid scene bounds").arg(id));
  }
  return 0;
}
