#include "mermaid/railroad/RailroadScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/railroad/RailroadScene.h"
#include "mermaid/text/LabelText.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::railroad {
namespace {

// themeCSS helpers: css channel or primitive base fallback.
bool cssVisible(const RailroadElementCss& css) { return css.visible; }
QString cssFamily(const RailroadElementCss& css, const QString& base) {
  return css.fontFamily.trimmed().isEmpty() ? base : css.fontFamily;
}
qreal cssSize(const RailroadElementCss& css, qreal base) {
  return css.fontSize >= 0.0 ? css.fontSize : base;
}
bool cssBold(const RailroadElementCss& css, bool base) {
  const QString weight = css.fontWeight.trimmed().toLower();
  if (weight.isEmpty()) return base;
  return weight == QLatin1String("bold") || weight == QLatin1String("bolder") ||
         weight.toInt() >= 700;
}
qreal cssOpacity(const RailroadElementCss& css) {
  return css.opacity >= 0.0 ? css.opacity : 1.0;
}
QColor withOpacity(QColor color, qreal opacity) {
  color.setAlphaF(color.alphaF() * qBound(0.0, opacity, 1.0));
  return color;
}
qreal cssStrokeWidthPx(const RailroadElementCss& css, qreal base,
                       const CssLengthContext& lengths, qreal diagonal) {
  return editor::cssStrokeWidthPx(
      css.strokeWidth.trimmed().isEmpty()
          ? QString::number(base) + QStringLiteral("px")
          : css.strokeWidth,
      lengths, diagonal);
}

editor::CssPixelFont textFont(const RailroadScene& scene,
                              const RailroadPrimitive& primitive) {
  QStringList families =
      text::cssFontFamilies(cssFamily(primitive.css, scene.config.fontFamily));
  if (families.isEmpty()) families.append(QStringLiteral("monospace"));
  editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
      families.first(), cssSize(primitive.css, scene.config.fontSize));
  if (families.size() > 1) font.font.setFamilies(families);
  if (cssBold(primitive.css, primitive.bold)) font.font.setWeight(QFont::Bold);
  const QString style = primitive.css.fontStyle.trimmed().toLower();
  font.font.setItalic(style.isEmpty()
                          ? primitive.italic
                          : style == QLatin1String("italic"));
  return font;
}

color::SvgPaint paintValue(const QString& value,
                           color::SvgPaintKind kind) {
  return color::resolveSvgPaint(value, kind, QColor(Qt::black));
}



void drawText(const RailroadScene& scene, QPainter& painter,
              const RailroadPrimitive& primitive) {
  if (!cssVisible(primitive.css)) return;
  if (primitive.text.isEmpty() ||
      !(cssSize(primitive.css, scene.config.fontSize) > 0.0))
    return;
  const color::SvgPaint fill = paintValue(
      primitive.fill, color::SvgPaintKind::Text);
  if (fill.none) return;
  const editor::CssPixelFont font = textFont(scene, primitive);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const QString visible = text::collapsedSvgText(primitive.text);
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
  painter.setPen(withOpacity(fill.color, cssOpacity(primitive.css)));
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

QPen primitivePen(const RailroadPrimitive& primitive,
                  const CssLengthContext& lengths, qreal diagonal) {
  const color::SvgPaint stroke = paintValue(
      primitive.stroke, color::SvgPaintKind::Stroke);
  if (stroke.none || primitive.strokeWidth == 0.0) return QPen(Qt::NoPen);
  const qreal width = cssStrokeWidthPx(primitive.css, primitive.strokeWidth,
                                       lengths, diagonal);
  if (!(width > 0.0)) return QPen(Qt::NoPen);
  QPen pen(withOpacity(stroke.color, cssOpacity(primitive.css)), width);
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
  return fill.none ? QBrush(Qt::NoBrush)
                   : QBrush(withOpacity(fill.color,
                                        cssOpacity(primitive.css)));
}

}  // namespace

void paintRailroadScene(const RailroadScene& scene, QPainter& painter,
                        const MermaidPaintOptions&) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const CssLengthContext lengths = editor::pieCssLengthContext(
      scene.config.fontFamily, scene.config.fontSize);
  const qreal diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                         std::sqrt(2.0);
  for (const RailroadPrimitive& primitive : scene.primitives) {
    if (primitive.kind == RailroadPrimitiveKind::Text) {
      drawText(scene, painter, primitive);
      continue;
    }
    if (!cssVisible(primitive.css)) continue;
    painter.save();
    painter.translate(primitive.translation);
    painter.setPen(primitivePen(primitive, lengths, diagonal));
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
