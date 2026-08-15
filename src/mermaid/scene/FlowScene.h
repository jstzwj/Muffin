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
// Markers ride on edges (marker-end/marker-start). Labels retain the resolved
// HTML/SVG branch because Mermaid can mix them when only the legacy
// flowchart.htmlLabels alias is configured.

#include "mermaid/MermaidScene.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

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
  bool htmlLabels = true;
  qreal maximumLineWidth = 200.0;
  bool visible = true;
  flowchart::FlowLabelDocument richText;  // parsed and Math-prepared scene data
};

struct FlowSceneTextOptions {
  bool nodeHtmlLabels = true;
  bool auxiliaryHtmlLabels = true;
  qreal maximumLineWidth = 200.0;
  const csscascade::FlowchartProjection* css = nullptr;
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
  bool boundsVisible = true;  // display:none drops the box; visibility keeps it
  bool visible = true;
  // Painter-only shape geometry (from flowShapeGeometry; NOT serialized by
  // toJson — the painter consumes them, the JSON golden verifies shapeKind).
  qreal cornerRadius = 0.0;
  qreal radiusX = 0.0, radiusY = 0.0;
  QVector<QPointF> points;  // polygon outline (centred at origin)
  QVector<FlowSceneShapePath> shapePaths;  // ordered like upstream SVG children
  QVector<rough::Drawable> roughDrawables;
  QRectF paintedBounds;
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
  QSizeF labelSize;
  rough::Drawable roughDrawable;
  QString renderedPath;
  // Painter-only bounds. Keeping path and label separate lets viewport paint
  // skip path parsing without hiding an independently visible label.
  QRectF pathBounds;
  QRectF labelBounds;
  // Painter-only: path endpoints + tangent directions for marker orientation
  // (from FlowLayoutEdge.points; NOT serialized by toJson).
  QPointF startPoint, endPoint, startTangent, endTangent;
  bool animated = false;
  // Mermaid maps animate:true to `fast`, while an explicit animation value
  // overrides it. Empty for a static edge; otherwise `fast` or `slow`.
  QString animation;
  bool visible = true;
  // Whether the element contributes to the scene/viewBox bounds. visibility:
  // hidden keeps layout (bounds yes, paint no); display:none drops the box
  // (bounds no) — `visible` alone cannot express both.
  bool boundsVisible = true;
};

struct FlowSceneCluster {
  QString id;
  qreal cx = 0.0, cy = 0.0;  // center
  qreal width = 0.0, height = 0.0;
  bool swimlane = false;
  bool titleOnLeft = false;
  qreal titleBandSize = 0.0;
  qreal titleTopMargin = 0.0;
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString swimlaneTitleFill;
  QString swimlaneTitleStroke;
  QString swimlaneTitleStrokeWidth;
  QString swimlaneBodyFill;
  QString swimlaneBodyStroke;
  QString swimlaneBodyStrokeWidth;
  bool swimlaneTitleVisible = true;
  bool swimlaneBodyVisible = true;
  FlowSceneLabel label;
  // Hand-drawn swimlanes are generated once and shared by measurement and
  // painting. Re-running RoughJS-compatible generation in the painter would
  // make the SVG getBBox contract and the painted pixels two independent
  // computations.
  rough::Drawable roughTitle;
  rough::Drawable roughBody;
  QRectF paintedBounds;
  bool visible = true;
  bool boundsVisible = true;
};

struct FlowScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  bool roundRasterExtentToNearestPixel() const override { return true; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  SvgMarkerProjection svgMarkerProjection() const override;
  bool hasAnimation() const override {
    for (const auto& edge : edges)
      if (edge.animated) return true;
    return false;
  }
  const QVector<InteractionRegion>& interactionRegions() const override { return interactionRegions_; }

  QRectF bounds;          // diagram bounds (scene coords)
  QString background;     // theme.background
  QString markerDiagramType = QStringLiteral("flowchart-v2");
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
  QVector<InteractionRegion> interactionRegions_;  // precomputed at build

  QJsonObject toJsonObject() const override;
  // Compact-string wrapper retained for callers that want the serialized form
  // (MermaidSvgExporter digest, debug dumps). Byte-identical to the pre-split
  // toJson() output: same key order, same r3 rounding.
  QString toJson() const;
};

// Build the scene from data + layout + theme. Reads ONLY these inputs (no
// FlowDB re-read, no layout). `options.curve` is already baked into the edge
// paths in `layout`; the scene does not recompute curves.
FlowScene buildFlowScene(const flowchart::FlowchartData& data,
                         const flowchart::FlowLayoutResult& layout,
                         const flowtheme::FlowThemeVariables& theme,
                         flowchart::FlowLook look = flowchart::FlowLook::Classic,
                         quint32 handDrawnSeed = 0,
                         const FlowSceneTextOptions& textOptions = {},
                         qreal shapeRadius = 5.0);

}  // namespace muffin::mermaid::flowscene
