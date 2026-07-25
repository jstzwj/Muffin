#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/FlowTheme.h"

#include <QColor>
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
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid::flowchart;

namespace {
[[noreturn]] void fail(const QString& message) {
  // Write to stderr explicitly so ctest captures the message on every
  // platform (Qt's qCritical routes to OutputDebugString on Windows, which
  // --output-on-failure does not capture, leaving failures undiagnosed).
  std::fprintf(stderr, "%s\n", message.toUtf8().constData());
  std::fflush(stderr);
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
qreal cssNumber(const QString& value) {
  static const QRegularExpression pattern(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
  const auto match = pattern.match(value);
  return match.hasMatch() ? match.captured().toDouble() : 0.0;
}
bool sameColor(const QString& native, const QString& upstream) {
  if (native.isEmpty() || upstream.isEmpty() || upstream == QLatin1String("none"))
    return native.isEmpty() || upstream == QLatin1String("none");
  return muffin::mermaid::color::toQColor(native) ==
         muffin::mermaid::color::toQColor(upstream);
}
QString normalizedDash(QString value) {
  value = value.trimmed();
  if (value == QLatin1String("none")) return {};
  const QStringList parts = value.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                        Qt::SkipEmptyParts);
  if (!parts.isEmpty() &&
      std::all_of(parts.cbegin(), parts.cend(),
                  [](const QString& part) { return qFuzzyIsNull(cssNumber(part)); }))
    return {};
  QStringList numbers;
  for (const QString& part : parts) numbers.push_back(QString::number(cssNumber(part), 'g', 12));
  return numbers.join(QLatin1Char(' '));
}
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
QVector<bool> collapsedDirections(const QVector<FlowLabelVisualRun>& runs) {
  QVector<bool> result;
  const FlowLabelVisualRun* previous = nullptr;
  for (const auto& run : runs) {
    if (run.math) continue;
    const bool contiguous = previous &&
        (previous->start + previous->length == run.start ||
         run.start + run.length == previous->start);
    if (result.isEmpty() || result.last() != run.rightToLeft || !contiguous)
      result.push_back(run.rightToLeft);
    previous = &run;
  }
  return result;
}
QVector<bool> collapsedDirections(const QJsonArray& runs) {
  QVector<bool> result;
  qsizetype previousStart = -1;
  qsizetype previousLength = 0;
  for (const QJsonValue& value : runs) {
    const QJsonObject run = value.toObject();
    if (run.value(QStringLiteral("math")).toBool()) continue;
    const bool rtl = run.value(QStringLiteral("rtl")).toBool();
    const qsizetype start = run.value(QStringLiteral("start")).toInteger();
    const qsizetype length = run.value(QStringLiteral("length")).toInteger();
    const bool contiguous = previousStart >= 0 &&
        (previousStart + previousLength == start || start + length == previousStart);
    if (result.isEmpty() || result.last() != rtl || !contiguous) result.push_back(rtl);
    previousStart = start;
    previousLength = length;
  }
  return result;
}
void requireLabelLayoutNear(const FlowLabelLayoutMetrics& native,
                            const QJsonArray& expected, const QString& context,
                            qreal horizontalTolerance = 0.2) {
  require(native.lines.size() == expected.size(),
          context + QStringLiteral(" line count mismatch: native=%1 upstream=%2")
                        .arg(native.lines.size()).arg(expected.size()));
  for (qsizetype index = 0; index < native.lines.size(); ++index) {
    const auto& line = native.lines.at(index);
    const QJsonObject upstream = expected.at(index).toObject();
    QStringList nativeFonts;
    for (const auto& run : line.runs)
      nativeFonts.push_back(QStringLiteral("%1:%2").arg(run.fontFamily,
                                                         run.rightToLeft ? QStringLiteral("rtl")
                                                                         : QStringLiteral("ltr")));
    require(std::abs(line.width - upstream.value(QStringLiteral("width")).toDouble()) <=
                    horizontalTolerance &&
                std::abs(line.height - upstream.value(QStringLiteral("height")).toDouble()) <= 2.0,
            context + QStringLiteral(" line %1 box mismatch: native=%2x%3 upstream=%4x%5 fonts=%6")
                          .arg(index).arg(line.width).arg(line.height)
                          .arg(upstream.value(QStringLiteral("width")).toDouble())
                          .arg(upstream.value(QStringLiteral("height")).toDouble())
                          .arg(nativeFonts.join(QLatin1Char(','))));
    if (!upstream.value(QStringLiteral("baseline")).isNull()) {
      require(std::abs(line.baseline - upstream.value(QStringLiteral("baseline")).toDouble()) <= 2.0 &&
                  std::abs(line.ascent - upstream.value(QStringLiteral("ascent")).toDouble()) <= 2.0 &&
                  std::abs(line.descent - upstream.value(QStringLiteral("descent")).toDouble()) <= 2.0,
              context + QStringLiteral(" line %1 metrics mismatch: native baseline/ascent/descent=%2/%3/%4 upstream=%5/%6/%7")
                            .arg(index).arg(line.baseline).arg(line.ascent).arg(line.descent)
                            .arg(upstream.value(QStringLiteral("baseline")).toDouble())
                            .arg(upstream.value(QStringLiteral("ascent")).toDouble())
                            .arg(upstream.value(QStringLiteral("descent")).toDouble()));
    }
    const QVector<bool> nativeDirections = collapsedDirections(line.runs);
    const QVector<bool> upstreamDirections =
        collapsedDirections(upstream.value(QStringLiteral("runs")).toArray());
    auto directionText = [](const QVector<bool>& directions) {
      QStringList result;
      for (bool rtl : directions) result.push_back(rtl ? QStringLiteral("rtl")
                                                       : QStringLiteral("ltr"));
      return result.join(QLatin1Char(','));
    };
    const bool nativeHasRtl = nativeDirections.contains(true);
    const bool upstreamHasRtl = upstreamDirections.contains(true);
    require(nativeHasRtl == upstreamHasRtl &&
                (upstreamDirections.size() <= 1 || nativeDirections.size() > 1),
            context + QStringLiteral(" line %1 visual bidi coverage mismatch: native=%2 upstream=%3")
                          .arg(index).arg(directionText(nativeDirections),
                                          directionText(upstreamDirections)));
  }
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected flowchart geometry fixture path"));
  std::fprintf(stderr, "[diag] stage 1: app + argc ok\n"); std::fflush(stderr);

  using muffin::mermaid::flowchart::d3curve::generateRoundedPath;
  require(generateRoundedPath({}).isEmpty() &&
              generateRoundedPath({QPointF(0, 0), QPointF(10, 0)}) ==
                  QLatin1String("M0,0L10,0"),
          QStringLiteral("Rounded path endpoint contract drifted"));
  require(generateRoundedPath(
              {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)}) ==
              QLatin1String("M0,0L5,0Q10,0 10,5L10,10"),
          QStringLiteral("Rounded path short-segment clamp drifted"));
  require(generateRoundedPath({QPointF(0, 0), QPointF(10, 0),
                               QPointF(10, 0), QPointF(20, 0)}) ==
              QLatin1String("M0,0L10,0L10,0L20,0") &&
              generateRoundedPath({QPointF(0, 0), QPointF(10, 0),
                                   QPointF(0, 0)}) ==
                  QLatin1String("M0,0L10,0L0,0"),
          QStringLiteral("Rounded path degenerate-point contract drifted"));
  std::fprintf(stderr, "[diag] stage 2: d3curves ok\n"); std::fflush(stderr);

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open flowchart geometry fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Flowchart geometry fixture version drifted"));
  const QJsonObject fontContract = root.value(QStringLiteral("font")).toObject();
  require(fontContract.value(QStringLiteral("mathFamily")).toString() ==
                  QLatin1String("STIX Two Math") &&
              fontContract.value(QStringLiteral("mathSource")).toString() ==
                  QLatin1String("third_party/stix"),
          QStringLiteral("Flowchart geometry Math font contract drifted"));
  bool sawFormattedTextBlock = false;
  std::fprintf(stderr, "[diag] stage 3: fixture loaded, entering cases\n"); std::fflush(stderr);
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    std::fprintf(stderr, "[diag] case: %s\n", id.toUtf8().constData()); std::fflush(stderr);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const Flowchart chart = Flowchart::parse(source);
    // This legacy geometry fixture intentionally uses Arial plus the host
    // fallback chain. DirectWrite and Qt may select different CJK/bidi faces;
    // the bundled-Noto label oracle remains the strict 0.25px contract.
    const bool systemFallbackCase = id.contains(QLatin1String("cjk")) ||
                                    id.contains(QLatin1String("bidi"));
    // Host-font slack: this legacy fixture was captured on x86 Windows with
    // real Arial. Other platforms substitute the face (Linux has no Arial) or
    // vary its metrics via a different renderer (DirectWrite on ARM, CoreText
    // on macOS), so text-derived sizes drift several px. The strict bundled-
    // font oracle (MuffinMermaidFlowchartLabelOracleTest) remains the tight
    // fidelity gate; geometry coordinates (0.002) and SVG structure are still
    // exact here. Linux CI also installs fonts-liberation (Arial-metric) so
    // its drift is sub-px, but the slack covers renderer variance everywhere.
    const qreal textTolerance = systemFallbackCase ? 2.0 : 6.0;
    const qreal semanticShapeTolerance = 6.0;
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
    const auto theme = muffin::mermaid::flowtheme::resolveFlowTheme(
        muffin::mermaid::flowtheme::FlowThemeId::Default);
    const auto scene = muffin::mermaid::flowscene::buildFlowScene(chart.data(), actual, theme);
    const QJsonObject expectedSvg = fixture.value(QStringLiteral("expected")).toObject()
                                        .value(QStringLiteral("svg")).toObject();
    require(expectedSvg.value(QStringLiteral("class")).toString() == QLatin1String("flowchart") &&
                expectedSvg.value(QStringLiteral("xmlns")).toString() == QLatin1String("http://www.w3.org/2000/svg") &&
                !expectedSvg.value(QStringLiteral("viewBox")).toString().isEmpty() &&
                expectedSvg.value(QStringLiteral("role")).toString() == QLatin1String("graphics-document document") &&
                expectedSvg.value(QStringLiteral("aria-roledescription")).toString() == QLatin1String("flowchart-v2"),
            QStringLiteral("Flowchart SVG root structure %1 mismatch").arg(id));
    if (qEnvironmentVariable("MUFFIN_GEOMETRY_DEBUG") == id) {
      for (const FlowLayoutNode& node : actual.nodes)
        qDebug().noquote() << "node" << node.id << node.x << node.y << node.width << node.height;
      for (const FlowLayoutCluster& cluster : actual.clusters)
        qDebug().noquote() << "cluster" << cluster.id << cluster.x << cluster.y
                           << cluster.width << cluster.height;
    }
    require(actual.nodes.size() == expectedNodes.size(), QStringLiteral("Flowchart geometry %1 node count mismatch").arg(id));
    for (qsizetype i = 0; i < actual.nodes.size(); ++i) {
      const FlowLayoutNode& node = actual.nodes.at(i);
      const QJsonObject expected = expectedNodes.at(i).toObject();
      require(node.id == expected.value(QStringLiteral("id")).toString(),
              QStringLiteral("Flowchart geometry %1 order mismatch at %2: native=%3 upstream=%4")
                  .arg(id).arg(i).arg(node.id,
                      expected.value(QStringLiteral("id")).toString()));
      const QJsonObject groupAttributes = expected.value(QStringLiteral("group")).toObject();
      const QJsonObject labelAttributes = expected.value(QStringLiteral("label")).toObject()
                                              .value(QStringLiteral("attributes")).toObject();
      const QJsonObject shapeObject = expected.value(QStringLiteral("shape")).toObject();
      const bool shapeContainerValid = shapeObject.value(QStringLiteral("tag")).toString().isEmpty() ||
          shapeObject.value(QStringLiteral("attributes")).toObject()
              .value(QStringLiteral("class")).toString().contains(QStringLiteral("label-container"));
      const bool labelContainerValid = expected.value(QStringLiteral("label")).toObject()
                                               .value(QStringLiteral("tag")).toString().isEmpty() ||
          labelAttributes.value(QStringLiteral("class")).toString()
              .split(QLatin1Char(' ')).contains(QStringLiteral("label"));
      require(groupAttributes.value(QStringLiteral("class")).toString().split(QLatin1Char(' ')).contains(QStringLiteral("node")) &&
                  labelContainerValid &&
                  shapeContainerValid,
              QStringLiteral("Flowchart SVG node structure %1/%2 mismatch").arg(id, node.id));
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
      const QJsonObject attributes = expected.value(QStringLiteral("attributes")).toObject();
      const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
      const auto& sceneEdge = scene.edges.at(i);
      const QString edgeClass = attributes.value(QStringLiteral("class")).toString();
      require(edgeClass.contains(QStringLiteral("flowchart-link")) ||
                  edgeClass.contains(QStringLiteral("edge-thickness-invisible")),
              QStringLiteral("Flowchart SVG edge class %1/%2 mismatch").arg(id, sceneEdge.id));
      require(attributes.value(QStringLiteral("data-edge")).toString() == QLatin1String("true") &&
                  attributes.value(QStringLiteral("data-id")).toString() == sceneEdge.id &&
                  attributes.value(QStringLiteral("data-look")).toString() == QLatin1String("classic") &&
                  !attributes.value(QStringLiteral("d")).toString().isEmpty(),
              QStringLiteral("Flowchart SVG edge structural attributes %1/%2 mismatch")
                  .arg(id, sceneEdge.id));
      require(attributes.value(QStringLiteral("marker-end")).toString() == sceneEdge.markerEnd &&
                  attributes.value(QStringLiteral("marker-start")).toString() == sceneEdge.markerStart,
              QStringLiteral("Flowchart SVG edge markers %1/%2 mismatch: native=%3/%4 upstream=%5/%6")
                  .arg(id, sceneEdge.id, sceneEdge.markerStart, sceneEdge.markerEnd,
                       attributes.value(QStringLiteral("marker-start")).toString(),
                       attributes.value(QStringLiteral("marker-end")).toString()));
      require(sameColor(sceneEdge.stroke, computed.value(QStringLiteral("stroke")).toString()) &&
                  std::abs(cssNumber(sceneEdge.strokeWidth) -
                           cssNumber(computed.value(QStringLiteral("stroke-width")).toString())) <= 0.01 &&
                  normalizedDash(sceneEdge.strokeDasharray) ==
                      normalizedDash(computed.value(QStringLiteral("stroke-dasharray")).toString()),
              QStringLiteral("Flowchart SVG edge computed style %1/%2 mismatch").arg(id, sceneEdge.id));
      const auto semanticEdge = std::find_if(chart.data().edges.cbegin(), chart.data().edges.cend(),
                                             [&](const FlowEdge& candidate) {
                                               return candidate.id == sceneEdge.id;
                                             });
      if (semanticEdge != chart.data().edges.cend() &&
          (semanticEdge->animate || !semanticEdge->animation.isEmpty()))
        require(computed.value(QStringLiteral("animation-name")).toString() != QLatin1String("none") &&
                    !computed.value(QStringLiteral("animation-duration")).toString().isEmpty(),
                QStringLiteral("Flowchart SVG edge animation %1/%2 mismatch").arg(id, sceneEdge.id));
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
    const QJsonArray markerDefinitions = fixture.value(QStringLiteral("expected")).toObject()
                                             .value(QStringLiteral("defs")).toObject()
                                             .value(QStringLiteral("markers")).toArray();
    auto requireMarkerDefinition = [&](const QString& markerId) {
      if (markerId.isEmpty()) return;
      const auto found = std::find_if(markerDefinitions.cbegin(), markerDefinitions.cend(),
                                      [&](const QJsonValue& value) {
        return value.toObject().value(QStringLiteral("attributes")).toObject()
                    .value(QStringLiteral("id")).toString() == markerId;
      });
      require(found != markerDefinitions.cend(),
              QStringLiteral("Flowchart SVG marker %1/%2 definition missing").arg(id, markerId));
      const QJsonObject marker = found->toObject();
      const QJsonObject markerAttributes = marker.value(QStringLiteral("attributes")).toObject();
      require(markerAttributes.value(QStringLiteral("orient")).toString() == QLatin1String("auto") &&
                  markerAttributes.value(QStringLiteral("markerUnits")).toString() == QLatin1String("userSpaceOnUse") &&
                  cssNumber(markerAttributes.value(QStringLiteral("markerWidth")).toString()) > 0.0 &&
                  cssNumber(markerAttributes.value(QStringLiteral("markerHeight")).toString()) > 0.0 &&
                  markerAttributes.contains(QStringLiteral("refX")) &&
                  markerAttributes.contains(QStringLiteral("refY")) &&
                  !marker.value(QStringLiteral("children")).toArray().isEmpty(),
              QStringLiteral("Flowchart SVG marker %1/%2 structural attributes mismatch")
                  .arg(id, markerId));
    };
    for (const auto& edge : scene.edges) {
      requireMarkerDefinition(edge.markerStart);
      requireMarkerDefinition(edge.markerEnd);
    }
    const QJsonArray expectedClusters = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("clusters")).toArray();
    require(actual.clusters.size() == expectedClusters.size(), QStringLiteral("Flowchart geometry %1 cluster count mismatch").arg(id));
    for (qsizetype i = 0; i < actual.clusters.size(); ++i) {
      const FlowLayoutCluster& cluster = actual.clusters.at(i);
      const QJsonObject expected = expectedClusters.at(i).toObject();
      require(cluster.id == expected.value(QStringLiteral("id")).toString(), QStringLiteral("Flowchart geometry %1 cluster id mismatch").arg(id));
      require(expected.value(QStringLiteral("group")).toObject()
                      .value(QStringLiteral("class")).toString().contains(QStringLiteral("cluster")),
              QStringLiteral("Flowchart SVG cluster class %1/%2 mismatch").arg(id, cluster.id));
      const QJsonObject rectComputed = expected.value(QStringLiteral("rectComputed")).toObject();
      const QJsonObject labelStructure = expected.value(QStringLiteral("label")).toObject()
                                             .value(QStringLiteral("structure")).toObject();
      require(labelStructure.value(QStringLiteral("tag")).toString() == QLatin1String("g") &&
                  labelStructure.value(QStringLiteral("class")).toString().contains(QStringLiteral("cluster-label")),
              QStringLiteral("Flowchart SVG cluster label structure %1/%2 mismatch")
                  .arg(id, cluster.id));
      const auto& sceneCluster = scene.clusters.at(i);
      require(sameColor(sceneCluster.fill, rectComputed.value(QStringLiteral("fill")).toString()) &&
                  sameColor(sceneCluster.stroke, rectComputed.value(QStringLiteral("stroke")).toString()) &&
                  std::abs(cssNumber(sceneCluster.strokeWidth) -
                           cssNumber(rectComputed.value(QStringLiteral("stroke-width")).toString())) <= 0.01,
              QStringLiteral("Flowchart SVG cluster computed style %1/%2 mismatch").arg(id, cluster.id));
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
      require(std::abs(native.width() - expected.value(QStringLiteral("width")).toDouble()) <= semanticShapeTolerance &&
                  std::abs(native.height() - expected.value(QStringLiteral("height")).toDouble()) <= semanticShapeTolerance,
              QStringLiteral("Flowchart native text %1/%2 size mismatch: native=%3x%4 upstream=%5x%6")
                  .arg(id, nodeId).arg(native.width()).arg(native.height())
                  .arg(expected.value(QStringLiteral("width")).toDouble())
                  .arg(expected.value(QStringLiteral("height")).toDouble()));
      const auto vertex = std::find_if(chart.data().vertices.cbegin(), chart.data().vertices.cend(),
                                       [&](const FlowVertex& candidate) {
                                         return candidate.id == nodeId;
                                       });
      if (vertex != chart.data().vertices.cend() &&
          expected.value(QStringLiteral("labelWidth")).toDouble() > 0.0) {
        const QSizeF label = measureLabel(vertex->text, vertex->labelType);
        require(std::abs(label.width() - expected.value(QStringLiteral("labelWidth")).toDouble()) <= textTolerance &&
                    std::abs(label.height() - expected.value(QStringLiteral("labelHeight")).toDouble()) <= textTolerance,
                QStringLiteral("Flowchart node label %1/%2 bbox mismatch: native=%3x%4 upstream=%5x%6")
                    .arg(id, nodeId).arg(label.width()).arg(label.height())
                    .arg(expected.value(QStringLiteral("labelWidth")).toDouble())
                    .arg(expected.value(QStringLiteral("labelHeight")).toDouble()));
        const QJsonArray expectedLines = expected.value(QStringLiteral("label")).toObject()
                                             .value(QStringLiteral("lines")).toArray();
        if (!expectedLines.isEmpty())
          requireLabelLayoutNear(layoutFlowLabel(parseFlowLabel(vertex->text, vertex->labelType),
                                                 QStringLiteral("Arial"), 16.0, 24.0),
                                 expectedLines,
                                 QStringLiteral("Flowchart node label %1/%2").arg(id, nodeId),
                                 textTolerance);
      }
      const auto sceneNode = std::find_if(scene.nodes.cbegin(), scene.nodes.cend(),
                                          [&](const muffin::mermaid::flowscene::FlowSceneNode& candidate) {
                                            return candidate.id == nodeId;
                                          });
      const QString shapeTag = expected.value(QStringLiteral("shape")).toObject()
                                   .value(QStringLiteral("tag")).toString();
      if (sceneNode != scene.nodes.cend() && !shapeTag.isEmpty() &&
          shapeTag != QLatin1String("g")) {
        const QJsonObject shapeComputed = expected.value(QStringLiteral("shape")).toObject()
                                              .value(QStringLiteral("computed")).toObject();
        require(sameColor(sceneNode->fill, shapeComputed.value(QStringLiteral("fill")).toString()) &&
                    sameColor(sceneNode->stroke, shapeComputed.value(QStringLiteral("stroke")).toString()) &&
                    std::abs(cssNumber(sceneNode->strokeWidth) -
                             cssNumber(shapeComputed.value(QStringLiteral("stroke-width")).toString())) <= 0.01,
                QStringLiteral("Flowchart SVG node computed style %1/%2 mismatch").arg(id, nodeId));
      }
    }
    for (qsizetype i = 0; i < chart.data().edges.size() && i < expectedEdges.size(); ++i) {
      const QJsonObject expectedLabel = expectedEdges.at(i).toObject()
                                            .value(QStringLiteral("label")).toObject();
      if (expectedLabel.value(QStringLiteral("width")).toDouble() <= 0.0) continue;
      const QSizeF label = measureFlowchartEdgeLabel(chart.data().edges.at(i));
      constexpr qreal serializedTextSlack = 0.005;
      require(std::abs(label.width() - expectedLabel.value(QStringLiteral("width")).toDouble()) <=
                      textTolerance + serializedTextSlack &&
                  std::abs(label.height() - expectedLabel.value(QStringLiteral("height")).toDouble()) <=
                      textTolerance + serializedTextSlack,
              QStringLiteral("Flowchart edge label %1/%2 bbox mismatch: native=%3x%4 upstream=%5x%6")
                  .arg(id, chart.data().edges.at(i).id).arg(label.width()).arg(label.height())
                  .arg(expectedLabel.value(QStringLiteral("width")).toDouble())
                  .arg(expectedLabel.value(QStringLiteral("height")).toDouble()));
      if (id == QLatin1String("edge-three-line-label")) {
        const qsizetype lineCount = expectedLabel.value(QStringLiteral("lines"))
                                        .toArray().size();
        require(lineCount == 5,
                QStringLiteral("Flowchart formatted SVG text fixture line count drifted"));
        require(std::abs(flowSvgFormattedTextBlockHeight(
                             QStringLiteral("Arial"), 16.0, lineCount) -
                         expectedLabel.value(QStringLiteral("height")).toDouble()) <=
                    0.005,
                QStringLiteral("Flowchart formatted SVG text block model drifted"));
        sawFormattedTextBlock = true;
      }
    }
    for (const FlowSubgraph& subgraph : chart.data().subgraphs) {
      const auto expected = std::find_if(expectedClusters.cbegin(), expectedClusters.cend(),
                                         [&](const QJsonValue& candidate) {
                                           return candidate.toObject().value(QStringLiteral("id")).toString() ==
                                                  subgraph.id;
                                         });
      if (expected == expectedClusters.cend()) continue;
      const QJsonObject expectedLabel = expected->toObject().value(QStringLiteral("label")).toObject();
      if (expectedLabel.isEmpty()) continue;
      const QSizeF label = measureFlowchartClusterLabel(subgraph);
      qreal expectedWidth = expectedLabel.value(QStringLiteral("width")).toDouble();
      qreal expectedHeight = expectedLabel.value(QStringLiteral("height")).toDouble();
      const QJsonArray expectedLines = expectedLabel.value(QStringLiteral("lines")).toArray();
      if (!expectedLines.isEmpty()) {
        expectedWidth = 0.0;
        expectedHeight = 0.0;
        for (const QJsonValue& line : expectedLines) {
          expectedWidth = std::max(expectedWidth,
                                   line.toObject().value(QStringLiteral("width")).toDouble());
          expectedHeight += line.toObject().value(QStringLiteral("height")).toDouble();
        }
      }
      constexpr qreal serializedTextSlack = 0.005;
      require(std::abs(label.width() - expectedWidth) <= textTolerance + serializedTextSlack &&
                  std::abs(label.height() - expectedHeight) <= textTolerance + serializedTextSlack,
              QStringLiteral("Flowchart cluster label %1/%2 bbox mismatch: native=%3x%4 upstream=%5x%6")
                  .arg(id, subgraph.id).arg(label.width()).arg(label.height())
                  .arg(expectedWidth).arg(expectedHeight));
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
  require(sawFormattedTextBlock,
          QStringLiteral("Flowchart formatted SVG text block fixture missing"));
  return 0;
}
