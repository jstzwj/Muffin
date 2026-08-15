#pragma once

#include "mermaid/flowchart/FlowchartLayout.h"

namespace muffin::mermaid::flowchart {

struct SwimlaneLayoutOptions {
  qreal nodeSpacing = 50.0;
  qreal rankSpacing = 50.0;
  qreal nodePadding = 15.0;
  qreal labelLineHeight = 24.0;
  FlowLook look = FlowLook::Classic;
  bool ignoreCrossLaneEdges = true;
  bool optimizeRanksByCrossings = true;
  bool automaticLaneOrdering = false;
  QString lineHops = QStringLiteral("arc");
  QString curve = QStringLiteral("basis");
  QMap<QString, QSizeF> renderedNodeSizes;
  QMap<QString, FlowEdgeLabelLayout> preparedEdgeLabels;
  QMap<QString, QSizeF> measuredClusterLabels;
};

// Mermaid inserts hand-drawn node shapes before Swimlane layout and feeds the
// generated rough SVG group's getBBox to the layout engine. The returned map
// is that layout-facing size; `measuredNodes` remains the shape's paint size.
QMap<QString, QSizeF> measureSwimlaneHandDrawnNodes(
    const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
    quint32 handDrawnSeed);

// Native port of Mermaid 11.16's swimlane Sugiyama pipeline. The flowchart
// parser/DB and scene remain shared; only placement, lane geometry and routing
// differ from Dagre.
FlowLayoutResult layoutSwimlaneNodes(
    const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
    SwimlaneLayoutOptions options = {});

}  // namespace muffin::mermaid::flowchart
