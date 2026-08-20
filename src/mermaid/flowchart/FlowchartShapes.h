#pragma once

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace muffin::mermaid::flowchart {

struct FlowShapeGeometry {
  QString kind;
  QRectF bounds;
  qreal cornerRadius = 0.0;
  qreal radiusX = 0.0;
  qreal radiusY = 0.0;
  QVector<QPointF> points;
};

// Outline polygon for polygon-geometry shapes in the handler's rendered local
// coordinates. Most are centred at the origin; question keeps its upstream
// +0.5px x adjustment, and bang/cloud retain the offset produced by translating
// their SVG arc path by (-w/2,-h/2). The bbox size still equals the node's
// label-container bbox. Empty for rect/ellipse/cylinder/stadium/subroutine shapes.
// The points are 1:1 ports of each upstream shape handler's `points` array after
// the handler's centring transform (insertPolygonShape's translate(-w/2, h/2),
// triangle's translate(-h/2, h/2), slopedRect's translate(0, h/4), etc.) is
// applied — i.e. the points as they appear in the rendered SVG. Intersection
// maps the polygon's minimum corner to the dagre node's top-left, matching the
// upstream polygon helper even for a locally offset silhouette.
QVector<QPointF> flowShapePolygonPoints(const QString& canonicalType, qreal width, qreal height,
                                        FlowLook look = FlowLook::Classic);

// For the SVG-arc shapes (bang, cloud) the node bbox is an affine function of
// the label-derived path parameters (effectiveW/H for bang, w/h for cloud) —
// every outline coordinate and arc radius is a fixed fraction of those, so the
// bbox is exactly affine. This evaluates the forward model from the label dims,
// so measureFlowchartNodes can size them without rendering.
QSizeF flowShapeArcShapeSize(const QString& canonicalType, qreal labelW, qreal labelH);

// Filled QPainterPath for a tiltedCylinder (horizontal cylinder). Its
// createCylinderPathD3 path is two subpaths (left cap + top + right cap[sweep1];
// right cap[sweep0] + bottom + implicit diagonal close) with nonzero fill that
// renders a shape smaller than the path bbox. The arcs are sampled in SVG
// traversal order (Qt's arcTo reverses arc direction, flipping the winding), so
// the path is built point-by-point and filled with WindingFill (== SVG nonzero).
QPainterPath flowShapeHorizontalCylinderPath(const QRectF& bounds, qreal radiusX, qreal radiusY);

// `shapeRadius` is the RAW config.themeVariables.radius (shapes/roundedRect
// reads `themeVariables?.radius ?? 5` — the per-theme radius literals never
// reach this consumer; only a user override does).
FlowShapeGeometry flowShapeGeometry(const FlowVertex& vertex, const QSizeF& size,
                                    FlowLook look = FlowLook::Classic,
                                    qreal shapeRadius = 5.0);

}  // namespace muffin::mermaid::flowchart
