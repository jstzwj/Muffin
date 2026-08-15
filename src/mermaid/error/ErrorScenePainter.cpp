#include "mermaid/error/ErrorScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/error/ErrorScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace muffin::mermaid::error {
namespace {

QStringList fontFamilies(const QString& expression) {
  QStringList result;
  for (QString item : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    item = item.trimmed();
    if (item.size() >= 2 &&
        ((item.front() == QLatin1Char('"') && item.back() == QLatin1Char('"')) ||
         (item.front() == QLatin1Char('\'') && item.back() == QLatin1Char('\''))))
      item = item.mid(1, item.size() - 2);
    if (!item.isEmpty()) result.append(item);
  }
  if (result.isEmpty()) result.append(QStringLiteral("serif"));
  return result;
}

// QFont family matching on the Windows/DirectWrite backend is effectively
// case-sensitive for application fonts: the default theme's lowercase stack
// ("trebuchet ms") resolves to a substitute face with ~2% wider metrics
// (QFontInfo::exactMatch() == false) while the canonical spelling
// exact-matches. Resolve each family against the database spelling before
// building the QFont. The map is built once per process.
QString canonicalFamily(const QString& family) {
  static const QHash<QString, QString> cache = [] {
    QHash<QString, QString> map;
    const QStringList families = QFontDatabase::families();
    for (const QString& name : families) map.insert(name.toLower(), name);
    return map;
  }();
  return cache.value(family.toLower(), family);
}

QFont::Weight cssWeight(const QString& weight) {
  const QString value = weight.trimmed().toLower();
  if (value == QLatin1String("bold") || value == QLatin1String("bolder"))
    return QFont::Bold;
  bool ok = false;
  const int numeric = value.toInt(&ok);
  if (ok && numeric >= 700) return QFont::Bold;
  return QFont::Normal;
}

// Chromium's getComputedTextLength parity for the centered anchor: shape in
// OpenType design units when the face is available (the same helper the
// eventmodeling family uses for its literal trebuchet measurement), else
// unhinted QFontMetricsF.
qreal textAdvance(const QString& text, const QString& firstFamily,
                  const QFont& font, qreal fontScale, qreal pixelSize,
                  QFont::Weight weight) {
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = weight;
  const auto design = flowchart::measureOpenTypeDesignAdvance(
      document, firstFamily, pixelSize);
  if (design) return *design;
  return QFontMetricsF(font).horizontalAdvance(text) * fontScale;
}

QColor withOpacity(QColor color, qreal opacity) {
  color.setAlphaF(color.alphaF() * qBound(0.0, opacity, 1.0));
  return color;
}

}  // namespace

void paintErrorScene(const ErrorScene& scene, QPainter& painter) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const qreal scale = scene.viewBoxBounds.width() > 0.0
                          ? scene.bounds.width() / scene.viewBoxBounds.width()
                          : 1.0;
  painter.save();
  painter.scale(scale, scale);

  // Per-path resolved CSS: the six icons are sibling DOM elements, and
  // structural selectors (:nth-of-type / adjacent sibling) can style them
  // individually — each path carries its own fill/stroke/opacity/visibility.
  const ErrorIconCss iconDefault;
  for (int index = 0; index < scene.iconPaths.size(); ++index) {
    const ErrorIconCss& css = index < scene.css.icons.size()
                                  ? scene.css.icons.at(index) : iconDefault;
    if (!css.visible) continue;
    const auto fill = color::resolveSvgPaint(
        css.fill.isEmpty() ? scene.style.errorBkgColor : css.fill,
        color::SvgPaintKind::Fill, Qt::black);
    const auto stroke = color::resolveSvgPaint(
        css.stroke, color::SvgPaintKind::Stroke, Qt::black);
    if (fill.none && stroke.none) continue;
    // SVG paints BOTH channels: fill plus a stroked outline (initial
    // stroke:none upstream — a themeCSS `path { stroke: … }` activates it).
    if (stroke.none || css.strokeWidthPx <= 0.0) {
      painter.setPen(Qt::NoPen);
    } else {
      painter.setPen(QPen(withOpacity(stroke.color, css.opacity),
                          css.strokeWidthPx));
    }
    painter.setBrush(fill.none
                         ? Qt::NoBrush
                         : QBrush(withOpacity(fill.color, css.opacity)));
    painter.drawPath(scene.iconPaths.at(index));
  }

  const auto drawText = [&](const ErrorTextGeometry& geometry,
                            const ErrorTextCss& css) {
    if (!css.visible) return;
    const QString familyExpression =
        css.fontFamily.trimmed().isEmpty() ? scene.style.fontFamily
                                           : css.fontFamily;
    QStringList families = fontFamilies(familyExpression);
    for (QString& family : families) family = canonicalFamily(family);
    const qreal size = css.fontSize >= 0.0 ? css.fontSize : geometry.fontSize;
    if (!(size > 0.0)) return;
    const QFont::Weight weight = cssWeight(css.fontWeight);
    auto font = editor::makeUnhintedCssPixelFont(families.first(), size);
    // Only attach the CSS fallback list when the canonical first family did
    // not exact-match: Qt's setFamilies() resolution takes a different
    // matching path than the single-family constructor and can pick a
    // substitute face with ~2% wider glyphs even when the first family is
    // installed (probed: QFont("Trebuchet MS") exact vs setFamilies({"Trebuchet
    // MS", ...}) not exact). Qt's per-script engine fallback still covers
    // missing glyphs without the explicit list.
    if (families.size() > 1 && !QFontInfo(font.font).exactMatch())
      font.font.setFamilies(families);
    if (weight != QFont::Normal) font.font.setWeight(weight);
    const auto fill = color::resolveSvgPaint(
        css.fill.isEmpty() ? scene.style.errorTextColor : css.fill,
        color::SvgPaintKind::Text, Qt::black);
    const auto stroke = color::resolveSvgPaint(
        css.stroke.isEmpty() ? scene.style.errorTextColor : css.stroke,
        color::SvgPaintKind::Stroke, Qt::black);
    if (fill.none && stroke.none) return;
    const qreal advance = textAdvance(geometry.text, families.first(),
                                      font.font, font.scale, size, weight);
    painter.save();
    painter.scale(font.scale, font.scale);
    painter.setFont(font.font);
    // text-anchor:middle at x, alphabetic baseline at y (dominant-baseline
    // auto) — QPainter's drawText(QPointF) origin is the left baseline.
    // SVG text paints fill AND stroke as separate channels (`.error-text`
    // declares both); draw the fill through the text engine, then stroke the
    // glyph outlines on top with the resolved stroke width (initial 1px).
    const QPointF origin((geometry.anchor.x() - advance / 2.0) / font.scale,
                         geometry.anchor.y() / font.scale);
    if (!fill.none) {
      painter.setPen(QPen(withOpacity(fill.color, css.opacity)));
      painter.drawText(origin, geometry.text);
    }
    if (!stroke.none && css.strokeWidthPx > 0.0) {
      QPainterPath outlined;
      outlined.addText(origin, font.font, geometry.text);
      // Pen width is expressed in the font.scale-scaled local space; divide
      // it back so the rendered width is strokeWidthPx viewBox units.
      painter.setPen(QPen(withOpacity(stroke.color, css.opacity),
                          css.strokeWidthPx / font.scale));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(outlined);
    }
    painter.restore();
  };
  drawText(scene.headline, scene.css.headline);
  drawText(scene.version, scene.css.version);

  painter.restore();
}

void ErrorScene::paint(QPainter& painter,
                       const MermaidPaintOptions&) const {
  paintErrorScene(*this, painter);
}

}  // namespace muffin::mermaid::error
