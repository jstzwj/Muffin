#include "render/LayoutBox.h"

#include "render/DecorationPainter.h"
#include "render/GradientPainter.h"
#include "render/Filter.h"
#include <QPainter>
#include <QPainterPath>

namespace muffin {

void paintLayoutBox(QPainter& painter, const LayoutBox& box, QPointF offset) {
  const auto rect = box.borderBox.translated(offset);
  if (!rect.isValid()) return;
  const auto& used = box.usedBox;
  const auto& paint = box.style.paint;
  if (hasElementBackdrop(paint)) {
    if (auto* device = dynamic_cast<QImage*>(painter.device())) {
      const auto area = painter.transform().mapRect(rect).toAlignedRect().intersected(device->rect());
      if (!area.isEmpty()) {
        auto backdrop = device->copy(area);
        applyElementBackdrop(backdrop, paint);
        painter.save();
        painter.setWorldTransform({});
        painter.setClipRect(area);
        painter.drawImage(area.topLeft(), backdrop);
        painter.restore();
      }
    }
  }
  if (hasElementFilter(paint)) {
    const int padding = qCeil(paint.filterBlur + 2);
    const auto area = rect.adjusted(-padding, -padding, padding, padding);
    QImage image(qCeil(area.width()), qCeil(area.height()), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    auto unfiltered = box;
    unfiltered.style.paint.filterPresent = false;
    unfiltered.style.paint.filterBlur = 0;
    unfiltered.style.paint.filterBrightness = unfiltered.style.paint.filterContrast = unfiltered.style.paint.filterOpacity = 1;
    unfiltered.style.paint.filterGrayscale = unfiltered.style.paint.filterSepia = unfiltered.style.paint.filterHueRotateDeg = 0;
    unfiltered.style.paint.backdropPresent = false;
    unfiltered.style.paint.backdropBlur = 0;
    unfiltered.style.paint.backdropBrightness = unfiltered.style.paint.backdropContrast = unfiltered.style.paint.backdropOpacity = 1;
    unfiltered.style.paint.backdropGrayscale = unfiltered.style.paint.backdropSepia = unfiltered.style.paint.backdropHueRotateDeg = 0;
    {
      QPainter buffer(&image);
      buffer.setRenderHint(QPainter::Antialiasing);
      paintLayoutBox(buffer, unfiltered, offset - area.topLeft());
    }
    applyElementFilter(image, paint);
    painter.drawImage(area.topLeft(), image);
    return;
  }
  painter.save();
  painter.setOpacity(painter.opacity() * paint.opacity);
  DecorationPainter::paintBoxShadow(painter, rect, used.borderRadius, paint.boxShadowColor, paint.boxShadowOffsetX, paint.boxShadowOffsetY,
                                    paint.boxShadowBlur, paint.boxShadowSpread);
  QPainterPath clip;
  clip.addRoundedRect(rect, used.borderRadius, used.borderRadius);
  painter.setClipPath(clip, Qt::IntersectClip);
  painter.setPen(Qt::NoPen);
  if (paint.backgroundColor.isValid()) painter.fillPath(clip, paint.backgroundColor);
  if (GradientPainter::isGradient(paint.backgroundImage)) painter.fillPath(clip, GradientPainter::makeBrush(paint.backgroundImage, rect));
  const auto side = [&](QRectF area, const QColor& color, const QString& style, bool horizontal) {
    if (area.width() <= 0 || area.height() <= 0 || !color.isValid()) return;
    const qreal width = horizontal ? area.height() : area.width();
    if (style == QLatin1String("dashed") || style == QLatin1String("dotted")) {
      painter.save();
      painter.setClipRect(area, Qt::IntersectClip);
      painter.setPen(QPen(color, width, style == QLatin1String("dotted") ? Qt::DotLine : Qt::DashLine, Qt::FlatCap));
      painter.drawLine(horizontal ? QLineF(area.left(), area.center().y(), area.right(), area.center().y())
                                  : QLineF(area.center().x(), area.top(), area.center().x(), area.bottom()));
      painter.restore();
    } else if (style == QLatin1String("double") && width >= 3) {
      const qreal strip = width / 3;
      painter.fillRect(
          horizontal ? QRectF(area.left(), area.top(), area.width(), strip) : QRectF(area.left(), area.top(), strip, area.height()), color);
      painter.fillRect(horizontal ? QRectF(area.left(), area.bottom() - strip, area.width(), strip)
                                  : QRectF(area.right() - strip, area.top(), strip, area.height()),
                       color);
    } else
      painter.fillRect(area, color);
  };
  side(QRectF(rect.left(), rect.top(), rect.width(), used.borderTopWidth), used.borderTopColor, used.borderTopStyle, true);
  side(QRectF(rect.left(), rect.bottom() - used.borderBottomWidth, rect.width(), used.borderBottomWidth), used.borderBottomColor,
       used.borderBottomStyle, true);
  side(QRectF(rect.left(), rect.top(), used.borderLeftWidth, rect.height()), used.borderLeftColor, used.borderLeftStyle, false);
  side(QRectF(rect.right() - used.borderRightWidth, rect.top(), used.borderRightWidth, rect.height()), used.borderRightColor,
       used.borderRightStyle, false);
  painter.restore();
}

}  // namespace muffin
