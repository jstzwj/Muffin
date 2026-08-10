#include "mermaid/gantt/GanttScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace muffin::mermaid::gantt {
namespace {

QColor paintColor(const QString& value, const QColor& fallback = Qt::black) {
  return color::isParsableColor(value) ? color::toQColor(value) : fallback;
}

void drawRect(QPainter& painter, const GanttRectGeometry& rect) {
  painter.save();
  QColor fill = paintColor(rect.fill);
  fill.setAlphaF(std::clamp(fill.alphaF() * rect.opacity, 0.0, 1.0));
  painter.setBrush(fill);
  if (rect.strokeWidth > 0.0 && !rect.stroke.isEmpty() &&
      rect.stroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0)
    painter.setPen(QPen(paintColor(rect.stroke), rect.strokeWidth));
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

void drawLine(QPainter& painter, const GanttLineGeometry& line) {
  QColor color = paintColor(line.stroke);
  color.setAlphaF(std::clamp(color.alphaF() * line.opacity, 0.0, 1.0));
  painter.setPen(QPen(color, line.strokeWidth));
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(line.line);
}

void drawText(QPainter& painter, const GanttScene& scene,
              const GanttTextGeometry& text) {
  if (text.text.isEmpty() && text.lines.isEmpty()) return;
  editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
      scene.style.fontFamily, text.fontSize);
  if (!(font.scale > 0.0)) return;
  QFont qfont = font.font;
  qfont.setItalic(text.italic);
  if (text.bold) qfont.setWeight(QFont::Bold);
  QFontMetricsF metrics(qfont);
  const auto oneLine = [&](const QString& value, const QPointF& anchor) {
    qreal x = anchor.x();
    const qreal width = metrics.horizontalAdvance(value) * font.scale;
    if (text.anchor == GanttTextAnchor::Middle) x -= width / 2.0;
    else if (text.anchor == GanttTextAnchor::End) x -= width;
    painter.save();
    painter.translate(x, anchor.y());
    painter.scale(font.scale, font.scale);
    painter.setFont(qfont);
    painter.setPen(paintColor(text.fill));
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
  for (const auto& rect : scene.excludes) drawRect(painter, rect);
  for (const auto& line : scene.gridLines) drawLine(painter, line);
  for (const auto& text : scene.gridLabels) drawText(painter, scene, text);
  for (const auto& rect : scene.sections) drawRect(painter, rect);
  for (const auto& rect : scene.tasks) drawRect(painter, rect);
  for (const auto& text : scene.taskLabels) drawText(painter, scene, text);
  for (const auto& text : scene.sectionLabels) drawText(painter, scene, text);
  for (const auto& line : scene.todayLines) drawLine(painter, line);
  drawText(painter, scene, scene.titleGeometry);
  painter.restore();
}

}  // namespace muffin::mermaid::gantt
