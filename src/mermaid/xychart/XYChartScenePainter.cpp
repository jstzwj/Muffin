#include "mermaid/xychart/XYChartScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::xychart {
namespace {

QString visibleSvgText(QString text) {
  static const QRegularExpression whitespace(QStringLiteral(R"([\t\n\r\f ]+)"));
  text.replace(whitespace, QStringLiteral(" "));
  while (text.startsWith(QLatin1Char(' '))) text.remove(0, 1);
  while (text.endsWith(QLatin1Char(' '))) text.chop(1);
  return text;
}

QPen strokePen(const QString& value, qreal width) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Stroke, QColor(Qt::black));
  if (paint.none || width == 0.0)
    return QPen(Qt::NoPen);
  const qreal usedWidth = width > 0.0 && std::isfinite(width) ? width : 1.0;
  QPen pen(paint.color, usedWidth);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  pen.setMiterLimit(4.0);
  return pen;
}

QBrush fillBrush(const QString& value) {
  // An empty SVG fill declaration is invalid and inherits the root black
  // fill. It is not the `none` paint keyword.
  if (value.trimmed().isEmpty()) return QBrush(Qt::black);
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, QColor(Qt::black));
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

void drawText(QPainter& painter, const XYChartSceneStyle& style,
              const XYChartTextGeometry& text) {
  const QString visible = visibleSvgText(text.text);
  const qreal usedSize = text.fontSize < 0.0 || !std::isfinite(text.fontSize)
                             ? style.rootFontSize
                             : text.fontSize;
  if (visible.isEmpty() || !(usedSize > 0.0) ||
      !std::isfinite(text.position.x()) ||
      !std::isfinite(text.position.y()))
    return;
  const color::SvgPaint fill = text.fill.trimmed().isEmpty()
                                   ? color::SvgPaint{false, QColor(Qt::black)}
                                   : color::resolveSvgPaint(
                                         text.fill, color::SvgPaintKind::Text,
                                         QColor(Qt::black));
  if (fill.none) return;
  const editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(style.fontFamily, usedSize);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const qreal advance = metrics.horizontalAdvance(visible) * font.scale;
  qreal localX = 0.0;
  if (text.anchor == XYChartTextAnchor::Middle) localX -= advance / 2.0;
  else if (text.anchor == XYChartTextAnchor::End) localX -= advance;
  qreal localBaseline = 0.0;
  if (text.baseline == XYChartBaseline::BeforeEdge)
    localBaseline += metrics.ascent() * font.scale;
  else if (text.baseline == XYChartBaseline::Hanging)
    localBaseline += metrics.ascent() * font.scale * 0.8;
  else if (text.baseline == XYChartBaseline::Middle)
    localBaseline +=
        (metrics.ascent() - metrics.descent()) * font.scale / 2.0;
  painter.save();
  // SVG applies translate(position), then rotation. text-anchor and
  // dominant-baseline operate in the resulting local glyph coordinate space.
  painter.translate(text.position);
  painter.rotate(text.rotation);
  painter.translate(localX, localBaseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

}  // namespace

void paintXYChartScene(const XYChartScene& scene, QPainter& painter,
                       const MermaidPaintOptions&) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(fillBrush(scene.style.backgroundColor));
  painter.drawRect(scene.bounds);

  struct Drawable {
    int order = -1;
    const XYChartRectGeometry* rect = nullptr;
    const XYChartPathGeometry* path = nullptr;
    const XYChartTextGeometry* text = nullptr;
  };
  QVector<Drawable> drawables;
  drawables.reserve(scene.rects.size() + scene.paths.size() +
                    scene.texts.size());
  for (const auto& rect : scene.rects)
    drawables.append({rect.paintOrder, &rect, nullptr, nullptr});
  for (const auto& path : scene.paths)
    drawables.append({path.paintOrder, nullptr, &path, nullptr});
  for (const auto& text : scene.texts)
    drawables.append({text.paintOrder, nullptr, nullptr, &text});
  std::stable_sort(drawables.begin(), drawables.end(),
                   [](const Drawable& a, const Drawable& b) {
                     return a.order < b.order;
                   });

  for (const Drawable& drawable : drawables) {
    if (drawable.rect) {
      const XYChartRectGeometry& rect = *drawable.rect;
      painter.setPen(strokePen(rect.stroke, rect.strokeWidth));
      painter.setBrush(fillBrush(rect.fill));
      if (std::isfinite(rect.rect.x()) && std::isfinite(rect.rect.y()) &&
          std::isfinite(rect.rect.width()) && std::isfinite(rect.rect.height()))
        painter.drawRect(rect.rect);
    } else if (drawable.path) {
      const XYChartPathGeometry& path = *drawable.path;
      painter.setPen(strokePen(path.stroke, path.strokeWidth));
      painter.setBrush(fillBrush(path.fill));
      if (!path.path.isEmpty()) painter.drawPath(scene::parseSvgPath(path.path));
    } else if (drawable.text) {
      drawText(painter, scene.style, *drawable.text);
    }
  }
}

}  // namespace muffin::mermaid::xychart
