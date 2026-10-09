#include "render/DecorationPainter.h"

#include "render/Blur.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssValueParser.h"
#include "render/Filter.h"
#include "render/GradientPainter.h"
#include "theme/ThemeDefinition.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTransform>
#include <QRectF>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QtMath>

#include <memory>
#include <cmath>

namespace muffin {
namespace DecorationPainter {

namespace {

// Process-wide SVG cache keyed by SVG byte data (content-addressed; themes share).
std::shared_ptr<QSvgRenderer> svgIcon(const QByteArray& data) {
  static QHash<QByteArray, std::shared_ptr<QSvgRenderer>> cache;
  if (data.isEmpty()) {
    return nullptr;
  }
  const auto it = cache.constFind(data);
  if (it != cache.constEnd()) {
    return *it;
  }
  auto r = std::make_shared<QSvgRenderer>(data);
  if (!r->isValid()) {
    r.reset();
  }
  cache.insert(data, r);
  return r;
}

// Render an SVG as an alpha mask tinted with `tint`, into a tile of `size` (px).
// Mask semantics: the SVG's alpha (its shape) becomes the tint's coverage, so a
// `mask-image: url(svg)` declaration paints the SVG shape in the ::before's
// background-colour. Returns a null image when the SVG is invalid or the size is
// degenerate. Shared by paintIcon (icon recolour) and paintWriteTexture (page
// texture tiling) — both are the same "alpha mask + tint" recipe at heart.
QImage renderMaskTile(const QByteArray& svgData, const QColor& tint, QSize size) {
  const auto icon = svgIcon(svgData);
  if (!icon || !tint.isValid() || size.width() <= 0 || size.height() <= 0) {
    return QImage();
  }
  QImage shape(size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
  shape.fill(Qt::transparent);
  {
    QPainter sp(&shape);
    sp.setRenderHint(QPainter::SmoothPixmapTransform, true);
    icon->render(&sp, QRectF(0, 0, size.width(), size.height()));
  }
  QImage out(size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  {
    QPainter op(&out);
    op.fillRect(out.rect(), tint);
    op.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    op.drawImage(0, 0, shape);
  }
  return out;
}

const ElementBackground* elementBackground(const RenderTheme& theme, const QString& host) {
  for (const ElementBackground& eb : theme.decorations().backgrounds) {
    if (eb.host == host) {
      return &eb;
    }
  }
  return nullptr;
}

const PseudoElementRule* pseudoRule(const RenderTheme& theme, const QString& host, const QString& pseudo) {
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == host && r.pseudo == pseudo) {
      return &r;
    }
  }
  return nullptr;
}

const HoverEffect* hoverEffectFor(const RenderTheme& theme, const QString& host) {
  for (const HoverEffect& he : theme.decorations().hoverEffects) {
    if (he.host == host) {
      return &he;
    }
  }
  return nullptr;
}

qreal pseudoUsedLengthImpl(const PseudoElementRule& rule, const QString& property, qreal basis, qreal fallback, qreal zoom) {
  if (rule.computed) {
    const auto length = rule.computed->length(property);
    if (length.status == CssLengthStatus::Valid) return length.px * zoom + length.fraction * basis;
  }
  return fallback * zoom;
}

QRectF positionedPseudo(const PseudoElementRule& rule, QRectF host, QRectF box, qreal zoom) {
  if (!rule.computed) return box;
  const auto& style = *rule.computed;
  const auto valid = [&](const char* name) { return style.length(QLatin1String(name)).status == CssLengthStatus::Valid; };
  if (valid("left")) box.moveLeft(host.left() + pseudoUsedLengthImpl(rule, "left", host.width(), 0, zoom));
  else if (valid("right")) box.moveRight(host.right() - pseudoUsedLengthImpl(rule, "right", host.width(), 0, zoom));
  if (valid("top")) box.moveTop(host.top() + pseudoUsedLengthImpl(rule, "top", host.height(), 0, zoom));
  else if (valid("bottom")) box.moveBottom(host.bottom() - pseudoUsedLengthImpl(rule, "bottom", host.height(), 0, zoom));
  const CssLengthContext context{style.fontSizePx * style.textScale, style.rootFontSizePx * style.textScale,
                               style.fontSizePx * .5, style.fontSizePx * .5, style.viewportPx};
  static const QRegularExpression translations(QStringLiteral(R"(translate(x|y)?\(([^()]*(?:\([^()]*\)[^()]*)*)\))"));
  auto matches = translations.globalMatch(style.resolvedValue("transform").toLower());
  while (matches.hasNext()) {
    const auto match = matches.next();
    auto arguments = match.captured(2); arguments.replace(',', ' ');
    const auto parts = splitTopLevelSpaces(arguments);
    if (parts.isEmpty()) continue;
    const auto used = [&](const QString& value, qreal basis) {
      const auto length = parseCssLengthPercentage(QStringView(value), context);
      return length.status == CssLengthStatus::Valid ? length.px * zoom + length.fraction * basis : 0;
    };
    if (match.captured(1) == "y") box.translate(0, used(parts[0], box.height()));
    else {
      const qreal y = match.captured(1).isEmpty() && parts.size() > 1 ? used(parts[1], box.height()) : 0;
      box.translate(used(parts[0], box.width()), y);
    }
  }
  return box;
}

void paintPseudoIcon(QPainter& painter, const PseudoElementRule& rule, QRectF box, QColor tint, qreal zoom) {
  QRectF image = box;
  if (rule.svgFromMask && rule.computed) {
    const auto& style = *rule.computed;
    auto size = style.resolvedValue("mask-size");
    if (size.isEmpty()) size = style.resolvedValue("-webkit-mask-size");
    const auto parts = splitTopLevelSpaces(size);
    const CssLengthContext context{style.fontSizePx * style.textScale, style.rootFontSizePx * style.textScale,
                                 style.fontSizePx * .5, style.fontSizePx * .5, style.viewportPx};
    if (!parts.isEmpty()) {
      const auto used = [&](const QString& value, qreal basis) {
        const auto length = parseCssLengthPercentage(QStringView(value), context);
        return length.status == CssLengthStatus::Valid ? length.px * zoom + length.fraction * basis : basis;
      };
      image.setWidth(used(parts[0], box.width()));
      image.setHeight(used(parts.size() > 1 ? parts[1] : "auto", box.height()));
    }
    auto position = style.resolvedValue("mask-position");
    if (position.isEmpty()) position = style.resolvedValue("-webkit-mask-position");
    if (position.contains("center")) image.moveCenter(box.center());
    else {
      if (position.contains("right")) image.moveRight(box.right());
      if (position.contains("bottom")) image.moveBottom(box.bottom());
    }
  }
  painter.save(); painter.setClipRect(box, Qt::IntersectClip);
  paintIcon(painter, rule.svgData, image, tint, rule.svgFromMask);
  painter.restore();
}

}  // namespace

qreal pseudoUsedLength(const PseudoElementRule& rule, const QString& property, qreal basis, qreal fallback, qreal zoom) {
  return pseudoUsedLengthImpl(rule, property, basis, fallback, zoom);
}

void paintIcon(QPainter& painter, const QByteArray& svgData, const QRectF& target, const QColor& tint, bool recolour) {
  const auto icon = svgIcon(svgData);
  if (!icon) {
    return;
  }
  if (!recolour || !tint.isValid()) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    icon->render(&painter, target);
    painter.restore();
    return;
  }
  // Mask recolour: render the SVG as an alpha mask tinted with `tint`, then blit.
  // (phycat's mask icons carry no fill of their own — only shape.)
  const QSize size(qMax(1, int(qCeil(target.width()))), qMax(1, int(qCeil(target.height()))));
  const QImage tile = renderMaskTile(svgData, tint, size);
  if (tile.isNull()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(target, tile);
  painter.restore();
}

void paintPseudoIconBox(QPainter& painter, const PseudoElementRule& rule, const QRectF& box, const QColor& tint, qreal zoom) {
  paintPseudoIcon(painter, rule, box, tint, zoom);
}

bool hasElementBackground(const RenderTheme& theme, const QString& host) {
  const ElementBackground* eb = elementBackground(theme, host);
  return eb && eb->gradient.kind != GradientSpec::Kind::None;
}

void paintHrGradient(QPainter& painter, const RenderTheme& theme, const QRectF& rect) {
  const ElementBackground* eb = elementBackground(theme, QStringLiteral("hr"));
  if (!eb) {
    return;
  }
  const qreal h = qMax<qreal>(2.0, rect.height() * 0.08);
  const QRectF bar(rect.left(), rect.center().y() - h / 2.0, rect.width(), h);
  painter.save();
  painter.fillRect(bar, GradientPainter::makeBrush(eb->gradient, bar, theme.zoomPercent() / 100.0));
  painter.restore();
}

void paintShapeBox(QPainter& painter, const PseudoElementRule& rule, QRectF box) {
  if (box.width() <= 0.0 || box.height() <= 0.0) {
    return;
  }
  // border-radius % is relative to the box (50% → circle), not em; clamp to half
  // the smaller side so a declared 50% rounds into a disc regardless of emPx.
  const qreal r = qBound(0.0, rule.borderRadius, qMin(box.width(), box.height()) / 2.0);
  painter.save();
  painter.setOpacity(rule.opacity);
  if (rule.backgroundColor.isValid()) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(rule.backgroundColor);
    painter.drawRoundedRect(box, r, r);
  }
  if (rule.borderWidth > 0.0 && rule.borderColor.isValid()) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(rule.borderColor, rule.borderWidth));
    painter.drawRoundedRect(box, r, r);
  }
  painter.restore();
}

void paintPseudoDecorations(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect, const PaintContext& ctx) {
  const bool isHeading = ctx.headingLevel >= 1 && ctx.headingLevel <= 6;
  const qreal em = ctx.font.pointSizeF() * 96.0 / 72.0;
  const qreal zoom = theme.zoomPercent() / 100.0;
  const qreal vCenter = ctx.textBounds.isValid() ? ctx.textBounds.center().y() : rect.center().y();

  if (const PseudoElementRule* before = pseudoRule(theme, host, QStringLiteral("before"))) {
    if (isHeading) {
      if (before->absolute) {
        // position:absolute left bar (phycat h3): anchored to the heading padding
        // box (rect.left), vertically centred on the text line. Resolve width/
        // height against the HOST rect when the CSS used a `%` (phycat's `height:
        // 61%` is 61% of the rendered heading, not 0.61em — the map-time value in
        // `size` is em-relative and made the bar too short).
        const qreal w = pseudoUsedLength(*before, "width", rect.width(), before->size.width() > 0 ? before->size.width() : 4, zoom);
        const qreal h = pseudoUsedLength(*before, "height", rect.height(), before->size.height() > 0 ? before->size.height() : em / zoom, zoom);
        paintShapeBox(painter, *before, positionedPseudo(*before, rect, QRectF(rect.left(), vCenter - h / 2, w, h), zoom));
      }
    } else if (!before->content.isEmpty() && host == QStringLiteral("blockquote")) {
      // Honor CSS geometry: position:absolute left/top anchor the glyph and
      // font-size scales it (phycat's ✨ at left:16px/top:18px/font-size:20px).
      // Falls back to the legacy inset (left+4, baseline+2, host font) when the
      // theme declared no positioning, preserving prior behaviour.
      QFont f = ctx.font;
      if (before->fontSizePx > 0.0) {
        f.setPointSizeF(before->fontSizePx * 72.0 / 96.0);
      }
      const QFontMetricsF m(f);
      const qreal x = rect.left() + (before->absolute ? before->insets.left() : 4.0);
      const qreal y = rect.top() + (before->insetsTop >= 0.0 ? before->insetsTop : m.ascent() + 2.0);
      painter.save();
      painter.setFont(f);
      painter.setPen(before->color.isValid() ? before->color : theme.textColor());
      painter.drawText(QPointF(x, y), before->content);
      painter.restore();
    }
  }

  if (const PseudoElementRule* after = pseudoRule(theme, host, QStringLiteral("after"))) {
    if (after->absolute && isHeading &&
        (after->background.kind != GradientSpec::Kind::None || after->backgroundColor.isValid() ||
         (after->borderBottomColor.isValid() && after->borderBottomWidth > 0.0))) {
      // ::after underline bar. Width/height come from the rule (e.g. Whitey's
      // h2::after border-bottom: 100px centred; phycat's h1::after gradient bar).
      const qreal borderW = after->borderBottomWidth > 0.0 ? after->borderBottomWidth : 0.0;
      const qreal barH = pseudoUsedLength(*after, "height", rect.height(), after->size.height() > 0 ? after->size.height() : qMax<qreal>(2, borderW), zoom);
      qreal barW = pseudoUsedLength(*after, "width", rect.width(), after->size.width() > 0 ? after->size.width() :
                               (ctx.textBounds.isValid() ? ctx.textBounds.width() : rect.width()) / zoom, zoom);
      // Hover widens the bar toward its :hover width (phycat h1::after 40px → 100%),
      // animated by the HoverAnimator phase. Focus widens it toward its :focus
      // width next (same recipe, FocusAnimator phase). The centred anchor (textMid,
      // below) keeps it growing symmetrically from the middle, matching the reference.
      if (!after->hoverWidthRaw.isEmpty() && ctx.hoverPhase > 0.0) {
        const CssLengthContext context{after->computed ? after->computed->fontSizePx * after->computed->textScale : em / zoom,
                                       after->computed ? after->computed->rootFontSizePx * after->computed->textScale : 16};
        const auto length = parseCssLengthPercentage(QStringView(after->hoverWidthRaw), context);
        const qreal hoverW = length.status == CssLengthStatus::Valid ? length.px * zoom + length.fraction * rect.width() : barW;
        barW = barW + (qBound(0.0, hoverW, rect.width()) - barW) * ctx.hoverPhase;
      }
      if (!after->focusWidthRaw.isEmpty() && ctx.focusPhase > 0.0) {
        const CssLengthContext context{after->computed ? after->computed->fontSizePx * after->computed->textScale : em / zoom,
                                       after->computed ? after->computed->rootFontSizePx * after->computed->textScale : 16};
        const auto length = parseCssLengthPercentage(QStringView(after->focusWidthRaw), context);
        const qreal focusW = length.status == CssLengthStatus::Valid ? length.px * zoom + length.fraction * rect.width() : barW;
        barW = barW + (qBound(0.0, focusW, rect.width()) - barW) * ctx.focusPhase;
      }
      barW = qMin(barW, rect.width());
      const qreal textMid = ctx.textBounds.isValid() ? ctx.textBounds.center().x() : rect.center().x();
      const QRectF initial(textMid - barW / 2, rect.bottom() - barH, barW, barH);
      const QRectF bar = after->absolute ? positionedPseudo(*after, rect, initial, zoom) : initial;
      painter.save();
      painter.setOpacity(after->opacity);
      if (after->background.kind != GradientSpec::Kind::None) {
        painter.fillRect(bar, GradientPainter::makeBrush(after->background, bar, theme.zoomPercent() / 100.0));
      } else if (after->backgroundColor.isValid()) {
        painter.fillRect(bar, after->backgroundColor);
      }
      if (after->borderBottomColor.isValid() && after->borderBottomWidth > 0.0) {
        painter.setPen(QPen(after->borderBottomColor, after->borderBottomWidth));
        painter.drawLine(bar.bottomLeft(), bar.bottomRight());
      }
      painter.restore();
    }
  }
}

void paintWriteTexture(QPainter& painter, const RenderTheme& theme, const QRectF& pageRect) {
  const PseudoElementRule* rule = pseudoRule(theme, QStringLiteral("#write"), QStringLiteral("before"));
  if (!rule) {
    return;
  }
  // A #write::before texture is a MASK — either a gradient mask (maskPattern) or
  // an SVG url() mask (svgData). Both supply shape; the ::before background-colour
  // (maskTint) supplies the visible colour, painted at the rule's opacity. The old
  // code only handled the gradient case and dropped url(svg) masks entirely
  // (phycat's diamond/cross grid), leaving the page blank.
  const bool hasGradientMask = rule->maskPattern.kind != GradientSpec::Kind::None;
  const bool hasSvgMask = !rule->svgData.isEmpty();
  if (!hasGradientMask && !hasSvgMask) {
    return;
  }
  const QColor tint = rule->maskTint.isValid() ? rule->maskTint : theme.textColor();
  const qreal scale = theme.zoomPercent() / 100.0;
  const qreal tileW = qBound(2.0, rule->maskTile.width() * scale, 256.0);
  const qreal tileH = qBound(2.0, rule->maskTile.height() * scale, 256.0);
  QImage tile;
  if (hasGradientMask) {
    // Recolour the mask gradient stops to the tint (a mask is colour-agnostic).
    GradientSpec tinted = rule->maskPattern;
    for (GradientStop& s : tinted.stops) {
      if (s.color != QColor(Qt::transparent)) {
        s.color = tint;
      }
    }
    tile = QImage(int(qCeil(tileW)), int(qCeil(tileH)), QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    {
      QPainter tp(&tile);
      tp.fillRect(tile.rect(), GradientPainter::makeBrush(tinted, QRectF(0, 0, tileW, tileH), scale));
    }
  } else {
    tile = renderMaskTile(rule->svgData, tint, QSize(int(qCeil(tileW)), int(qCeil(tileH))));
  }
  if (tile.isNull()) {
    return;
  }
  QBrush pattern(QPixmap::fromImage(tile));  // QBrush(QPixmap) → TexturePattern, tiles
  painter.save();
  painter.setOpacity(rule->opacity);
  painter.fillRect(pageRect, pattern);
  painter.restore();
}

void paintGlow(QPainter& painter, const QRectF& rect, const QColor& color, qreal blur, qreal alpha) {
  if (!color.isValid() || blur <= 0.0 || alpha <= 0.0) {
    return;
  }
  painter.save();
  painter.setPen(Qt::NoPen);
  QColor base = color;
  base.setAlpha(qMin(base.alpha(), 60));  // cap the peak so the shell sum stays soft
  constexpr int kLayers = 8;
  for (int i = kLayers; i >= 1; --i) {
    const qreal grow = blur * (i / qreal(kLayers));
    QColor shell = base;
    shell.setAlphaF(base.alphaF() * alpha / qreal(kLayers));
    painter.setBrush(shell);
    painter.drawRoundedRect(rect.adjusted(-grow, -grow, grow, grow), grow, grow);
  }
  painter.restore();
}

void paintBoxShadow(QPainter& painter, const QRectF& rect, qreal borderRadius, const QColor& color, qreal offsetX, qreal offsetY,
                    qreal blur, qreal spread) {
  if (!color.isValid()) {
    return;
  }
  if (qFuzzyIsNull(offsetX) && qFuzzyIsNull(offsetY) && qFuzzyIsNull(blur) && qFuzzyIsNull(spread)) {
    return;
  }

  const QRectF core = rect.translated(offsetX, offsetY).adjusted(-spread, -spread, spread, spread);
  if (core.isEmpty()) {
    return;
  }

  const qreal clampedBlur = qMax<qreal>(0.0, blur);
  const qreal coreRadius = qMax<qreal>(0.0, borderRadius + spread);
  if (clampedBlur <= 0.0) {
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(core, coreRadius, coreRadius);
    painter.restore();
    return;
  }

  // CSS blurs the spread-expanded shape itself; drawing an opaque spread core
  // after translucent shells creates a hard band outside the page. Render the
  // shape into an alpha image and blur it instead. Three box passes approximate
  // a Gaussian, matching the text/filter blur path already used by Muffin.
  const int blurRadius = qMax(1, qRound(clampedBlur / 2.0));
  const qreal blurExtent = blurRadius * 3.0 + 2.0;
  // Raster coverage has finite subpixel precision. Equivalent eager/lazy sums
  // can differ by 1e-13 at a coverage boundary; normalize only the raster mask,
  // without changing the shared layout/caret geometry or relaxing pixel tests.
  const auto snap = [](qreal value) { return std::round(value * 256) / 256; };
  const QRectF rasterCore(QPointF(snap(core.left()), snap(core.top())), QPointF(snap(core.right()), snap(core.bottom())));
  QRectF paintBounds = rasterCore.adjusted(-blurExtent, -blurExtent, blurExtent, blurExtent);
  if (painter.hasClipping()) {
    paintBounds = paintBounds.intersected(painter.clipBoundingRect().adjusted(-blurExtent, -blurExtent, blurExtent, blurExtent));
  }
  const QRect imageBounds = paintBounds.toAlignedRect();
  if (imageBounds.isEmpty()) {
    return;
  }

  QImage shadow(imageBounds.size(), QImage::Format_ARGB32_Premultiplied);
  shadow.fill(Qt::transparent);
  {
    QPainter maskPainter(&shadow);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    maskPainter.translate(-imageBounds.topLeft());
    maskPainter.setPen(Qt::NoPen);
    maskPainter.setBrush(color);
    maskPainter.drawRoundedRect(rasterCore, coreRadius, coreRadius);
  }
  boxBlur(shadow, blurRadius);

  painter.save();
  painter.drawImage(imageBounds.topLeft(), shadow);
  painter.restore();
}

void paintBlockHoverGlow(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect, qreal phase) {
  const HoverEffect* he = hoverEffectFor(theme, host);
  if (!he) {
    return;
  }
  paintGlow(painter, rect, he->glowColor, he->glowBlur, phase);
}

}  // namespace DecorationPainter
}  // namespace muffin
