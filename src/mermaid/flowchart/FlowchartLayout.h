#pragma once

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QMap>
#include <QPointF>
#include <QSizeF>

#include <vector>

namespace muffin::mermaid::dagre {
struct DagreSnapshot;  // defined in mermaid/dagre/Layout.h
}

namespace muffin::mermaid::flowchart {

enum class FlowLook { Classic, Neo, HandDrawn };

inline FlowLook parseFlowLook(const QString& value) {
  if (value.compare(QStringLiteral("neo"), Qt::CaseInsensitive) == 0) return FlowLook::Neo;
  if (value.compare(QStringLiteral("handDrawn"), Qt::CaseInsensitive) == 0)
    return FlowLook::HandDrawn;
  return FlowLook::Classic;
}

inline QString flowLookName(FlowLook look) {
  if (look == FlowLook::Neo) return QStringLiteral("neo");
  if (look == FlowLook::HandDrawn) return QStringLiteral("handDrawn");
  return QStringLiteral("classic");
}

struct FlowLayoutNode {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  // Some renderers (notably hand-drawn Swimlane) lay out by the generated
  // SVG group's getBBox while retaining the pre-RoughJS shape dimensions for
  // painting. Zero means the rendered dimensions equal width/height.
  qreal renderWidth = 0.0;
  qreal renderHeight = 0.0;
  int rank = 0;
};

struct FlowLayoutEdge {
  QString id;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  QString path;
  bool reversedForLayout = false;
  bool hasLabelPosition = false;
  qreal labelX = 0.0;
  qreal labelY = 0.0;
  QSizeF labelSize;
  FlowLabelDocument labelDocument;
};

struct FlowEdgeLabelLayout {
  QSizeF size;
  FlowLabelDocument document;
};

struct FlowLayoutCluster {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  bool swimlane = false;
  bool titleOnLeft = false;
  qreal titleBandSize = 0.0;
};

struct FlowLayoutResult {
  QVector<FlowLayoutNode> nodes;
  QVector<FlowLayoutEdge> edges;
  QVector<FlowLayoutCluster> clusters;
};

struct FlowLayoutOptions {
  qreal nodeSpacing = 50.0;
  qreal rankSpacing = 50.0;
  qreal edgeSpacing = 20.0;
  qreal nodePadding = 15.0;
  qreal clusterHorizontalPadding = 35.0;
  qreal clusterVerticalPadding = 25.0;
  qreal diagramPadding = 0.0;
  FlowLook look = FlowLook::Classic;
  QMap<QString, QSizeF> measuredEdgeLabels;
  QMap<QString, FlowEdgeLabelLayout> preparedEdgeLabels;
  QMap<QString, QSizeF> measuredClusterLabels;
  // Edge curve: "basis" (default, d3 curveBasis), "linear" (curveLinear),
  // "step" (curveStep). Mirrors mermaid's flowchart.curve config.
  QString curve = QStringLiteral("basis");
  // Mermaid's standalone flowchart adapter consumes coordinates relative to
  // its first semantic node, while Swimlane's explicit Dagre fallback keeps
  // the coordinates emitted by Dagre and lets setupGraphViewbox add padding.
  bool preserveDagreCoordinates = false;
};

struct FlowTextOptions {
  QString fontFamily = QStringLiteral("Arial");
  qreal fontPixelSize = 16.0;
  qreal lineHeight = 24.0;
  qreal horizontalPadding = 30.0;
  qreal verticalPadding = 15.0;
  FlowLook look = FlowLook::Classic;
  bool htmlLabels = true;
  // Swimlane's renderer observes the browser's insertion-time inline width
  // and post-layout SVG terminal fringe. Other Mermaid renderers use their
  // own DOM/getBBox stage and retain the established shared measurements.
  bool chromiumInlineWidth = false;
  bool chromiumSvgTerminalPhase = false;
};

// Measures a label's bbox with the given text options (QFontMetrics). Used by
// measureFlowchartNodes and by flowShapeGeometry for shapes whose outline
// depends on the label (bang, cloud).
QSizeF measureLabel(const QString& text, const FlowTextOptions& options = {});
QSizeF measureLabel(const QString& text, const QString& labelType,
                    const FlowTextOptions& options = {});
QSizeF measureFlowchartEdgeLabel(const FlowEdge& edge,
                                 const FlowTextOptions& options = {});
FlowEdgeLabelLayout layoutFlowchartEdgeLabel(
    const FlowEdge& edge, const FlowTextOptions& options = {});
QSizeF measureFlowchartClusterLabel(const FlowSubgraph& subgraph,
                                    const FlowTextOptions& options = {});

QMap<QString, QSizeF> measureFlowchartNodes(const FlowchartData& data,
                                            FlowTextOptions options = {});

// Shared generic-node intersection used by diagram families that reuse
// Mermaid's dagre-wrapper shapes without using Dagre for placement (Block).
// `nodeRect` is centred at its scene position and `toward` is the next point
// on the edge. The result follows the same ellipse/polygon/rect dispatch and
// diamond bias correction as the flowchart renderer.
QPointF intersectFlowShape(const FlowVertex& vertex, const QRectF& nodeRect,
                           const QPointF& toward,
                           FlowLook look = FlowLook::Classic);

// Applies Mermaid's marker refX contract to the rendered path while preserving
// the unmodified router points for label placement and marker orientation.
void clipFlowEdgeForMarkers(QVector<QPointF>& points, const QString& type);

FlowLayoutResult layoutFlowchartNodes(const FlowchartData& data,
                                      const QMap<QString, QSizeF>& measuredNodes,
                                      FlowLayoutOptions options = {});

// Native Dagre compound pipeline (milestone C). Same contract as
// layoutFlowchartNodes; kept separate so the two can be A/B-verified against the
// same geometry goldens before the flat pipeline is retired.
//
// `dagreSnapshots` (optional, milestone G2): when non-null, receives the
// intermediate-state snapshots captured at the rank / parentDummyChains /
// addBorderSegments / order phase boundaries for the Level-2 intermediate
// golden. Null by default → no overhead.
FlowLayoutResult layoutFlowchartNodesDagre(const FlowchartData& data,
                                           const QMap<QString, QSizeF>& measuredNodes,
                                           FlowLayoutOptions options = {},
                                           std::vector<dagre::DagreSnapshot>* dagreSnapshots = nullptr);

}  // namespace muffin::mermaid::flowchart
