// Verifies the native Dagre compound pipeline (layoutFlowchartNodesDagre) against
// the SAME committed geometry goldens as the flat pipeline, INCLUDING the
// compound-crossing case that the flat pipeline skips (pendingNative). This test
// is the milestone-C acceptance gate.

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSizeF>

#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid::flowchart;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
void requirePathNear(const QString& actual, const QString& expected, const QString& context) {
  static const QRegularExpression numberPattern(QStringLiteral("-?\\d+(?:\\.\\d+)?(?:e[-+]?\\d+)?"),
                                                QRegularExpression::CaseInsensitiveOption);
  const QString actualStructure = QString(actual).replace(numberPattern, QStringLiteral("#"));
  const QString expectedStructure = QString(expected).replace(numberPattern, QStringLiteral("#"));
  require(actualStructure == expectedStructure,
          context + QStringLiteral(" path command mismatch:\nnative:   %1\nupstream: %2").arg(actual, expected));
  auto actualMatch = numberPattern.globalMatch(actual);
  auto expectedMatch = numberPattern.globalMatch(expected);
  while (actualMatch.hasNext() && expectedMatch.hasNext()) {
    const qreal actualValue = actualMatch.next().captured().toDouble();
    const qreal expectedValue = expectedMatch.next().captured().toDouble();
    // 0.002 tolerance on 0.001-serialised values; +1e-9 absorbs the float
    // representation of an exact 0.002 diff (e.g. 403.301 - 403.299) so a value
    // at the tolerance boundary is accepted. A real 0.0021 algorithm diff still
    // fails.
    require(std::abs(actualValue - expectedValue) <= 0.002 + 1e-9,
            context + QStringLiteral(" path coordinate mismatch: native=%1 upstream=%2")
                          .arg(actualValue).arg(expectedValue));
  }
  require(!actualMatch.hasNext() && !expectedMatch.hasNext(), context + QStringLiteral(" path coordinate count mismatch"));
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected flowchart geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open flowchart geometry fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Flowchart geometry fixture version drifted"));
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const Flowchart chart = Flowchart::parse(fixture.value(QStringLiteral("source")).toString());
    const QJsonArray expectedNodes = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("nodes")).toArray();
    QMap<QString, QSizeF> sizes;
    for (const QJsonValue& nodeValue : expectedNodes) {
      const QJsonObject node = nodeValue.toObject();
      sizes.insert(node.value(QStringLiteral("id")).toString(),
                   QSizeF(node.value(QStringLiteral("width")).toDouble(), node.value(QStringLiteral("height")).toDouble()));
    }
    FlowLayoutOptions layoutOptions;
    layoutOptions.curve = fixture.value(QStringLiteral("curve")).toString();
    const QJsonArray expectedEdges = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("edges")).toArray();
    for (const QJsonValue& edgeValue : expectedEdges) {
      const QJsonObject edge = edgeValue.toObject();
      const QJsonObject label = edge.value(QStringLiteral("label")).toObject();
      if (!label.isEmpty()) {
        layoutOptions.measuredEdgeLabels.insert(
            edge.value(QStringLiteral("id")).toString(),
            QSizeF(label.value(QStringLiteral("width")).toDouble(),
                   label.value(QStringLiteral("height")).toDouble()));
      }
    }
    const FlowLayoutResult actual = layoutFlowchartNodesDagre(chart.data(), sizes, layoutOptions);
    require(actual.nodes.size() == expectedNodes.size(),
            QStringLiteral("Dagre %1 node count mismatch native=%2 upstream=%3")
                .arg(id).arg(actual.nodes.size()).arg(expectedNodes.size()));
    for (qsizetype i = 0; i < actual.nodes.size(); ++i) {
      const FlowLayoutNode& node = actual.nodes.at(i);
      const QJsonObject expected = expectedNodes.at(i).toObject();
      require(node.id == expected.value(QStringLiteral("id")).toString(),
              QStringLiteral("Dagre %1/%2 order mismatch native=%3 upstream=%4")
                  .arg(id, node.id, node.id, expected.value(QStringLiteral("id")).toString()));
      require(std::abs(node.x - expected.value(QStringLiteral("dx")).toDouble()) <= 0.002,
              QStringLiteral("Dagre %1/%2 x mismatch: native=%3 upstream=%4")
                  .arg(id, node.id).arg(node.x).arg(expected.value(QStringLiteral("dx")).toDouble()));
      require(std::abs(node.y - expected.value(QStringLiteral("dy")).toDouble()) <= 0.002,
              QStringLiteral("Dagre %1/%2 y mismatch: native=%3 upstream=%4")
                  .arg(id, node.id).arg(node.y).arg(expected.value(QStringLiteral("dy")).toDouble()));
    }
    require(actual.edges.size() == expectedEdges.size(),
            QStringLiteral("Dagre %1 edge count mismatch native=%2 upstream=%3")
                .arg(id).arg(actual.edges.size()).arg(expectedEdges.size()));
    for (qsizetype i = 0; i < actual.edges.size(); ++i) {
      const QJsonObject expected = expectedEdges.at(i).toObject();
      require(actual.edges.at(i).id == expected.value(QStringLiteral("id")).toString(),
              QStringLiteral("Dagre %1 edge id mismatch").arg(id));
      requirePathNear(actual.edges.at(i).path, expected.value(QStringLiteral("d")).toString(),
                      QStringLiteral("Dagre %1/%2").arg(id, actual.edges.at(i).id));
      const QJsonObject expectedLabel = expected.value(QStringLiteral("label")).toObject();
      if (expectedLabel.value(QStringLiteral("width")).toDouble() > 0.0) {
        require(actual.edges.at(i).hasLabelPosition &&
                    std::abs(actual.edges.at(i).labelX - expectedLabel.value(QStringLiteral("dx")).toDouble()) <= 0.002 &&
                    std::abs(actual.edges.at(i).labelY - expectedLabel.value(QStringLiteral("dy")).toDouble()) <= 0.002,
                QStringLiteral("Dagre %1/%2 label position mismatch: native=%3,%4 upstream=%5,%6")
                    .arg(id, actual.edges.at(i).id)
                    .arg(actual.edges.at(i).labelX).arg(actual.edges.at(i).labelY)
                    .arg(expectedLabel.value(QStringLiteral("dx")).toDouble())
                    .arg(expectedLabel.value(QStringLiteral("dy")).toDouble()));
      }
    }
    const QJsonArray expectedClusters = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("clusters")).toArray();
    require(actual.clusters.size() == expectedClusters.size(),
            QStringLiteral("Dagre %1 cluster count mismatch native=%2 upstream=%3")
                .arg(id).arg(actual.clusters.size()).arg(expectedClusters.size()));
    for (qsizetype i = 0; i < actual.clusters.size(); ++i) {
      const FlowLayoutCluster& cluster = actual.clusters.at(i);
      const QJsonObject expected = expectedClusters.at(i).toObject();
      require(cluster.id == expected.value(QStringLiteral("id")).toString(),
              QStringLiteral("Dagre %1 cluster id mismatch native=%2 upstream=%3")
                  .arg(id, cluster.id, expected.value(QStringLiteral("id")).toString()));
      require(std::abs(cluster.x - expected.value(QStringLiteral("dx")).toDouble()) <= 0.002 &&
                  std::abs(cluster.y - expected.value(QStringLiteral("dy")).toDouble()) <= 0.002 &&
                  std::abs(cluster.width - expected.value(QStringLiteral("width")).toDouble()) <= 0.002 &&
                  std::abs(cluster.height - expected.value(QStringLiteral("height")).toDouble()) <= 0.002,
              QStringLiteral("Dagre %1/%2 cluster bounds mismatch: native=%3,%4 %5x%6 upstream=%7,%8 %9x%10")
                  .arg(id, cluster.id).arg(cluster.x).arg(cluster.y).arg(cluster.width).arg(cluster.height)
                  .arg(expected.value(QStringLiteral("dx")).toDouble())
                  .arg(expected.value(QStringLiteral("dy")).toDouble())
                  .arg(expected.value(QStringLiteral("width")).toDouble())
                  .arg(expected.value(QStringLiteral("height")).toDouble()));
    }
  }
  return 0;
}
