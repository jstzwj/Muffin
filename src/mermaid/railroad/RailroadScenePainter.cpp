#include "mermaid/railroad/RailroadScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/railroad/RailroadScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::railroad {
namespace {

QString visibleSvgText(QString text) {
  static const QRegularExpression whitespace(QStringLiteral("[\\t\\n\\f\\r ]+"));
  text.replace(whitespace, QStringLiteral(" "));
  return text.trimmed();
}

QStringList cssFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("monospace"));
  return result;
}

editor::CssPixelFont textFont(const RailroadScene& scene,
                              const RailroadPrimitive& primitive) {
  const QStringList families = cssFamilies(scene.config.fontFamily);
  editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
      families.first(), scene.config.fontSize);
  if (families.size() > 1) font.font.setFamilies(families);
  if (primitive.bold) font.font.setWeight(QFont::Bold);
  font.font.setItalic(primitive.italic);
  return font;
}

color::SvgPaint paintValue(const QString& value,
                           color::SvgPaintKind kind) {
  return color::resolveSvgPaint(value, kind, QColor(Qt::black));
}

void drawText(const RailroadScene& scene, QPainter& painter,
              const RailroadPrimitive& primitive) {
  if (primitive.text.isEmpty() || !(scene.config.fontSize > 0.0)) return;
  const color::SvgPaint fill = paintValue(
      primitive.fill, color::SvgPaintKind::Text);
  if (fill.none) return;
  const editor::CssPixelFont font = textFont(scene, primitive);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const QString visible = visibleSvgText(primitive.text);
  qreal x = primitive.position.x();
  if (primitive.middleAnchor)
    x -= metrics.horizontalAdvance(visible) * font.scale / 2.0;
  qreal y = primitive.position.y();
  if (primitive.baseline == RailroadTextBaseline::Middle)
    y += metrics.xHeight() * font.scale / 2.0;
  painter.save();
  painter.translate(primitive.translation);
  painter.translate(x, y);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

QPen primitivePen(const RailroadPrimitive& primitive) {
  const color::SvgPaint stroke = paintValue(
      primitive.stroke, color::SvgPaintKind::Stroke);
  if (stroke.none || primitive.strokeWidth == 0.0) return QPen(Qt::NoPen);
  const qreal width = primitive.strokeWidth > 0.0 &&
                              std::isfinite(primitive.strokeWidth)
                          ? primitive.strokeWidth
                          : 1.0;
  QPen pen(stroke.color, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  if (!primitive.dash.isEmpty()) {
    QVector<qreal> normalized;
    normalized.reserve(primitive.dash.size());
    for (qreal value : primitive.dash) normalized.append(value / width);
    pen.setDashPattern(normalized);
  }
  return pen;
}

QBrush primitiveBrush(const RailroadPrimitive& primitive) {
  const color::SvgPaint fill = paintValue(
      primitive.fill, color::SvgPaintKind::Fill);
  return fill.none ? QBrush(Qt::NoBrush) : QBrush(fill.color);
}

}  // namespace

void paintRailroadScene(const RailroadScene& scene, QPainter& painter,
                        const MermaidPaintOptions&) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const RailroadPrimitive& primitive : scene.primitives) {
    if (primitive.kind == RailroadPrimitiveKind::Text) {
      drawText(scene, painter, primitive);
      continue;
    }
    painter.save();
    painter.translate(primitive.translation);
    painter.setPen(primitivePen(primitive));
    painter.setBrush(primitiveBrush(primitive));
    switch (primitive.kind) {
      case RailroadPrimitiveKind::Rect:
        painter.drawRoundedRect(primitive.rect, primitive.rx, primitive.ry,
                                Qt::AbsoluteSize);
        break;
      case RailroadPrimitiveKind::Circle:
        painter.drawEllipse(primitive.rect);
        break;
      case RailroadPrimitiveKind::Path:
        painter.drawPath(primitive.path);
        break;
      case RailroadPrimitiveKind::Text:
        break;
    }
    painter.restore();
  }
}

}  // namespace muffin::mermaid::railroad
