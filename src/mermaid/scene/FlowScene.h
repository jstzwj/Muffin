#pragma once

// Immutable flowchart scene layer (milestone F2) — the bridge between layout
// and QPainter. `buildFlowScene` joins FlowchartData (semantic) + FlowLayoutResult
// (geometry) + FlowTheme (resolved colours) + FlowStyleResolve (cascade) into a
// single ordered scene. The painter (F3) consumes ONLY this scene — it never
// re-reads FlowDB or re-runs layout. Screen and export share the scene; only the
// viewport transform differs.
//
// Draw order matches mermaid's SVG DOM: clusters -> edges -> edge labels -> nodes
// (verified via the rendered SVG: g.clusters, g.edgePaths, g.edgeLabels, g.nodes).
// Markers ride on edges (marker-end/marker-start); node labels are foreignObject
// in mermaid 11.16 even with htmlLabels:false, but the scene captures only the
// label text, parsed rich-text data, prepared Math operations, position and font.

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/theme/FlowTheme.h"

#include <QRectF>
#include <QPainterPath>
#include <QString>
#include <QVector>

namespace muffin::mermaid::flowscene {

struct FlowSceneLabel {
  QString text;
  QString labelType = QStringLiteral("text");
  qreal x = 0.0;          // center, scene coords (relative to diagram origin)
  qreal y = 0.0;
  QString color;          // resolved text colour
  QString fontFamily;
  QString fontSize;
  QString fontWeight;
  QString background;     // edge-label bg (edgeLabelBackground); empty for nodes
  bool mathEnabled = false;  // Mermaid enables MathML only for node labels.
  flowchart::FlowLabelDocument richText;  // parsed and Math-prepared scene data
};

struct FlowSceneShapePath {
  QPainterPath path;      // node-local coordinates, centred at (0, 0)
  bool fill = true;
  bool stroke = true;
  QString fillOverride;
  QString strokeOverride;
};

struct FlowSceneNode {
  QString id;
  QString shapeType;      // canonical Mermaid shape id (painter-only)
  QString shapeKind;      // flowShapeGeometry.kind (rect/roundedRect/ellipse/polygon/...)
  qreal cx = 0.0, cy = 0.0;  // center, scene coords
  qreal width = 0.0, height = 0.0;
  QString fill;
  QString stroke;
  QString strokeWidth;
  FlowSceneLabel label;
  QString link;           // hit region (vertex.link)
  QString tooltip;
  // Painter-only shape geometry (from flowShapeGeometry; NOT serialized by
  // toJson — the painter consumes them, the JSON golden verifies shapeKind).
  qreal cornerRadius = 0.0;
  qreal radiusX = 0.0, radiusY = 0.0;
  QVector<QPointF> points;  // polygon outline (centred at origin)
  QVector<FlowSceneShapePath> shapePaths;  // ordered like upstream SVG children
  // Edge endpoints + tangents for marker orientation (painter-only).
};

struct FlowSceneEdge {
  QString id;
  QString path;           // SVG path `d` (relative coords, from FlowLayoutEdge.path)
  QString stroke;
  QString strokeWidth;
  QString strokeDasharray;
  QString markerEnd;      // marker type ("pointEnd"/...) or empty
  QString markerStart;
  FlowSceneLabel label;   // text empty if the edge has no label
  // Painter-only: path endpoints + tangent directions for marker orientation
  // (from FlowLayoutEdge.points; NOT serialized by toJson).
  QPointF startPoint, endPoint, startTangent, endTangent;
  bool animated = false;
};

struct FlowSceneCluster {
  QString id;
  qreal cx = 0.0, cy = 0.0;  // center
  qreal width = 0.0, height = 0.0;
  QString fill;
  QString stroke;
  QString strokeWidth;
  FlowSceneLabel label;
};

struct FlowScene {
  QRectF bounds;          // diagram bounds (scene coords)
  QString background;     // theme.background
  flowchart::FlowLook look = flowchart::FlowLook::Classic;
  quint32 handDrawnSeed = 0;
  bool useGradient = false;
  QString gradientStart;
  QString gradientStop;
  QString lineColor;
  QString shadowColor;
  qreal shadowOpacity = 0.25;
  qreal shadowOffsetX = 0.0;
  qreal shadowOffsetY = 1.0;
  // Draw order: clusters, then edges, then nodes (mermaid SVG order).
  QVector<FlowSceneCluster> clusters;
  QVector<FlowSceneEdge> edges;
  QVector<FlowSceneNode> nodes;

  QString toJson() const;
};

// Build the scene from data + layout + theme. Reads ONLY these inputs (no
// FlowDB re-read, no layout). `options.curve` is already baked into the edge
// paths in `layout`; the scene does not recompute curves.
FlowScene buildFlowScene(const flowchart::FlowchartData& data,
                         const flowchart::FlowLayoutResult& layout,
                         const flowtheme::FlowThemeVariables& theme,
                         flowchart::FlowLook look = flowchart::FlowLook::Classic,
                         quint32 handDrawnSeed = 0);

}  // namespace muffin::mermaid::flowscene
