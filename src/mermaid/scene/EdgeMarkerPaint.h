#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cmath>

namespace muffin::mermaid::scene {

// Shared edge-marker painting primitives. Four families (flowchart, classdiagram, erdiagram,
// requirement) replay an SVG `<marker>` definition at an edge endpoint with the same transform
// idiom — translate(endpoint); rotate(tangent); translate(-refX, -refY) — and erdiagram and
// requirement also duplicate the polyline stitching used to estimate the edge tangent. These
// helpers are that single implementation; the marker GEOMETRY (path data, refX/refY, colors)
// stays per-family, matching each family's upstream defs.

constexpr qreal kMarkerPi = 3.14159265358979323846;

// atan2 tangent → degrees: the rotation every marker replay uses.
inline qreal tangentAngleDeg(const QPointF& tangent) {
  return std::atan2(tangent.y(), tangent.x()) * 180.0 / kMarkerPi;
}

// Apply the SVG marker anchor transform: the marker's local (refX, refY) lands on `endpoint`
// and the local +X axis aligns with the edge tangent. Call between painter.save()/restore();
// the caller keeps ownership of pen/brush and the geometry it draws.
inline void applyMarkerTransform(QPainter& painter, const QPointF& endpoint, qreal angleDeg,
                                 qreal refX, qreal refY) {
  painter.translate(endpoint);
  painter.rotate(angleDeg);
  painter.translate(-refX, -refY);
}

// Continuous polyline for marker tangent estimation. Prefers `points`; otherwise stitches
// `segments` (dropping the shared joint vertex between consecutive segment polylines) so
// first()/last() still bound the full edge.
QVector<QPointF> stitchEdgePolyline(const QVector<QPointF>& points,
                                    const QVector<QVector<QPointF>>& segments);

// Edge stroke path: the authored SVG `path` when present, else the stitched polyline.
QPainterPath edgePolylinePath(const QString& path, const QVector<QPointF>& points,
                              const QVector<QVector<QPointF>>& segments);

}  // namespace muffin::mermaid::scene
