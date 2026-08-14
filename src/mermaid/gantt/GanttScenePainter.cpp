#include "mermaid/gantt/GanttScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::gantt {
namespace {

QColor paintColor(const QString& value, const QColor& fallback = Qt::black) {
  return color::isParsableColor(value) ? color::toQColor(value) : fallback;
}

// Shared themeCSS paint context: em units resolve against the root font and
// percentages against the viewBox diagonal (hypot/sqrt2), like every other
// family.
struct GanttPaintContext {
  CssLengthContext lengths;
  qreal diagonal = 1.0;
};

qreal cssStrokeWidth(const GanttElementCss& css, qreal base,
                     const GanttPaintContext& context) {
  if (css.strokeWidth.trimmed().isEmpty()) return base;
  return editor::cssStrokeWidthPx(css.strokeWidth, context.lengths,
                                  context.diagonal);
}

void drawRect(QPainter& painter, const GanttRectGeometry& rect,
              const GanttPaintContext& context) {
  if (!rect.css.visible) return;
  painter.save();
  QColor fill = paintColor(rect.css.fill.isEmpty() ? rect.fill : rect.css.fill);
  const qreal opacity = rect.css.opacity >= 0.0 ? rect.css.opacity : rect.opacity;
  fill.setAlphaF(std::clamp(fill.alphaF() * opacity, 0.0, 1.0));
  painter.setBrush(fill);
  const QString stroke = rect.css.stroke.isEmpty() ? rect.stroke : rect.css.stroke;
  const qreal strokeWidth =
      cssStrokeWidth(rect.css, rect.strokeWidth, context);
  if (strokeWidth > 0.0 && !stroke.isEmpty() &&
      stroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0)
    painter.setPen(QPen(paintColor(stroke), strokeWidth));
  else
    painter.setPen(Qt::NoPen);

  if (rect.milestone) {
    painter.translate(rect.transformOrigin);
    painter.rotate(45.0);
    painter.scale(0.8, 0.8);
    const QRectF local = rect.rect.translated(-rect.transformOrigin);
    painter.drawRoundedRect(local, rect.radius, rect.radius);
  } else {
    painter.drawRoundedRect(rect.rect, rect.radius, rect.radius);
  }
  painter.restore();
}

void drawLine(QPainter& painter, const GanttLineGeometry& line,
              const GanttPaintContext& context) {
  if (!line.css.visible) return;
  const qreal strokeWidth = cssStrokeWidth(line.css, line.strokeWidth, context);
  if (strokeWidth <= 0.0) return;
  QColor color = paintColor(line.css.stroke.isEmpty() ? line.stroke : line.css.stroke);
  const qreal opacity = line.css.opacity >= 0.0 ? line.css.opacity : line.opacity;
  color.setAlphaF(std::clamp(color.alphaF() * opacity, 0.0, 1.0));
  painter.setPen(QPen(color, strokeWidth));
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(line.line);
}

GanttTextAnchor cssAnchor(const QString& value, GanttTextAnchor fallback) {
  const QString anchor = value.trimmed().toLower();
  if (anchor == QLatin1String("middle")) return GanttTextAnchor::Middle;
  if (anchor == QLatin1String("end")) return GanttTextAnchor::End;
  if (anchor == QLatin1String("start")) return GanttTextAnchor::Start;
  return fallback;
}

void drawText(QPainter& painter, const GanttScene& scene,
              const GanttTextGeometry& text) {
  if (!text.css.visible) return;
  if (text.text.isEmpty() && text.lines.isEmpty()) return;
  const QString family = text.css.fontFamily.isEmpty() ? scene.style.fontFamily
                                                       : text.css.fontFamily;
  const qreal size = text.css.fontSize >= 0.0 ? text.css.fontSize : text.fontSize;
  editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(family, size);
  if (!(font.scale > 0.0)) return;
  QFont qfont = font.font;
  const QString weight = text.css.fontWeight.trimmed().toLower();
  qfont.setWeight(weight == QLatin1String("bold") || weight == QLatin1String("bolder") ||
                          (weight.toInt() >= 700)
                      ? QFont::Bold
                      : QFont::Normal);
  if (text.bold) qfont.setWeight(QFont::Bold);
  const QString fontStyle = text.css.fontStyle.trimmed().toLower();
  qfont.setItalic(fontStyle.isEmpty() ? text.italic
                                      : fontStyle == QLatin1String("italic"));
  const GanttTextAnchor anchor = cssAnchor(text.css.textAnchor, text.anchor);
  QFontMetricsF metrics(qfont);
  QColor pen = paintColor(text.css.fill.isEmpty() ? text.fill : text.css.fill);
  const qreal opacity = text.css.opacity >= 0.0 ? text.css.opacity : text.opacity;
  pen.setAlphaF(std::clamp(pen.alphaF() * opacity, 0.0, 1.0));
  const auto oneLine = [&](const QString& value, const QPointF& at) {
    qreal x = at.x();
    const qreal width = metrics.horizontalAdvance(value) * font.scale;
    if (anchor == GanttTextAnchor::Middle) x -= width / 2.0;
    else if (anchor == GanttTextAnchor::End) x -= width;
    painter.save();
    painter.translate(x, at.y());
    painter.scale(font.scale, font.scale);
    painter.setFont(qfont);
    painter.setPen(pen);
    painter.drawText(QPointF(0.0, 0.0), value);
    painter.restore();
  };
  if (text.lines.isEmpty()) {
    oneLine(text.text, text.position);
    return;
  }
  const qreal dy = -(text.lines.size() - 1) / 2.0 * text.lineStep;
  const qreal centralBaseline = (metrics.ascent() - metrics.descent()) *
                                font.scale / 2.0;
  for (qsizetype i = 0; i < text.lines.size(); ++i)
    oneLine(text.lines.at(i),
            QPointF(text.position.x(), text.position.y() + dy +
                    i * text.lineStep + centralBaseline));
}

}  // namespace

void paintGanttScene(const GanttScene& scene, QPainter& painter,
                     const MermaidPaintOptions& options) {
  Q_UNUSED(options);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setClipRect(scene.bounds);
  GanttPaintContext context;
  context.lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene.style.fontFamily), scene.style.rootFontSize);
  context.diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                     std::sqrt(2.0);
  for (const auto& rect : scene.excludes) drawRect(painter, rect, context);
  for (const auto& line : scene.gridLines) drawLine(painter, line, context);
  for (const auto& text : scene.gridLabels) drawText(painter, scene, text);
  for (const auto& rect : scene.sections) drawRect(painter, rect, context);
  for (const auto& rect : scene.tasks) drawRect(painter, rect, context);
  for (const auto& text : scene.taskLabels) drawText(painter, scene, text);
  for (const auto& text : scene.sectionLabels) drawText(painter, scene, text);
  for (const auto& line : scene.todayLines) drawLine(painter, line, context);
  drawText(painter, scene, scene.titleGeometry);
  painter.restore();
}

}  // namespace muffin::mermaid::gantt
