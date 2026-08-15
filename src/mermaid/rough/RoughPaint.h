#pragma once

// Shared rough (handDrawn) painting helpers.
//
// `drawRoughDrawable` is the QPainter analogue of rough.js's canvas renderer:
// it walks a rough::Drawable's OpSets and, for each one, sets the matching
// QPainter state — FillPath paints the solid fill brush, FillSketch strokes the
// hachure, Path strokes the outline — then draws the OpSet as a path. Every
// diagram painter that supports the handDrawn look (flowchart, sequence, class,
// state) routes its rough shapes through this single helper so the
// fill/stroke/hachure convention is identical across diagram types.
//
// The `roughRect`/`roughPath`/`roughLine` convenience wrappers build the
// Options + Drawable for the common shapes and are what the non-flowchart
// painters call from their `if (scene.handDrawn)` branches. Filled boxes use a
// soft hachure (roughness 0.7); strokes and edges use a tighter jitter
// (roughness 0.3) so connections read cleaner than their boxes. Flowchart keeps
// its own finer-grained Options (category-mask rendering) and calls
// drawRoughDrawable directly.

#include "mermaid/rough/RoughOps.h"
#include "mermaid/scene/SvgPathParse.h"

#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

namespace muffin::mermaid::rough {

inline QPainterPath roundedRectPath(const QRectF& bounds, qreal radius) {
  const QString data = QStringLiteral(
      "M %1 %2 H %3 A %4 %4 0 0 1 %5 %6 V %7 "
      "A %4 %4 0 0 1 %3 %8 H %1 A %4 %4 0 0 1 %9 %7 "
      "V %6 A %4 %4 0 0 1 %1 %2 Z")
      .arg(bounds.left() + radius, 0, 'g', 17)
      .arg(bounds.top(), 0, 'g', 17)
      .arg(bounds.right() - radius, 0, 'g', 17)
      .arg(radius, 0, 'g', 17)
      .arg(bounds.right(), 0, 'g', 17)
      .arg(bounds.top() + radius, 0, 'g', 17)
      .arg(bounds.bottom() - radius, 0, 'g', 17)
      .arg(bounds.bottom(), 0, 'g', 17)
      .arg(bounds.left(), 0, 'g', 17);
  return scene::parseSvgPath(data);
}

inline Drawable roughRoundedRectPathDrawable(const QRectF& bounds,
                                              qreal radius,
                                              Options options = {}) {
  if (!qFuzzyIsNull(radius))
    return path(roundedRectPath(bounds, radius), std::move(options), true);

  const QPointF topLeft = bounds.topLeft();
  const QPointF topRight = bounds.topRight();
  const QPointF bottomRight = bounds.bottomRight();
  const QPointF bottomLeft = bounds.bottomLeft();
  const auto cubicAt = [](const QPointF& point) {
    return PathCommand{PathCommandType::CubicTo, point, point, point};
  };
  QVector<PathCommand> commands{
      {PathCommandType::Move, topLeft, {}, {}},
      {PathCommandType::LineTo, topRight, {}, {}}, cubicAt(topRight),
      {PathCommandType::LineTo, bottomRight, {}, {}}, cubicAt(bottomRight),
      {PathCommandType::LineTo, bottomLeft, {}, {}}, cubicAt(bottomLeft),
      {PathCommandType::LineTo, topLeft, {}, {}}, cubicAt(topLeft),
      {PathCommandType::Close, {}, {}, {}}};
  return path(commands, std::move(options));
}

inline Options nodeOptions(quint32 seed, qreal strokeWidth = 1.3) {
  Options options;
  options.seed = seed;
  options.roughness = 0.7;
  options.strokeWidth = strokeWidth;
  options.fill = QStringLiteral("#000");
  options.fillWeight = 4.0;
  options.hachureGap = 5.2;
  return options;
}

inline Options edgeOptions(quint32 seed, qreal strokeWidth = 1.0) {
  Options options;
  options.seed = seed;
  options.roughness = 0.3;
  options.strokeWidth = strokeWidth;
  return options;
}

inline Drawable roughRectDrawable(const QRectF& rect, quint32 seed,
                                  qreal strokeWidth = 1.3) {
  return rectangle(rect.x(), rect.y(), rect.width(), rect.height(),
                   nodeOptions(seed, strokeWidth));
}

inline Drawable roughNodeLineDrawable(const QPointF& a, const QPointF& b,
                                      quint32 seed,
                                      qreal strokeWidth = 1.3) {
  return line(a.x(), a.y(), b.x(), b.y(), nodeOptions(seed, strokeWidth));
}

inline Drawable roughEdgeDrawable(const QPainterPath& source, quint32 seed,
                                  qreal strokeWidth = 1.0) {
  return path(source, edgeOptions(seed, strokeWidth));
}

inline Drawable translatedDrawable(Drawable drawable, const QPointF& delta) {
  if (delta.isNull()) return drawable;
  for (OpSet& set : drawable.sets) {
    for (Op& op : set.ops) {
      for (qsizetype i = 0; i + 1 < op.data.size(); i += 2) {
        op.data[i] += delta.x();
        op.data[i + 1] += delta.y();
      }
    }
  }
  return drawable;
}

inline void drawRoughDrawable(QPainter& painter, const Drawable& drawable,
                              const QBrush& fillBrush, const QPen& strokePen,
                              const QPen& fillSketchPen) {
  for (const OpSet& set : drawable.sets) {
    switch (set.type) {
      case OpSetType::FillPath:
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillBrush);
        break;
      case OpSetType::FillSketch:
        painter.setPen(fillSketchPen);
        painter.setBrush(Qt::NoBrush);
        break;
      case OpSetType::Path:
        painter.setPen(strokePen);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    painter.drawPath(toPainterPath(set));
  }
}

inline void roughRect(QPainter& painter, const QRectF& rect, quint32 seed,
                      const QColor& fill, const QColor& stroke, qreal strokeWidth) {
  drawRoughDrawable(painter, roughRectDrawable(
                        rect, seed, std::max<qreal>(1.3, strokeWidth)),
                    fill, QPen(stroke, strokeWidth), QPen(fill, strokeWidth));
}

inline void roughPath(QPainter& painter, const QPainterPath& source, quint32 seed,
                      const QColor& stroke, qreal strokeWidth) {
  drawRoughDrawable(painter, roughEdgeDrawable(
                        source, seed, std::max<qreal>(1.3, strokeWidth)),
                    Qt::NoBrush,
                    QPen(stroke, strokeWidth), Qt::NoPen);
}

inline void roughLine(QPainter& painter, const QPointF& a, const QPointF& b,
                      quint32 seed, const QColor& stroke, qreal strokeWidth) {
  const Drawable drawable = line(
      a.x(), a.y(), b.x(), b.y(),
      edgeOptions(seed, std::max<qreal>(1.3, strokeWidth)));
  drawRoughDrawable(painter, drawable, Qt::NoBrush,
                    QPen(stroke, strokeWidth), Qt::NoPen);
}

}  // namespace muffin::mermaid::rough
