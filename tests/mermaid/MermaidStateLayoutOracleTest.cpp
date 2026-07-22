#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid::state;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
QPointF point(const QJsonObject& value) {
  return {value.value(QStringLiteral("x")).toDouble(),
          value.value(QStringLiteral("y")).toDouble()};
}
QSizeF size(const QJsonObject& value) {
  return {value.value(QStringLiteral("width")).toDouble(),
          value.value(QStringLiteral("height")).toDouble()};
}
template <typename T>
const T* findById(const QVector<T>& values, const QString& id) {
  const auto it = std::find_if(values.cbegin(), values.cend(),
      [&](const T& value) { return value.id == id; });
  return it == values.cend() ? nullptr : &*it;
}
void requireNear(qreal actual, qreal expected, qreal tolerance,
                 const QString& context) {
  if (std::abs(actual - expected) > tolerance)
    fail(QStringLiteral("%1: %2 differs from %3").arg(context).arg(actual).arg(expected));
}
void requireNear(const QPointF& actual, const QPointF& expected, qreal tolerance,
                 const QString& context) {
  requireNear(actual.x(), expected.x(), tolerance, context + QStringLiteral(" x"));
  requireNear(actual.y(), expected.y(), tolerance, context + QStringLiteral(" y"));
}
void requireNear(const QSizeF& actual, const QSizeF& expected, qreal tolerance,
                 const QString& context) {
  requireNear(actual.width(), expected.width(), tolerance, context + QStringLiteral(" width"));
  requireNear(actual.height(), expected.height(), tolerance, context + QStringLiteral(" height"));
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("Expected state layout fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  if (root.value(QStringLiteral("fixtureSha256")).toString() !=
      QLatin1String("406a561cb5a124331b36978ff1a02502d74befc02785466329da9e83e15b63a9"))
    fail(QStringLiteral("State layout fixture drifted"));
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const StateDiagram diagram = StateDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    const StateLayoutInput input = buildStateLayoutInput(diagram.data());
    const StateLayoutMeasurements measurements = measureStateLayoutInput(
        input, muffin::mermaid::MermaidFontRegistry::cssFamilyStack(), 16.0);
    const StatePlacementResult placed = layoutStateDiagramDagre(input, measurements);
    const QPointF origin = placed.nodes.isEmpty() ? QPointF{} : placed.nodes.first().center;
    const QJsonObject geometry = fixture.value(QStringLiteral("geometry")).toObject();
    for (const QJsonValue& nodeValue : geometry.value(QStringLiteral("nodes")).toArray()) {
      const QJsonObject expected = nodeValue.toObject();
      const QString nodeId = expected.value(QStringLiteral("id")).toString();
      const StatePlacementNode* actual = findById(placed.nodes, nodeId);
      if (!actual) fail(id + QStringLiteral(": missing node ") + nodeId);
      const QPointF expectedCenter = point(expected.value(QStringLiteral("center")).toObject());
      const QSizeF expectedSize = size(expected.value(QStringLiteral("bbox")).toObject());
      requireNear(actual->center - origin, expectedCenter, 0.02, id + QLatin1Char('/') + nodeId);
      requireNear(actual->paintedSize, expectedSize, 0.02,
                  id + QLatin1Char('/') + nodeId + QStringLiteral(" painted bbox"));
    }
    for (const QJsonValue& clusterValue : geometry.value(QStringLiteral("clusters")).toArray()) {
      const QJsonObject expected = clusterValue.toObject();
      const QString clusterId = expected.value(QStringLiteral("id")).toString();
      const StatePlacementCluster* actual = findById(placed.clusters, clusterId);
      if (!actual) fail(id + QStringLiteral(": missing cluster ") + clusterId);
      requireNear(actual->center - origin,
                  point(expected.value(QStringLiteral("center")).toObject()), 0.02,
                  id + QLatin1Char('/') + clusterId + QStringLiteral(" center"));
      requireNear(actual->size, size(expected.value(QStringLiteral("bbox")).toObject()), 0.02,
                  id + QLatin1Char('/') + clusterId + QStringLiteral(" bbox"));
    }
    for (const QJsonValue& edgeValue : geometry.value(QStringLiteral("edges")).toArray()) {
      const QJsonObject expected = edgeValue.toObject();
      const QString edgeId = expected.value(QStringLiteral("id")).toString();
      const StatePlacementEdge* actual = findById(placed.edges, edgeId);
      if (!actual || actual->points.size() < 2)
        fail(id + QStringLiteral(": missing edge ") + edgeId);
      requireNear(actual->points.first() - origin,
                  point(expected.value(QStringLiteral("start")).toObject()), 1.0,
                  id + QLatin1Char('/') + edgeId + QStringLiteral(" start"));
      requireNear(actual->points.last() - origin,
                  point(expected.value(QStringLiteral("end")).toObject()), 1.0,
                  id + QLatin1Char('/') + edgeId + QStringLiteral(" end"));
      const QJsonValue expectedLabel = expected.value(QStringLiteral("labelCenter"));
      if (expectedLabel.isObject()) {
        if (!actual->labelPosition) fail(id + QLatin1Char('/') + edgeId + QStringLiteral(" label missing"));
        requireNear(*actual->labelPosition - origin, point(expectedLabel.toObject()), 0.02,
                    id + QLatin1Char('/') + edgeId + QStringLiteral(" label"));
      }
    }
  }
  return 0;
}
