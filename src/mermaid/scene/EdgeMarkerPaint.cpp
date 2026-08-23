#include "mermaid/scene/EdgeMarkerPaint.h"

#include "mermaid/scene/SvgPathParse.h"

namespace muffin::mermaid::scene {

QVector<QPointF> stitchEdgePolyline(const QVector<QPointF>& points,
                                    const QVector<QVector<QPointF>>& segments) {
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

}  // namespace muffin::mermaid::scene
