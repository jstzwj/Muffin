#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/flowchart/FlowchartShapes.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>

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
    // representation of an exact 0.002 diff so a boundary value is accepted.
    require(std::abs(actualValue - expectedValue) <= 0.002 + 1e-9,
            context + QStringLiteral(" path coordinate mismatch: native=%1 upstream=%2")
                          .arg(actualValue).arg(expectedValue));
  }
  require(!actualMatch.hasNext() && !expectedMatch.hasNext(), context + QStringLiteral(" path coordinate count mismatch"));
}
}

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
    const FlowLayoutResult actual = layoutFlowchartNodes(chart.data(), sizes, layoutOptions);
    require(actual.nodes.size() == expectedNodes.size(), QStringLiteral("Flowchart geometry %1 node count mismatch").arg(id));
    for (qsizetype i = 0; i < actual.nodes.size(); ++i) {
      const FlowLayoutNode& node = actual.nodes.at(i);
      const QJsonObject expected = expectedNodes.at(i).toObject();
      require(node.id == expected.value(QStringLiteral("id")).toString(), QStringLiteral("Flowchart geometry %1 order mismatch").arg(id));
      require(std::abs(node.x - expected.value(QStringLiteral("dx")).toDouble()) <= 0.002,
              QStringLiteral("Flowchart geometry %1/%2 x mismatch: native=%3 upstream=%4")
                  .arg(id, node.id).arg(node.x).arg(expected.value(QStringLiteral("dx")).toDouble()));
      require(std::abs(node.y - expected.value(QStringLiteral("dy")).toDouble()) <= 0.002,
              QStringLiteral("Flowchart geometry %1/%2 y mismatch: native=%3 upstream=%4")
                  .arg(id, node.id).arg(node.y).arg(expected.value(QStringLiteral("dy")).toDouble()));
    }
    require(actual.edges.size() == expectedEdges.size(), QStringLiteral("Flowchart geometry %1 edge count mismatch").arg(id));
    for (qsizetype i = 0; i < actual.edges.size(); ++i) {
      const QJsonObject expected = expectedEdges.at(i).toObject();
      require(actual.edges.at(i).id == expected.value(QStringLiteral("id")).toString(),
              QStringLiteral("Flowchart geometry %1 edge order mismatch").arg(id));
      requirePathNear(actual.edges.at(i).path, expected.value(QStringLiteral("d")).toString(),
                      QStringLiteral("Flowchart geometry %1/%2").arg(id, actual.edges.at(i).id));
      const QJsonObject expectedLabel = expected.value(QStringLiteral("label")).toObject();
      if (expectedLabel.value(QStringLiteral("width")).toDouble() > 0.0) {
        require(actual.edges.at(i).hasLabelPosition &&
                    std::abs(actual.edges.at(i).labelX - expectedLabel.value(QStringLiteral("dx")).toDouble()) <= 0.002 &&
                    std::abs(actual.edges.at(i).labelY - expectedLabel.value(QStringLiteral("dy")).toDouble()) <= 0.002,
                QStringLiteral("Flowchart geometry %1/%2 label position mismatch: native=%3,%4 upstream=%5,%6")
                    .arg(id, actual.edges.at(i).id)
                    .arg(actual.edges.at(i).labelX).arg(actual.edges.at(i).labelY)
                    .arg(expectedLabel.value(QStringLiteral("dx")).toDouble())
                    .arg(expectedLabel.value(QStringLiteral("dy")).toDouble()));
      }
    }
    const QJsonArray expectedClusters = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("clusters")).toArray();
    require(actual.clusters.size() == expectedClusters.size(), QStringLiteral("Flowchart geometry %1 cluster count mismatch").arg(id));
    for (qsizetype i = 0; i < actual.clusters.size(); ++i) {
      const FlowLayoutCluster& cluster = actual.clusters.at(i);
      const QJsonObject expected = expectedClusters.at(i).toObject();
      require(cluster.id == expected.value(QStringLiteral("id")).toString(), QStringLiteral("Flowchart geometry %1 cluster id mismatch").arg(id));
      require(std::abs(cluster.x - expected.value(QStringLiteral("dx")).toDouble()) <= 0.002 &&
                  std::abs(cluster.y - expected.value(QStringLiteral("dy")).toDouble()) <= 0.002 &&
                  std::abs(cluster.width - expected.value(QStringLiteral("width")).toDouble()) <= 0.002 &&
                  std::abs(cluster.height - expected.value(QStringLiteral("height")).toDouble()) <= 0.002,
              QStringLiteral("Flowchart geometry %1/%2 cluster bounds mismatch: native=%3,%4 %5x%6 upstream=%7,%8 %9x%10")
                  .arg(id, cluster.id).arg(cluster.x).arg(cluster.y).arg(cluster.width).arg(cluster.height)
                  .arg(expected.value(QStringLiteral("dx")).toDouble())
                  .arg(expected.value(QStringLiteral("dy")).toDouble())
                  .arg(expected.value(QStringLiteral("width")).toDouble())
                  .arg(expected.value(QStringLiteral("height")).toDouble()));
    }
    const QMap<QString, QSizeF> nativeSizes = measureFlowchartNodes(chart.data());
    for (const QJsonValue& nodeValue : expectedNodes) {
      const QJsonObject expected = nodeValue.toObject();
      const QString nodeId = expected.value(QStringLiteral("id")).toString();
      const QSizeF native = nativeSizes.value(nodeId);
      require(std::abs(native.width() - expected.value(QStringLiteral("width")).toDouble()) <= 0.2 &&
                  std::abs(native.height() - expected.value(QStringLiteral("height")).toDouble()) <= 0.2,
              QStringLiteral("Flowchart native text %1/%2 size mismatch: native=%3x%4 upstream=%5x%6")
                  .arg(id, nodeId).arg(native.width()).arg(native.height())
                  .arg(expected.value(QStringLiteral("width")).toDouble())
                  .arg(expected.value(QStringLiteral("height")).toDouble()));
    }
    if (id == QLatin1String("basic-shapes") || id == QLatin1String("legacy-shapes") ||
        id == QLatin1String("expanded-shapes") || id == QLatin1String("expanded-shapes-2")) {
      for (qsizetype i = 0; i < chart.data().vertices.size(); ++i) {
        const FlowVertex& vertex = chart.data().vertices.at(i);
        const QJsonObject expected = expectedNodes.at(i).toObject();
        const QJsonObject shape = expected.value(QStringLiteral("shape")).toObject();
        const QJsonObject attributes = shape.value(QStringLiteral("attributes")).toObject();
        const FlowShapeGeometry native = flowShapeGeometry(
            vertex, QSizeF(expected.value(QStringLiteral("width")).toDouble(),
                           expected.value(QStringLiteral("height")).toDouble()));
        require(std::abs(native.bounds.width() - expected.value(QStringLiteral("width")).toDouble()) <= 0.002 &&
                    std::abs(native.bounds.height() - expected.value(QStringLiteral("height")).toDouble()) <= 0.002,
                QStringLiteral("Flowchart shape %1 semantic bounds mismatch").arg(vertex.id));
        if (id == QLatin1String("basic-shapes") &&
            shape.value(QStringLiteral("tag")).toString() == QLatin1String("rect")) {
          require(native.kind == (attributes.contains(QStringLiteral("rx"))
                                      ? QLatin1String("roundedRect") : QLatin1String("rect")),
                  QStringLiteral("Flowchart shape %1 kind mismatch").arg(vertex.id));
          require(std::abs(native.bounds.x() - attributes.value(QStringLiteral("x")).toString().toDouble()) <= 0.002 &&
                      std::abs(native.bounds.y() - attributes.value(QStringLiteral("y")).toString().toDouble()) <= 0.002 &&
                      std::abs(native.bounds.width() - attributes.value(QStringLiteral("width")).toString().toDouble()) <= 0.002 &&
                      std::abs(native.bounds.height() - attributes.value(QStringLiteral("height")).toString().toDouble()) <= 0.002,
                  QStringLiteral("Flowchart shape %1 rect mismatch").arg(vertex.id));
          if (attributes.contains(QStringLiteral("rx"))) {
            require(std::abs(native.cornerRadius - attributes.value(QStringLiteral("rx")).toString().toDouble()) <= 0.002,
                    QStringLiteral("Flowchart shape %1 radius mismatch").arg(vertex.id));
          }
        } else if (id == QLatin1String("basic-shapes") &&
                   shape.value(QStringLiteral("tag")).toString() == QLatin1String("circle")) {
          require(native.kind == QLatin1String("ellipse") &&
                      std::abs(native.bounds.width() / 2.0 - attributes.value(QStringLiteral("r")).toString().toDouble()) <= 0.002,
                  QStringLiteral("Flowchart shape %1 circle mismatch").arg(vertex.id));
        } else if (id == QLatin1String("basic-shapes") &&
                   shape.value(QStringLiteral("tag")).toString() == QLatin1String("polygon")) {
          require(native.kind == QLatin1String("polygon") && native.points.size() == 4,
                  QStringLiteral("Flowchart shape %1 polygon mismatch").arg(vertex.id));
          for (const QPointF& point : native.points) {
            require(std::abs(std::abs(point.x()) + std::abs(point.y()) - native.bounds.width() / 2.0) <= 0.002,
                    QStringLiteral("Flowchart shape %1 diamond point mismatch").arg(vertex.id));
          }
        }
        const QJsonObject pixel = expected.value(QStringLiteral("pixel")).toObject();
        // Expanded shapes whose handler draws the background via rc.path/rc.circle
        // directly (class "outer-path" or no label-container) have no captured
        // silhouette — skip them. Their geometry bounds are still verified above.
        if (pixel.value(QStringLiteral("png")).toString().isEmpty()) continue;
        QImage upstream;
        require(upstream.loadFromData(QByteArray::fromBase64(pixel.value(QStringLiteral("png")).toString().toLatin1()), "PNG"),
                QStringLiteral("Flowchart shape %1 pixel golden could not be decoded").arg(vertex.id));
        QImage rendered(upstream.size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.translate(2.0 - native.bounds.left(), 2.0 - native.bounds.top());
        if (native.kind == QLatin1String("roundedRect") || native.kind == QLatin1String("stadium")) {
          painter.drawRoundedRect(native.bounds, native.cornerRadius, native.cornerRadius, Qt::AbsoluteSize);
        } else if (native.kind == QLatin1String("ellipse")) {
          painter.drawEllipse(native.bounds);
        } else if (native.kind == QLatin1String("cylinder")) {
          const QRectF topEllipse(native.bounds.left(), native.bounds.top(),
                                  native.bounds.width(), native.radiusY * 2.0);
          const QRectF bottomEllipse(native.bounds.left(), native.bounds.bottom() - native.radiusY * 2.0,
                                     native.bounds.width(), native.radiusY * 2.0);
          QPainterPath path;
          path.moveTo(native.bounds.left(), native.bounds.top() + native.radiusY);
          path.arcTo(topEllipse, 180.0, -180.0);
          path.lineTo(native.bounds.right(), native.bounds.bottom() - native.radiusY);
          path.arcTo(bottomEllipse, 0.0, -180.0);
          path.closeSubpath();
          painter.drawPath(path);
        } else if (native.kind == QLatin1String("horizontalCylinder")) {
          // tiltedCylinder: sampled two-subpath path (arcs in SVG traversal order)
          // with WindingFill, built by the shared helper.
          painter.drawPath(flowShapeHorizontalCylinderPath(native.bounds, native.radiusX, native.radiusY));
        } else if (native.kind == QLatin1String("polygon")) {
          painter.drawPolygon(QPolygonF(native.points));
        } else {
          painter.drawRect(native.bounds);
        }
        painter.end();
        qint64 alphaDifference = 0;
        qint64 severePixels = 0;
        const qint64 pixelCount = upstream.width() * upstream.height();
        for (int y = 0; y < upstream.height(); ++y) {
          for (int x = 0; x < upstream.width(); ++x) {
            const int difference = std::abs(qAlpha(upstream.pixel(x, y)) - qAlpha(rendered.pixel(x, y)));
            alphaDifference += difference;
            if (difference > 64) ++severePixels;
          }
        }
        const qreal meanAlphaDifference = static_cast<qreal>(alphaDifference) / pixelCount;
        const qreal severeRatio = static_cast<qreal>(severePixels) / pixelCount;
        require(meanAlphaDifference <= 12.0 && severeRatio <= 0.08,
                QStringLiteral("Flowchart shape %1 pixel mismatch: mean alpha=%2 severe ratio=%3")
                    .arg(vertex.id).arg(meanAlphaDifference).arg(severeRatio));
      }
    }
  }
  return 0;
}
