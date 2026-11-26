#include "mermaid/scene/EdgeMarkerPaint.h"

#include "mermaid/scene/SvgPathParse.h"

namespace muffin::mermaid::scene {

QVector<QPointF> stitchEdgePolyline(const QVector<QPointF>& points, const QVector<QVector<QPointF>>& segments) {
  if (!points.isEmpty()) {
    return points;
  }
  QVector<QPointF> flat;
  for (const QVector<QPointF>& seg : segments) {
    if (seg.isEmpty()) {
      continue;
    }
    if (flat.isEmpty()) {
      flat = seg;
    } else {
      const qsizetype offset = (!flat.isEmpty() && flat.last() == seg.first()) ? 1 : 0;
      for (qsizetype i = offset; i < seg.size(); ++i) {
        flat.append(seg.at(i));
      }
    }
  }
  return flat;
}

QPointF pathStartTangent(const QPainterPath& path) {
  // A cubic is stored as its end point, then the second and the first control
  // point; the path leaves its start towards the first one.
  const QPainterPath::Element start = path.elementAt(0);
  const QPainterPath::Element next = path.elementAt(1);
  const QPainterPath::Element towards = next.type == QPainterPath::CurveToElement ? path.elementAt(3) : next;
  return QPointF(towards.x, towards.y) - QPointF(start.x, start.y);
}

QPointF pathEndTangent(const QPainterPath& path) {
  // A path enters its end from the second control point, or from the point
  // before a line.
  const qsizetype count = path.elementCount();
  const QPainterPath::Element end = path.elementAt(count - 1);
  const QPainterPath::Element from = path.elementAt(count - 2);
  return QPointF(end.x, end.y) - QPointF(from.x, from.y);
}

QPainterPath edgePolylinePath(const QString& path, const QVector<QPointF>& points,
                              const QVector<QVector<QPointF>>& segments) {
  if (!path.isEmpty()) {
    return parseSvgPath(path);
  }
  QPainterPath stroke;
  const QVector<QPointF> flat = stitchEdgePolyline(points, segments);
  if (flat.isEmpty()) {
    return stroke;
  }
  stroke.moveTo(flat.first());
  for (qsizetype i = 1; i < flat.size(); ++i) {
    stroke.lineTo(flat.at(i));
  }
  return stroke;
}

} // namespace muffin::mermaid::scene
