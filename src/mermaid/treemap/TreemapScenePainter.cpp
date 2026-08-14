#include "mermaid/treemap/TreemapScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/treemap/TreemapScene.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::treemap {
namespace {

QColor paintColor(const QString &value, const QColor &fallback) {
  const auto paint = color::resolveSvgPaint(value, color::SvgPaintKind::Fill,
                                             fallback);
  return paint.none ? QColor(Qt::transparent) : paint.color;
}

qreal cssStrokeWidthPx(const TreemapElementCss &css, qreal base,
                       const CssLengthContext &lengths, qreal diagonal) {
  const QString value = css.strokeWidth.trimmed().isEmpty()
                            ? QString::number(base) + QStringLiteral("px")
                            : css.strokeWidth;
  return editor::cssStrokeWidthPx(value, lengths, diagonal);
}

// themeCSS stroke-width resolution needs the shared length context.
struct TreemapPaintContext {
  CssLengthContext lengths;
  qreal diagonal = 1.0;
};

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

// One themeCSS-aware rect: the css fills paint, opacity and visibility.
void drawCssRect(QPainter &painter, const TreemapPaintContext &context,
                 const QRectF &rect, const TreemapElementCss &css,
                 const QString &fill, qreal fillOpacity, const QString &stroke,
                 qreal strokeWidth, qreal strokeOpacity) {
  if (!css.visible) return;
  const qreal resolvedFillOpacity =
      css.fillOpacity >= 0.0 ? css.fillOpacity : fillOpacity;
  const qreal resolvedStrokeOpacity =
      css.strokeOpacity >= 0.0 ? css.strokeOpacity : strokeOpacity;
  drawRect(painter, rect, css.fill.isEmpty() ? fill : css.fill,
           resolvedFillOpacity * (css.opacity >= 0.0 ? css.opacity : 1.0),
           css.stroke.isEmpty() ? stroke : css.stroke,
           cssStrokeWidthPx(css, strokeWidth, context.lengths, context.diagonal),
           resolvedStrokeOpacity * (css.opacity >= 0.0 ? css.opacity : 1.0));
}

void drawText(QPainter &painter, const TreemapScene &scene,
              const TreemapTextGeometry &text) {
  const bool cssVisible = text.css.visible;
  if (!text.visible || !cssVisible || text.text.isEmpty() ||
      !(text.fontSize > 0.0))
    return;
  const QString fill = text.css.fill.isEmpty() ? text.fill : text.css.fill;
  const qreal fontSize = text.css.fontSize >= 0.0 ? text.css.fontSize
                                                  : text.fontSize;
  if (!(fontSize > 0.0)) return;
  const auto resolved = color::resolveSvgPaint(fill,
                                               color::SvgPaintKind::Text,
                                               QColor(Qt::black));
  if (resolved.none) return;
  QColor penColor = resolved.color;
  penColor.setAlphaF(penColor.alphaF() *
                     (text.css.opacity >= 0.0 ? text.css.opacity : 1.0));
  const QString family = text.css.fontFamily.isEmpty()
                             ? scene.style.fontFamily
                             : text.css.fontFamily;
  const QString weight = text.css.fontWeight.trimmed().toLower();
  const bool bold = weight.isEmpty()
                        ? text.bold
                        : (weight == QLatin1String("bold") ||
                           weight == QLatin1String("bolder") ||
                           weight.toInt() >= 700);
  const QString fontStyle = text.css.fontStyle.trimmed().toLower();
  const bool italic = fontStyle.isEmpty()
                          ? text.italic
                          : fontStyle == QLatin1String("italic");
  auto font = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(family), fontSize);
  font.font.setWeight(bold ? QFont::Bold : QFont::Normal);
  font.font.setItalic(italic);
  const QFontMetricsF metrics(font.font);
  const qreal width = metrics.horizontalAdvance(text.text) * font.scale;
  qreal x = text.position.x();
  if (text.anchor == QLatin1String("middle")) x -= width / 2.0;
  else if (text.anchor == QLatin1String("end")) x -= width;
  const auto vertical = flowchart::flowLabelFontBoundingMetrics(
      family, fontSize, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  qreal baseline = text.position.y() +
      (text.baseline == TreemapTextBaseline::Middle
           ? vertical.xHeight / 2.0
           : vertical.ascent * 0.8);
  painter.save();
  if (!text.clip.isEmpty()) painter.setClipRect(text.clip);
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(penColor);
  painter.drawText(QPointF(0.0, 0.0), text.text);
  painter.restore();
}

} // namespace

void paintTreemapScene(const TreemapScene &scene, QPainter &painter,
                       const MermaidPaintOptions &) {
  TreemapPaintContext context;
  context.lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene.style.fontFamily),
      scene.style.rootFontSize);
  context.diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                     std::sqrt(2.0);
  drawText(painter, scene, scene.title);
  for (const auto &section : scene.sections) {
    if (section.depth == 0) continue;
    drawCssRect(painter, context, section.rect, section.rectCss, section.fill,
                section.fillOpacity, section.stroke, section.strokeWidth,
                section.strokeOpacity);
    drawText(painter, scene, section.label);
    drawText(painter, scene, section.value);
  }
  for (const auto &leaf : scene.leaves) {
    drawCssRect(painter, context, leaf.rect, leaf.rectCss, leaf.fill,
                leaf.fillOpacity, leaf.stroke, leaf.strokeWidth, 1.0);
    drawText(painter, scene, leaf.label);
    drawText(painter, scene, leaf.value);
  }
}

} // namespace muffin::mermaid::treemap
