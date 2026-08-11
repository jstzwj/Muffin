#include "mermaid/treemap/TreemapScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/treemap/TreemapScene.h"

#include <QFontMetricsF>
#include <QPainter>

#include <cmath>

namespace muffin::mermaid::treemap {
namespace {

QColor paintColor(const QString &value, const QColor &fallback) {
  const auto paint = color::resolveSvgPaint(value, color::SvgPaintKind::Fill,
                                             fallback);
  return paint.none ? QColor(Qt::transparent) : paint.color;
}

void drawRect(QPainter &painter, const QRectF &rect, const QString &fill,
              qreal fillOpacity, const QString &stroke, qreal strokeWidth,
              qreal strokeOpacity) {
  QColor fillColor = paintColor(fill, QColor(Qt::black));
  fillColor.setAlphaF(fillColor.alphaF() * qBound(0.0, fillOpacity, 1.0));
  painter.setBrush(fillColor.alpha() ? QBrush(fillColor) : QBrush(Qt::NoBrush));
  const auto strokePaint = color::resolveSvgPaint(
      stroke, color::SvgPaintKind::Stroke, QColor(Qt::black));
  if (strokePaint.none || !(strokeWidth > 0.0)) {
    painter.setPen(Qt::NoPen);
  } else {
    QColor strokeColor = strokePaint.color;
    strokeColor.setAlphaF(strokeColor.alphaF() * qBound(0.0, strokeOpacity, 1.0));
    QPen pen(strokeColor, strokeWidth);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
  }
  painter.drawRect(rect);
}

void drawText(QPainter &painter, const TreemapScene &scene,
              const TreemapTextGeometry &text) {
  if (!text.visible || text.text.isEmpty() || !(text.fontSize > 0.0))
    return;
  const auto fill = color::resolveSvgPaint(text.fill,
                                           color::SvgPaintKind::Text,
                                           QColor(Qt::black));
  if (fill.none) return;
  auto font = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(scene.style.fontFamily), text.fontSize);
  font.font.setWeight(text.bold ? QFont::Bold : QFont::Normal);
  font.font.setItalic(text.italic);
  const QFontMetricsF metrics(font.font);
  const qreal width = metrics.horizontalAdvance(text.text) * font.scale;
  qreal x = text.position.x();
  if (text.anchor == QLatin1String("middle")) x -= width / 2.0;
  else if (text.anchor == QLatin1String("end")) x -= width;
  const auto vertical = flowchart::flowLabelFontBoundingMetrics(
      scene.style.fontFamily, text.fontSize,
      text.bold ? QFont::Bold : QFont::Normal,
      text.italic ? QFont::StyleItalic : QFont::StyleNormal);
  qreal baseline = text.position.y() +
      (text.baseline == TreemapTextBaseline::Middle
           ? vertical.xHeight / 2.0
           : vertical.ascent * 0.8);
  painter.save();
  if (!text.clip.isEmpty()) painter.setClipRect(text.clip);
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.drawText(QPointF(0.0, 0.0), text.text);
  painter.restore();
}

} // namespace

void paintTreemapScene(const TreemapScene &scene, QPainter &painter,
                       const MermaidPaintOptions &) {
  drawText(painter, scene, scene.title);
  for (const auto &section : scene.sections) {
    if (section.depth == 0) continue;
    drawRect(painter, section.rect, section.fill, section.fillOpacity,
             section.stroke, section.strokeWidth, section.strokeOpacity);
    drawText(painter, scene, section.label);
    drawText(painter, scene, section.value);
  }
  for (const auto &leaf : scene.leaves) {
    drawRect(painter, leaf.rect, leaf.fill, leaf.fillOpacity, leaf.stroke,
             leaf.strokeWidth, 1.0);
    drawText(painter, scene, leaf.label);
    drawText(painter, scene, leaf.value);
  }
}

} // namespace muffin::mermaid::treemap
