#include "render/DecorationPainter.h"

#include "render/Blur.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssValueParser.h"
#include "render/Filter.h"
#include "render/GradientPainter.h"
#include "theme/ThemeDefinition.h"

#include <QBrush>
#include "render/TextLayout.h"
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

const HoverEffect* hoverEffectFor(const RenderTheme& theme, const QString& host) {
  for (const HoverEffect& he : theme.decorations().hoverEffects) {
    if (he.host == host) {
      return &he;
    }
  }
  return nullptr;
}

}  // namespace

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

void paintWriteTexture(QPainter& painter, const RenderTheme& theme, const QRectF& pageRect,
                       const std::optional<PseudoElementRule>& rule) {
  if (!rule) {
    return;
  }
  if (rule->computed && (rule->computed->resolvedValue("content") == "none" ||
      rule->computed->resolvedValue("content") == "normal" || rule->computed->resolvedValue("display") == "none")) return;
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
