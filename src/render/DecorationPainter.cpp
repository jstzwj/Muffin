#include "render/DecorationPainter.h"

#include "theme/CssThemeMapper.h"
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
#include <QSvgRenderer>
#include <QtMath>

#include <memory>

namespace muffin {
namespace DecorationPainter {

namespace {

// Process-wide SVG cache keyed by SVG byte data (content-addressed; themes share).
std::shared_ptr<QSvgRenderer> svgIcon(const QByteArray& data) {
  static QHash<QByteArray, std::shared_ptr<QSvgRenderer>> cache;
  if (data.isEmpty()) { return nullptr; }
  const auto it = cache.constFind(data);
  if (it != cache.constEnd()) { return *it; }
  auto r = std::make_shared<QSvgRenderer>(data);
  if (!r->isValid()) { r.reset(); }
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
  if (!icon || !tint.isValid() || size.width() <= 0 || size.height() <= 0) { return QImage(); }
  QImage shape(size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
  shape.fill(Qt::transparent);
  { QPainter sp(&shape);
    sp.setRenderHint(QPainter::SmoothPixmapTransform, true);
    icon->render(&sp, QRectF(0, 0, size.width(), size.height()));
  }
  QImage out(size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  { QPainter op(&out);
    op.fillRect(out.rect(), tint);
    op.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    op.drawImage(0, 0, shape);
  }
  return out;
}

const ElementBackground* elementBackground(const RenderTheme& theme, const QString& host) {
  for (const ElementBackground& eb : theme.decorations().backgrounds) {
    if (eb.host == host) { return &eb; }
  }
  return nullptr;
}

const PseudoElementRule* pseudoRule(const RenderTheme& theme, const QString& host, const QString& pseudo) {
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == host && r.pseudo == pseudo) { return &r; }
  }
  return nullptr;
}

const HoverEffect* hoverEffectFor(const RenderTheme& theme, const QString& host) {
  for (const HoverEffect& he : theme.decorations().hoverEffects) {
    if (he.host == host) { return &he; }
  }
  return nullptr;
}

}  // namespace

void paintIcon(QPainter& painter, const QByteArray& svgData, const QRectF& target,
               const QColor& tint, bool recolour) {
  const auto icon = svgIcon(svgData);
  if (!icon) { return; }
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
  if (tile.isNull()) { return; }
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(target, tile);
  painter.restore();
}

void paintElementBackground(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect) {
  const ElementBackground* eb = elementBackground(theme, host);
  if (!eb) { return; }
  const qreal r = qBound(0.0, eb->borderRadius, qMin(rect.width(), rect.height()) / 2.0);
  const ThemeElementStyle* es = theme.elementStyle(host);
  // CSS `backdrop-filter:` — sample the content painted BEHIND the box (only when
  // the paint device is a QImage, i.e. tests/export; the live screen editor paints
  // to the widget and would need a QImage-backed viewport to show this), filter it,
  // and composite it back so the element's own background sits on the frosted
  // backdrop. A solid backdrop (no texture) blurs to itself, so this only shows on
  // textured/gradient page backgrounds.
  if (es && hasElementBackdrop(es->paint)) {
    if (auto* devImg = dynamic_cast<QImage*>(painter.device())) {
      const QRect devRect = painter.transform().mapRect(rect).toAlignedRect().intersected(devImg->rect());
      if (!devRect.isEmpty()) {
        QImage backdrop = devImg->copy(devRect);
        applyElementBackdrop(backdrop, es->paint);
        painter.save();
        painter.setWorldTransform(QTransform());
        painter.setClipRect(devRect);
        painter.drawImage(devRect.topLeft(), backdrop);
        painter.restore();
      }
    }
  }
  // CSS `filter:` on the element: render its background (fill + gradient) to an
  // offscreen image, apply the filter chain (blur extends OUTSIDE the box — so the
  // image is padded and drawn unclipped), then the border-top line on top. Skipped
  // entirely when no filter is declared (the common fast path below).
  if (es && hasElementFilter(es->paint)) {
    const int pad = qCeil(es->paint.filterBlur + 2.0);
    const QRectF imgRect = rect.adjusted(-pad, -pad, pad, pad);
    QImage img(qCeil(imgRect.width()), qCeil(imgRect.height()), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
      QPainter ip(&img);
      ip.setRenderHint(QPainter::Antialiasing, true);
      ip.translate(-imgRect.topLeft());
      if (r > 0.5) { QPainterPath pill; pill.addRoundedRect(rect, r, r); ip.setClipPath(pill); }
      else { ip.setClipRect(rect); }
      if (eb->color.isValid()) { ip.fillRect(rect, eb->color); }
      if (eb->gradient.kind != GradientSpec::Kind::None) {
        ip.setOpacity(eb->opacity);
        ip.fillRect(rect, GradientPainter::makeBrush(eb->gradient, rect));
      }
    }
    applyElementFilter(img, es->paint);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imgRect.topLeft(), img);
    if (eb->borderTopColor.isValid() && eb->borderTopWidth > 0.0) {
      painter.setOpacity(1.0);
      painter.setPen(QPen(eb->borderTopColor, eb->borderTopWidth));
      const qreal y = rect.top() + eb->borderTopWidth / 2.0;
      painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
    painter.restore();
    return;
  }
  painter.save();
  if (r > 0.5) {
    QPainterPath pill;
    pill.addRoundedRect(rect, r, r);
    painter.setClipPath(pill);
  } else {
    painter.setClipRect(rect);
  }
  if (eb->color.isValid()) { painter.fillRect(rect, eb->color); }
  if (eb->gradient.kind != GradientSpec::Kind::None) {
    painter.setOpacity(eb->opacity);
    painter.fillRect(rect, GradientPainter::makeBrush(eb->gradient, rect));
  }
  if (eb->borderTopColor.isValid() && eb->borderTopWidth > 0.0) {
    painter.setOpacity(1.0);
    painter.setPen(QPen(eb->borderTopColor, eb->borderTopWidth));
    const qreal y = rect.top() + eb->borderTopWidth / 2.0;
    painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
  }
  painter.restore();
}

bool hasElementBackground(const RenderTheme& theme, const QString& host) {
  const ElementBackground* eb = elementBackground(theme, host);
  return eb && eb->gradient.kind != GradientSpec::Kind::None;
}

void paintHrGradient(QPainter& painter, const RenderTheme& theme, const QRectF& rect) {
  const ElementBackground* eb = elementBackground(theme, QStringLiteral("hr"));
  if (!eb) { return; }
  const qreal h = qMax<qreal>(2.0, rect.height() * 0.08);
  const QRectF bar(rect.left(), rect.center().y() - h / 2.0, rect.width(), h);
  painter.save();
  painter.fillRect(bar, GradientPainter::makeBrush(eb->gradient, bar));
  painter.restore();
}

void paintShapeBox(QPainter& painter, const PseudoElementRule& rule, QRectF box) {
  if (box.width() <= 0.0 || box.height() <= 0.0) { return; }
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

void paintPseudoDecorations(QPainter& painter, const RenderTheme& theme, const QString& host,
                            const QRectF& rect, const PaintContext& ctx) {
  const bool isHeading = ctx.headingLevel >= 1 && ctx.headingLevel <= 6;
  const QFontMetricsF fm(ctx.font);
  const qreal em = fm.height();
  const qreal vCenter = ctx.textBounds.isValid() ? ctx.textBounds.center().y() : rect.center().y();

  if (const PseudoElementRule* before = pseudoRule(theme, host, QStringLiteral("before"))) {
    if (isHeading) {
      if (before->absolute) {
        // position:absolute left bar (phycat h3): anchored to the heading padding
        // box (rect.left), vertically centred on the text line. Resolve width/
        // height against the HOST rect when the CSS used a `%` (phycat's `height:
        // 61%` is 61% of the rendered heading, not 0.61em — the map-time value in
        // `size` is em-relative and made the bar too short).
        const qreal w = !before->sizeRawWidth.isEmpty()
            ? CssThemeMapper::resolveLengthPx(before->sizeRawWidth, {}, em, rect.width())
            : (before->size.width() > 0.0 ? before->size.width() : qMax<qreal>(2.0, em * 0.25));
        const qreal h = !before->sizeRawHeight.isEmpty()
            ? CssThemeMapper::resolveLengthPx(before->sizeRawHeight, {}, em, rect.height())
            : (before->size.height() > 0.0 ? before->size.height() : em);
        paintShapeBox(painter, *before, QRectF(rect.left() + before->insets.left(), vCenter - h / 2.0, w, h));
      } else if (!before->svgData.isEmpty()) {
        // Legacy: an inline SVG ::before painted into the left margin (no advance).
        const QPointF anchor = ctx.textStart.x() >= 0 ? ctx.textStart : rect.topLeft();
        const qreal s = (before->size.width() > 0 ? before->size.width() : em);
        const QColor tint = before->color.isValid() ? before->color : before->backgroundColor;
        paintIcon(painter, before->svgData, QRectF(anchor.x() - s - 2.0, anchor.y(), s, s), tint, before->svgFromMask);
      } else {
        // Inline marker (h4 disc / h5 ring / h6 dash): occupies the reserved zone
        // [contentLeftX, textStart) so it sits left of the (already-shifted) text.
        const qreal zoneLeft = ctx.contentLeftX >= 0.0 ? ctx.contentLeftX : rect.left();
        const qreal zoneRight = (ctx.textStart.x() >= 0 ? ctx.textStart.x() : rect.right()) - before->marginRight;
        if (before->backgroundColor.isValid() || before->borderWidth > 0.0) {
          const qreal w = before->size.width() > 0.0 ? before->size.width() : em;
          const qreal h = before->size.height() > 0.0 ? before->size.height() : em;
          paintShapeBox(painter, *before, QRectF(zoneRight - w, vCenter - h / 2.0, w, h));
        } else if (!before->content.isEmpty() && zoneRight > zoneLeft) {
          // Prefer the layout-resolved text (counter() evaluated to "1. " etc.) over
          // the rule's literal `content` (which still holds `counter(h1) ". "` raw).
          const QString text = ctx.beforeContent.isEmpty() ? before->content : ctx.beforeContent;
          painter.save();
          painter.setOpacity(before->opacity);
          painter.setFont(ctx.font);
          painter.setPen(before->color.isValid() ? before->color : theme.textColor());
          painter.drawText(QRectF(zoneLeft, ctx.textBounds.top(), zoneRight - zoneLeft, ctx.textBounds.height()),
                           Qt::AlignVCenter | Qt::AlignRight, text);
          painter.restore();
        }
      }
    } else if (!before->content.isEmpty() && host == QStringLiteral("blockquote")) {
      // Honor CSS geometry: position:absolute left/top anchor the glyph and
      // font-size scales it (phycat's ✨ at left:16px/top:18px/font-size:20px).
      // Falls back to the legacy inset (left+4, baseline+2, host font) when the
      // theme declared no positioning, preserving prior behaviour.
      QFont f = ctx.font;
      if (before->fontSizePx > 0.0) { f.setPointSizeF(before->fontSizePx * 72.0 / 96.0); }
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
    if (!after->svgData.isEmpty() && isHeading) {
      // Trailing mask icon (phycat h3-h6): immediately after the text, top-aligned
      // (vertical-align: top), sized to the rule's width/height.
      const QPointF anchor = ctx.textEnd.x() >= 0 ? ctx.textEnd : QPointF(rect.right(), rect.top());
      const qreal w = after->size.width() > 0.0 ? after->size.width() : em;
      const qreal h = after->size.height() > 0.0 ? after->size.height() : em;
      const qreal top = ctx.textBounds.isValid() ? ctx.textBounds.top() : anchor.y();
      const QColor tint = after->color.isValid() ? after->color : after->backgroundColor;
      painter.save();
      painter.setOpacity(after->opacity);
      paintIcon(painter, after->svgData, QRectF(anchor.x() + after->marginLeft, top, w, h), tint, after->svgFromMask);
      painter.restore();
    } else if ((after->background.kind != GradientSpec::Kind::None || after->backgroundColor.isValid() ||
                (after->borderBottomColor.isValid() && after->borderBottomWidth > 0.0)) && isHeading) {
      // ::after underline bar. Width/height come from the rule (e.g. Whitey's
      // h2::after border-bottom: 100px centred; phycat's h1::after gradient bar).
      const qreal borderW = after->borderBottomWidth > 0.0 ? after->borderBottomWidth : 0.0;
      const qreal barH = (after->size.height() > 0 ? after->size.height() : qMax<qreal>(2.0, borderW));
      qreal barW = (after->size.width() > 0 ? after->size.width() : (ctx.textBounds.isValid() ? ctx.textBounds.width() : rect.width()));
      // Hover widens the bar toward its :hover width (phycat h1::after 40px → 100%),
      // animated by the HoverAnimator phase. Focus widens it toward its :focus
      // width next (same recipe, FocusAnimator phase). The centred anchor (textMid,
      // below) keeps it growing symmetrically from the middle, matching the reference.
      if (!after->hoverWidthRaw.isEmpty() && ctx.hoverPhase > 0.0) {
        const qreal hoverW = CssThemeMapper::resolveLengthPx(after->hoverWidthRaw, {}, em, rect.width());
        barW = barW + (qBound(0.0, hoverW, rect.width()) - barW) * ctx.hoverPhase;
      }
      if (!after->focusWidthRaw.isEmpty() && ctx.focusPhase > 0.0) {
        const qreal focusW = CssThemeMapper::resolveLengthPx(after->focusWidthRaw, {}, em, rect.width());
        barW = barW + (qBound(0.0, focusW, rect.width()) - barW) * ctx.focusPhase;
      }
      barW = qMin(barW, rect.width());
      const qreal textMid = ctx.textBounds.isValid() ? ctx.textBounds.center().x()
                            : (ctx.textStart.x() >= 0 && ctx.textEnd.x() >= 0
                                   ? (ctx.textStart.x() + ctx.textEnd.x()) / 2.0
                                   : rect.center().x());
      const QRectF bar(textMid - barW / 2.0, rect.bottom() - barH, barW, barH);
      painter.save();
      painter.setOpacity(after->opacity);
      if (after->background.kind != GradientSpec::Kind::None) {
        painter.fillRect(bar, GradientPainter::makeBrush(after->background, bar));
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
  if (!rule) { return; }
  // A #write::before texture is a MASK — either a gradient mask (maskPattern) or
  // an SVG url() mask (svgData). Both supply shape; the ::before background-colour
  // (maskTint) supplies the visible colour, painted at the rule's opacity. The old
  // code only handled the gradient case and dropped url(svg) masks entirely
  // (phycat's diamond/cross grid), leaving the page blank.
  const bool hasGradientMask = rule->maskPattern.kind != GradientSpec::Kind::None;
  const bool hasSvgMask = !rule->svgData.isEmpty();
  if (!hasGradientMask && !hasSvgMask) { return; }
  const QColor tint = rule->maskTint.isValid() ? rule->maskTint : theme.textColor();
  const qreal tileW = qBound(2.0, rule->maskTile.width(), 256.0);
  const qreal tileH = qBound(2.0, rule->maskTile.height(), 256.0);
  QImage tile;
  if (hasGradientMask) {
    // Recolour the mask gradient stops to the tint (a mask is colour-agnostic).
    GradientSpec tinted = rule->maskPattern;
    for (GradientStop& s : tinted.stops) {
      if (s.color != QColor(Qt::transparent)) { s.color = tint; }
    }
    tile = QImage(int(qCeil(tileW)), int(qCeil(tileH)), QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    { QPainter tp(&tile);
      tp.fillRect(tile.rect(), GradientPainter::makeBrush(tinted, QRectF(0, 0, tileW, tileH)));
    }
  } else {
    tile = renderMaskTile(rule->svgData, tint, QSize(int(qCeil(tileW)), int(qCeil(tileH))));
  }
  if (tile.isNull()) { return; }
  QBrush pattern(QPixmap::fromImage(tile));  // QBrush(QPixmap) → TexturePattern, tiles
  painter.save();
  painter.setOpacity(rule->opacity);
  painter.fillRect(pageRect, pattern);
  painter.restore();
}

void paintGlow(QPainter& painter, const QRectF& rect, const QColor& color, qreal blur, qreal alpha) {
  if (!color.isValid() || blur <= 0.0 || alpha <= 0.0) { return; }
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

void paintBlockHoverGlow(QPainter& painter, const RenderTheme& theme, const QString& host,
                         const QRectF& rect, qreal phase) {
  const HoverEffect* he = hoverEffectFor(theme, host);
  if (!he) { return; }
  paintGlow(painter, rect, he->glowColor, he->glowBlur, phase);
}

}  // namespace DecorationPainter
}  // namespace muffin
