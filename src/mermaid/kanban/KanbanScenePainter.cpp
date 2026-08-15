#include "mermaid/kanban/KanbanScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/theme/MermaidColor.h"

#include <QPainter>
#include <QJsonValue>

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

qreal effectiveOpacity(const KanbanElementCss& css) {
  return css.opacity >= 0.0 ? css.opacity : 1.0;
}

// A themeCSS stroke-width resolves like any CSS length: em/ex against the
// root font, percentages against the viewBox diagonal.
qreal cssStrokeWidthValue(const QString& raw, const KanbanScene& scene) {
  CssLengthContext context = editor::pieCssLengthContext(
      scene.style.fontFamily, scene.style.fontSize);
  context.viewportPx = scene.bounds.size();
  const qreal diagonal =
      std::hypot(scene.bounds.width(), scene.bounds.height()) /
      std::sqrt(2.0);
  return editor::cssStrokeWidthPx(raw, context, diagonal);
}

void paintLabel(QPainter& painter, const KanbanScene& scene,
                const KanbanLabelGeometry& label) {
  if (label.source.isEmpty() || label.bounds.isEmpty() ||
      !label.css.visible)
    return;
  const color::SvgPaint root = rootPaint(scene.style.textColor);
  const QString colorValue =
      !label.css.color.isEmpty() ? label.css.color : label.fill;
  const color::SvgPaint text = colorValue.trimmed().isEmpty()
      ? root
      : color::resolveSvgPaint(colorValue, color::SvgPaintKind::Text,
                               root.color);
  if (text.none || !(label.fontSize > 0.0)) return;
  const qreal lineHeight =
      label.html ? label.fontSize * 1.5 : label.fontSize * 1.1;
  painter.save();
  painter.setOpacity(effectiveOpacity(label.css));
  flowchart::paintFlowLabel(
      painter, label.document, label.bounds, label.fontFamily,
      label.fontSize, lineHeight, text.color, false,
      label.centered ? flowchart::FlowLabelAlign::Center
                     : flowchart::FlowLabelAlign::Left);
  painter.restore();
}

}  // namespace

void paintKanbanScene(const KanbanScene& scene, QPainter& painter,
                      const MermaidPaintOptions& options) {
  const color::SvgPaint root = rootPaint(scene.style.textColor);
  for (const KanbanSectionGeometry& section : scene.sections) {
    if (!mermaidPrimitiveIsVisible(section.paintedBounds.united(section.label.bounds),
                                   options))
      continue;
    if (!section.clusterCss.visible || !section.boxCss.visible) {
      paintLabel(painter, scene, section.label);
      continue;
    }
    const QString fillValue =
        !section.boxCss.fill.isEmpty() ? section.boxCss.fill : section.fill;
    const QString strokeValue = !section.boxCss.stroke.isEmpty()
                                    ? section.boxCss.stroke
                                    : section.stroke;
    const qreal strokeWidth = !section.boxCss.strokeWidth.isEmpty()
                                  ? cssStrokeWidthValue(
                                        section.boxCss.strokeWidth, scene)
                                  : section.strokeWidth;
    const QBrush fill = fillBrush(fillValue, root);
    const QPen stroke = strokePen(strokeValue, strokeWidth, root);
    painter.save();
    painter.setOpacity(effectiveOpacity(section.boxCss));
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
    painter.restore();
    paintLabel(painter, scene, section.label);
  }

  for (const KanbanItemGeometry& item : scene.items) {
    if (!mermaidPrimitiveIsVisible(item.bounds, options)) continue;
    const QString fillValue =
        !item.boxCss.fill.isEmpty() ? item.boxCss.fill : item.fill;
    const QString strokeValue =
        !item.boxCss.stroke.isEmpty() ? item.boxCss.stroke : item.stroke;
    const qreal strokeWidth = !item.boxCss.strokeWidth.isEmpty()
                                  ? cssStrokeWidthValue(
                                        item.boxCss.strokeWidth, scene)
                                  : item.strokeWidth;
    if (item.nodeCss.visible && item.boxCss.visible) {
      painter.save();
      painter.setOpacity(effectiveOpacity(item.boxCss));
      painter.setPen(strokePen(strokeValue, strokeWidth, root));
      painter.setBrush(fillBrush(fillValue, root));
      painter.drawRoundedRect(item.localBounds.translated(item.position),
                              item.radius, item.radius);
      painter.restore();
    }
    paintLabel(painter, scene, item.title);
    paintLabel(painter, scene, item.ticket);
    paintLabel(painter, scene, item.assigned);
    if (item.priorityVisible && item.priorityCss.visible) {
      const QString priorityValue = !item.priorityCss.stroke.isEmpty()
                                        ? item.priorityCss.stroke
                                        : item.priorityStroke;
      const qreal priorityWidth = !item.priorityCss.strokeWidth.isEmpty()
                                      ? cssStrokeWidthValue(
                                            item.priorityCss.strokeWidth,
                                            scene)
                                      : item.priorityStrokeWidth;
      const color::SvgPaint priority = color::resolveSvgPaint(
          priorityValue, color::SvgPaintKind::Stroke, root.color);
      if (!priority.none) {
        painter.save();
        painter.setOpacity(effectiveOpacity(item.priorityCss));
        QPen pen(priority.color, priorityWidth);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(item.priorityLine);
        painter.restore();
      }
    }
  }
}

}  // namespace muffin::mermaid::kanban
