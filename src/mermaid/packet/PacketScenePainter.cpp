#include "mermaid/packet/PacketScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::packet {
namespace {

QString visibleSvgText(QString text) {
  static const QRegularExpression whitespace(QStringLiteral(R"([\t\n\r\f ]+)"));
  text.replace(whitespace, QStringLiteral(" "));
  // SVG white-space:normal trims only CSS-collapsible ASCII whitespace.
  // NBSP and the other Unicode Zs separators remain visible glyph advances.
  while (!text.isEmpty() &&
         ((text.front().unicode() >= 0x0009 && text.front().unicode() <= 0x000d) ||
          text.front() == QLatin1Char(' ')))
    text.remove(0, 1);
  while (!text.isEmpty() &&
         ((text.back().unicode() >= 0x0009 && text.back().unicode() <= 0x000d) ||
          text.back() == QLatin1Char(' ')))
    text.chop(1);
  return text;
}

QStringList cssFontFamilies(const QString& expression) {
  QStringList families;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) families.append(family);
  }
  if (families.isEmpty()) families.append(QStringLiteral("Noto Sans"));
  return families;
}

editor::CssPixelFont textFont(const PacketScene& scene, qreal size) {
  const QStringList families = cssFontFamilies(scene.style.fontFamily);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  return font;
}

color::SvgPaint rootFill(const PacketScene& scene) {
  const QString value = scene.style.inheritedColor.trimmed();
  const QString lower = value.toLower();
  if (lower == QLatin1String("none")) return {true, {}};
  if (lower == QLatin1String("transparent"))
    return {false, QColor(Qt::transparent)};
  if (lower == QLatin1String("currentcolor") ||
      lower == QLatin1String("initial") ||
      lower == QLatin1String("inherit") || lower == QLatin1String("unset") ||
      lower == QLatin1String("revert") || value.isEmpty() ||
      !color::isParsableColor(value))
    return {false, QColor(Qt::black)};
  return {false, color::toQColor(value)};
}

color::SvgPaint textPaint(const PacketScene& scene, const QString& value) {
  const color::SvgPaint root = rootFill(scene);
  const QString trimmed = value.trimmed();
  const QString lower = trimmed.toLower();
  if (lower == QLatin1String("none")) return {true, {}};
  if (lower == QLatin1String("currentcolor") ||
      lower == QLatin1String("initial"))
    return {false, QColor(Qt::black)};
  if (trimmed.isEmpty() || lower == QLatin1String("inherit") ||
      lower == QLatin1String("unset") || lower == QLatin1String("revert") ||
      !color::isParsableColor(trimmed))
    return root;
  return {false, color::toQColor(trimmed)};
}

QColor inheritedColor(const PacketScene& scene) {
  const color::SvgPaint root = rootFill(scene);
  return root.none ? QColor(Qt::black) : root.color;
}

void drawPacketText(const PacketScene& scene, QPainter& painter,
                    const PacketTextGeometry& text) {
  const QString visible = visibleSvgText(text.text);
  if (visible.isEmpty() || !(text.fontSize > 0.0) ||
      !std::isfinite(text.position.x()) || !std::isfinite(text.position.y()))
    return;
  const color::SvgPaint paint = textPaint(scene, text.fill);
  if (paint.none) return;
  const editor::CssPixelFont font = textFont(scene, text.fontSize);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const qreal advance = metrics.horizontalAdvance(visible) * font.scale;
  qreal x = 0.0;
  if (text.anchor == PacketTextAnchor::Middle) x = -advance / 2.0;
  else if (text.anchor == PacketTextAnchor::End) x = -advance;
  qreal y = 0.0;
  if (text.baseline == PacketTextBaseline::Middle)
    y = metrics.xHeight() * font.scale / 2.0;
  painter.save();
  painter.translate(text.position.x() + x, text.position.y() + y);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(paint.color);
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

QPen strokePen(const PacketScene& scene, const PacketBlockGeometry& block) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      block.stroke, color::SvgPaintKind::Stroke, inheritedColor(scene));
  if (paint.none || block.strokeWidth == 0.0) return QPen(Qt::NoPen);
  const qreal width = block.strokeWidth > 0.0 && std::isfinite(block.strokeWidth)
                          ? block.strokeWidth
                          : 1.0;
  QPen pen(paint.color, width);
  pen.setJoinStyle(Qt::MiterJoin);
  pen.setCapStyle(Qt::FlatCap);
  return pen;
}

QBrush fillBrush(const PacketScene& scene, const PacketBlockGeometry& block) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      block.fill, color::SvgPaintKind::Fill, inheritedColor(scene));
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

}  // namespace

void paintPacketScene(const PacketScene& scene, QPainter& painter,
                      const MermaidPaintOptions&) {
  painter.save();
  // Mermaid's packet SVG has overflow:hidden. The fixed viewBox deliberately
  // clips long labels/title rather than expanding to their text bbox.
  painter.setClipRect(scene.bounds, Qt::IntersectClip);
  for (const PacketWordGeometry& word : scene.words) {
    for (const PacketBlockGeometry& block : word.blocks) {
      if (std::isfinite(block.rect.x()) && std::isfinite(block.rect.y()) &&
          std::isfinite(block.rect.width()) && std::isfinite(block.rect.height()) &&
          block.rect.width() > 0.0 && block.rect.height() > 0.0) {
        painter.setPen(strokePen(scene, block));
        painter.setBrush(fillBrush(scene, block));
        painter.drawRect(block.rect);
      }
      drawPacketText(scene, painter, block.labelText);
      for (const PacketTextGeometry& bit : block.bitTexts)
        drawPacketText(scene, painter, bit);
    }
  }
  drawPacketText(scene, painter, scene.titleText);
  painter.restore();
}

}  // namespace muffin::mermaid::packet
