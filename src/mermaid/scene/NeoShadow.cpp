#include "mermaid/scene/NeoShadow.h"

#include "mermaid/theme/MermaidColor.h"

#include <QImage>
#include <QRegularExpression>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace muffin::mermaid::scene {

NeoShadow parseNeoShadow(const QString& css) {
  NeoShadow shadow;
  const QString trimmed = css.trimmed();
  if (!trimmed.startsWith(QLatin1String("drop-shadow")))
    return shadow; // "url(#drop-shadow)"-style themes keep flat semantics
  const int open = trimmed.indexOf(QLatin1Char('('));
  const int close = trimmed.lastIndexOf(QLatin1Char(')'));
  if (open < 0 || close <= open)
    return shadow;
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  const QStringList parts = trimmed.mid(open + 1, close - open - 1).split(whitespace);
  qreal lengths[3] = {0.0, 0.0, 0.0};
  int lengthsSeen = 0;
  QString colorText;
  for (const QString& part : parts) {
    QString token = part;
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
      token.chop(2);
    bool ok = false;
    const qreal value = token.toDouble(&ok);
    if (ok && lengthsSeen < 3) {
      lengths[lengthsSeen++] = value;
    } else {
      colorText += colorText.isEmpty() ? part : (QLatin1Char(' ') + part);
    }
  }
  if (lengthsSeen < 2)
    return shadow;
  shadow.valid = true;
  shadow.dx = lengths[0];
  shadow.dy = lengths[1];
  // The blur radius is the feDropShadow stdDeviation itself, and Skia's
  // raster blur of it reads a little wider still: its profile outside a
  // straight edge is a Gaussian's of 1.1 times the radius (measured in
  // Chrome for radii from 1 to 8 px). Every built-in theme uses 2px.
  shadow.sigma = 1.1 * (lengthsSeen > 2 ? lengths[2] : 0.0);
  shadow.tint = colorText.isEmpty() ? QColor(Qt::black) : muffin::mermaid::color::toQColor(colorText);
  return shadow;
}

QString dropShadowText(const QString& filter, bool darkTheme) {
  const QString trimmed = filter.trimmed();
  // The redux themes name the filter the renderer defines in `<defs>`:
  // feDropShadow dx 4, dy 4, no blur, flood-opacity 0.06.
  if (!trimmed.startsWith(QLatin1String("url(")) || !trimmed.contains(QLatin1String("drop-shadow")))
    return trimmed;
  const QColor tint(darkTheme ? Qt::white : Qt::black);
  return QStringLiteral("drop-shadow(4px 4px 0px rgba(%1,%2,%3,0.06))")
      .arg(tint.red())
      .arg(tint.green())
      .arg(tint.blue());
}

// Paints the element silhouette blurred by `shadow.sigma`, offset by
// (dx, dy), in `shadow.tint` — the raster equivalent of filter:
// drop-shadow(). The filter input is the element's ACTUAL rendering: each
// paint channel contributes its region at its resolved alpha (color alpha ×
// fill-/stroke-opacity × element opacity, source-over combined where the
// regions overlap); a channel that paints nothing contributes nothing, and
// when neither paints (fill:none;stroke:none, or the element is hidden)
// the filter output is empty — exactly like the browser.
//
// One region contribution to the filter input is a path, its stroke width,
// and the per-channel alphas of ITS OWN rendering. A filtered GROUP (a
// stateEnd's g.outer-path) is several contributions source-over combined in
// ONE mask — collapsing them into global scalars would let a visible inner
// dot cast a full ring-shaped shadow when the ring itself paints nothing.
void paintNeoShadow(QPainter& painter, const QVector<ShadowPart>& parts, const NeoShadow& shadow) {
  if (!shadow.valid || parts.isEmpty())
    return;
  QRectF silhouette;
  bool any = false;
  for (const ShadowPart& part : parts) {
    if (part.path.isEmpty() || !part.channels.paints())
      continue;
    QRectF partBounds = part.path.boundingRect();
    if (part.channels.strokeAlpha > 0.0 && part.strokeWidth > 0.0) {
      QPainterPathStroker stroker;
      stroker.setWidth(part.strokeWidth);
      stroker.setCapStyle(Qt::FlatCap);
      stroker.setJoinStyle(Qt::MiterJoin);
      partBounds = partBounds.united(stroker.createStroke(part.path).boundingRect());
    }
    silhouette = any ? silhouette.united(partBounds) : partBounds;
    any = true;
  }
  if (!any || !silhouette.isValid())
    return;
  // The mask is the silhouette on the device's own pixels. The blur Chrome
  // rasterises is one of the screen's, and its profile only comes out the
  // same where its samples lie where the screen's do: a mask on another
  // grid, a fraction of a pixel off, lands the shadow that much off.
  const QTransform world = painter.combinedTransform();
  const qreal scale = qMax(qAbs(world.m11()), qAbs(world.m22()));
  bool invertible = false;
  const QTransform inverse = world.inverted(&invertible);
  if (!(scale > 0.0) || !invertible)
    return;
  const qreal sigma = shadow.sigma * scale;
  const qreal margin = 3.0 * sigma + (std::abs(shadow.dx) + std::abs(shadow.dy)) * scale + 2.0;
  const QRectF around = world.mapRect(silhouette).adjusted(-margin, -margin, margin, margin);
  const QRect device(qFloor(around.left()), qFloor(around.top()), qCeil(around.right()) - qFloor(around.left()),
                     qCeil(around.bottom()) - qFloor(around.top()));
  if (device.isEmpty())
    return;
  QImage mask(device.size(), QImage::Format_Grayscale8);
  mask.fill(0);
  {
    QPainter maskPainter(&mask);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    // Every pixel of the mask is a pixel of the device, at `device`.
    maskPainter.setTransform(world * QTransform::fromTranslate(-device.x(), -device.y()));
    maskPainter.setPen(Qt::NoPen);
    // The mask stores SOURCE-OVER COVERAGE in the grayscale value: each
    // channel paints white at its own alpha (fill region, then stroke
    // region), so overlaps accumulate like the element's real rendering.
    for (const ShadowPart& part : parts) {
      if (part.path.isEmpty())
        continue;
      if (part.channels.fillAlpha > 0.0) {
        maskPainter.setOpacity(part.channels.fillAlpha);
        maskPainter.setBrush(Qt::white);
        maskPainter.drawPath(part.path);
      }
      if (part.channels.strokeAlpha > 0.0 && part.strokeWidth > 0.0) {
        QPainterPathStroker stroker;
        stroker.setWidth(part.strokeWidth);
        stroker.setCapStyle(Qt::FlatCap);
        stroker.setJoinStyle(Qt::MiterJoin);
        const QPainterPath strokeRegion = stroker.createStroke(part.path);
        if (!strokeRegion.isEmpty()) {
          maskPainter.setOpacity(part.channels.strokeAlpha);
          maskPainter.setBrush(Qt::white);
          maskPainter.drawPath(strokeRegion);
        }
      }
    }
  }
  if (sigma > 0.0) {
    // Separable gaussian with standard deviation sigma.
    const int radius = qMax(1, qCeil(3.0 * sigma));
    QVector<qreal> kernel(2 * radius + 1, 0.0);
    for (int i = 0; i <= radius; ++i) {
      const qreal gaussian = std::exp(-(i * i) / (2.0 * sigma * sigma + 1e-9));
      kernel[radius - i] = kernel[radius + i] = gaussian;
    }
    const qreal sum = std::accumulate(kernel.cbegin(), kernel.cend(), 0.0);
    for (qreal& tap : kernel)
      tap /= sum;
    const auto blurPass = [&](bool horizontal) {
      QImage source = mask;
      const int w = mask.width(), h = mask.height();
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          qreal accumulated = 0.0;
          for (int tap = -radius; tap <= radius; ++tap) {
            const int sx = horizontal ? qBound(0, x + tap, w - 1) : x;
            const int sy = horizontal ? y : qBound(0, y + tap, h - 1);
            accumulated += source.scanLine(sy)[sx] * kernel.at(tap + radius);
          }
          mask.scanLine(y)[x] = static_cast<unsigned char>(qBound(0, qRound(accumulated), 255));
        }
      }
    };
    blurPass(true);
    blurPass(false);
  }
  QImage layer(mask.size(), QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  const QColor tint = shadow.tint.isValid() ? shadow.tint : QColor(Qt::black);
  for (int y = 0; y < mask.height(); ++y) {
    const unsigned char* row = mask.scanLine(y);
    QRgb* target = reinterpret_cast<QRgb*>(layer.scanLine(y));
    for (int x = 0; x < mask.width(); ++x) {
      // Coverage × the tint's own alpha (e.g. redux-dark's flat white
      // feDropShadow carries flood-opacity 0.06). qPremultiply applies the
      // final alpha to the channels — keep RGB at the full tint color so
      // soft edge pixels stay the shadow color (not alpha-squared).
      const int alpha = tint.alpha() * row[x] / 255;
      target[x] = qPremultiply(qRgba(tint.red(), tint.green(), tint.blue(), alpha));
    }
  }
  // The shadow where the filter puts it, its offset in device pixels, in the
  // painter's own coordinates.
  const QRectF placed(qFloor(around.left()) + shadow.dx * scale, qFloor(around.top()) + shadow.dy * scale,
                      device.width(), device.height());
  painter.drawImage(inverse.mapRect(placed), layer);
}

} // namespace muffin::mermaid::scene
