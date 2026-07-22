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
  FlowLook look = FlowLook::Classic;
  QMap<QString, QSizeF> measuredEdgeLabels;
  QMap<QString, FlowEdgeLabelLayout> preparedEdgeLabels;
  QMap<QString, QSizeF> measuredClusterLabels;
  // Edge curve: "basis" (default, d3 curveBasis), "linear" (curveLinear),
  // "step" (curveStep). Mirrors mermaid's flowchart.curve config.
  QString curve = QStringLiteral("basis");
};

struct FlowTextOptions {
  QString fontFamily = QStringLiteral("Arial");
  qreal fontPixelSize = 16.0;
  qreal lineHeight = 24.0;
  qreal horizontalPadding = 30.0;
  qreal verticalPadding = 15.0;
  FlowLook look = FlowLook::Classic;
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
