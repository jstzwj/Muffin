#include "mermaid/kanban/KanbanScenePainter.h"

#include "mermaid/rough/RoughPaint.h"
#include "mermaid/theme/MermaidColor.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::kanban {
namespace {

color::SvgPaint rootPaint(const QString& value) {
  if (value.trimmed().isEmpty()) return {false, QColor(Qt::black)};
  return color::resolveSvgPaint(value, color::SvgPaintKind::Fill,
                                QColor(Qt::black));
}

QBrush fillBrush(const QString& value, const color::SvgPaint& root) {
  if (value.trimmed().isEmpty())
    return root.none ? QBrush(Qt::NoBrush) : QBrush(root.color);
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, root.color);
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

QPen strokePen(const QString& value, qreal width,
               const color::SvgPaint& root) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Stroke, root.color);
  if (paint.none || !(width > 0.0) || !std::isfinite(width))
    return QPen(Qt::NoPen);
  QPen pen(paint.color, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

void paintDropShadow(QPainter& painter, const QRectF& rect, qreal radius,
                     const QColor& color, qreal opacity,
                     const QPointF& offset) {
  if (rect.isEmpty() || opacity <= 0.0 || color.alpha() == 0) return;
  static const qreal weights[5][5] = {
      {1, 2, 3, 2, 1}, {2, 4, 6, 4, 2}, {3, 6, 9, 6, 3},
      {2, 4, 6, 4, 2}, {1, 2, 3, 2, 1}};
  const qreal total = 81.0;
  painter.setPen(Qt::NoPen);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      QColor c = color;
      c.setAlphaF(std::clamp(opacity * weights[y][x] / total, 0.0, 1.0));
      painter.setBrush(c);
      painter.drawRoundedRect(rect.translated(offset + QPointF(x - 2, y - 2)),
                              radius, radius);
    }
  }
}

void paintLabel(QPainter& painter, const KanbanScene& scene,
                const KanbanLabelGeometry& label) {
  if (label.source.isEmpty() || label.bounds.isEmpty()) return;
  const color::SvgPaint root = rootPaint(scene.style.textColor);
  const color::SvgPaint text = label.fill.trimmed().isEmpty()
      ? root
      : color::resolveSvgPaint(label.fill, color::SvgPaintKind::Text,
                               root.color);
  if (text.none || !(scene.style.fontSize > 0.0)) return;
  const qreal lineHeight = scene.config.htmlLabels
      ? scene.style.fontSize * 1.5 : scene.style.fontSize * 1.1;
  flowchart::paintFlowLabel(
      painter, label.document, label.bounds, scene.style.fontFamily,
      scene.style.fontSize, lineHeight, text.color, false,
      label.centered ? flowchart::FlowLabelAlign::Center
                     : flowchart::FlowLabelAlign::Left);
}

}  // namespace

void paintKanbanScene(const KanbanScene& scene, QPainter& painter,
                      const MermaidPaintOptions& options) {
  const color::SvgPaint root = rootPaint(scene.style.textColor);
  for (const KanbanSectionGeometry& section : scene.sections) {
    if (!mermaidPrimitiveIsVisible(section.paintedBounds.united(section.label.bounds),
                                   options))
      continue;
    const QBrush fill = fillBrush(section.fill, root);
    const QPen stroke = strokePen(section.stroke, section.strokeWidth, root);
    if (section.dropShadow) {
      const color::SvgPaint shadow = color::resolveSvgPaint(
          scene.style.shadowColor, color::SvgPaintKind::Fill, QColor(Qt::black));
      if (!shadow.none)
        paintDropShadow(painter, section.shapeBounds, 5.0, shadow.color,
                        scene.style.shadowOpacity,
                        QPointF(scene.style.shadowOffsetX,
                                scene.style.shadowOffsetY));
    }
    if (section.handDrawn) {
      rough::drawRoughDrawable(painter, section.roughDrawable, fill, stroke,
                               QPen(fill.color(), 1.0));
    } else {
      painter.setPen(stroke);
      painter.setBrush(fill);
      painter.drawRoundedRect(section.shapeBounds, 5.0, 5.0);
    }
    paintLabel(painter, scene, section.label);
  }

  for (const KanbanItemGeometry& item : scene.items) {
    if (!mermaidPrimitiveIsVisible(item.bounds, options)) continue;
    painter.setPen(strokePen(item.stroke, item.strokeWidth, root));
    painter.setBrush(fillBrush(item.fill, root));
    painter.drawRoundedRect(item.localBounds.translated(item.position),
                            item.radius, item.radius);
    paintLabel(painter, scene, item.title);
    paintLabel(painter, scene, item.ticket);
    paintLabel(painter, scene, item.assigned);
    if (item.priorityVisible) {
      const color::SvgPaint priority = color::resolveSvgPaint(
          item.priorityStroke, color::SvgPaintKind::Stroke, root.color);
      if (!priority.none) {
        QPen pen(priority.color, item.priorityStrokeWidth);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(item.priorityLine);
      }
    }
  }
}

}  // namespace muffin::mermaid::kanban
