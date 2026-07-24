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

#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

namespace muffin::mermaid::rough {

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
  Options options;
  options.seed = seed;
  options.roughness = 0.7;
  options.strokeWidth = std::max<qreal>(1.3, strokeWidth);
  options.fill = QStringLiteral("#000");
  options.fillWeight = 3.0;
  options.hachureGap = 5.2;
  drawRoughDrawable(painter,
                    rectangle(rect.x(), rect.y(), rect.width(), rect.height(), options),
                    fill, QPen(stroke, strokeWidth), QPen(fill, strokeWidth));
}

inline void roughPath(QPainter& painter, const QPainterPath& source, quint32 seed,
                      const QColor& stroke, qreal strokeWidth) {
  Options options;
  options.seed = seed;
  options.roughness = 0.3;
  options.strokeWidth = std::max<qreal>(1.3, strokeWidth);
  drawRoughDrawable(painter, path(source, options), Qt::NoBrush,
                    QPen(stroke, strokeWidth), Qt::NoPen);
}

inline void roughLine(QPainter& painter, const QPointF& a, const QPointF& b,
                      quint32 seed, const QColor& stroke, qreal strokeWidth) {
  Options options;
  options.seed = seed;
  options.roughness = 0.3;
  options.strokeWidth = std::max<qreal>(1.3, strokeWidth);
  drawRoughDrawable(painter, line(a.x(), a.y(), b.x(), b.y(), options), Qt::NoBrush,
                    QPen(stroke, strokeWidth), Qt::NoPen);
}

}  // namespace muffin::mermaid::rough
