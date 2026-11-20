#include "render/LayoutBox.h"

#include "render/DecorationPainter.h"
#include "render/GradientPainter.h"
#include "render/Filter.h"
#include "render/CssPaintAnimation.h"
#include "render/CssBackgroundPainter.h"
#include "diagnostics/ThemePerformance.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace muffin {

QTransform elementPaintTransform(const ThemeElementPaintStyle& style, const QRectF& rect, qreal zoom) {
  QTransform transform;
  transform.translate(rect.center().x(), rect.center().y());
  for (const auto& op : style.transforms) {
    using Kind = ThemeElementPaintStyle::Transform::Kind;
    if (op.kind == Kind::Translate)
      transform.translate(op.x.px * zoom + op.x.fraction * rect.width(),
                          op.y.px * zoom + op.y.fraction * rect.height());
    else if (op.kind == Kind::Scale)
      transform.scale(op.x.px, op.y.px);
    else
      transform.rotate(op.angle);
  }
  transform.translate(-rect.center().x(), -rect.center().y());
  return transform;
}

void paintCssElement(QPainter& painter, QRectF borderBox, QRectF visualBounds, const ThemeElementPaintStyle& style,
                     qreal zoom, CssPaintContext* context,
                     const std::function<void(QPainter&, const ThemeElementPaintStyle&)>& contents,
                     const LayoutBox* maskGeometry) {
  painter.save();
  painter.setWorldTransform(elementPaintTransform(style, borderBox, zoom), true);
  const bool filtered = hasElementFilter(style);
  if (!filtered && style.opacity == 1 && style.maskLayers.empty())
    contents(painter, style);
  else {
    auto neutral = style;
    neutral.opacity = 1;
    clearElementFilter(neutral);
    neutral.maskLayers.clear();
    painter.setOpacity(painter.opacity() * style.opacity);
    paintSparseCssFilter(
        painter, visualBounds, style, zoom,
        [&](QPainter& buffer) {
          const auto pointer = context ? context->pointer : std::nullopt;
          if (pointer)
            context->pointer = buffer.worldTransform().map(painter.worldTransform().inverted().map(*pointer));
          contents(buffer, neutral);
          if (context)
            context->pointer = pointer;
        },
        [&](QImage& layer, QPointF origin) {
          if (style.maskLayers.empty())
            return;
          auto box = maskGeometry ? *maskGeometry : LayoutBox::place({}, {}, {}, borderBox);
          const auto delta = borderBox.topLeft() - box.borderBox.topLeft();
          box.borderBox.translate(delta);
          box.paddingBox.translate(delta);
          box.contentBox.translate(delta);
          applyCssMasks(layer, origin, box, style, zoom);
        });
  }
  painter.restore();
}

void paintBorderSide(QPainter& painter, QRectF area, const QColor& color, const QString& style, bool horizontal) {
  if (area.width() <= 0 || area.height() <= 0 || !color.isValid())
    return;
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
    painter.fillRect(horizontal ? QRectF(area.left(), area.top(), area.width(), strip)
                                : QRectF(area.left(), area.top(), strip, area.height()),
                     color);
    painter.fillRect(horizontal ? QRectF(area.left(), area.bottom() - strip, area.width(), strip)
                                : QRectF(area.right() - strip, area.top(), strip, area.height()),
                     color);
  } else
    painter.fillRect(area, color);
}

void paintLayoutBox(QPainter& painter, const LayoutBox& box, QPointF offset, const ThemeElementStyle* overrideStyle,
                    BoxBorders borders) {
  ThemePerfScope performance(ThemePerfStage::Paint);
  const auto rect = box.borderBox.translated(offset);
  if (!rect.isValid())
    return;
  auto used = box.usedBox;
  if (overrideStyle) {
    const qreal scale = box.style.text.fontSizePx > 0 ? box.font.pointSizeF() * 96 / 72 / box.style.text.fontSizePx : 1;
    used.borderRadius = overrideStyle->box.borderRadius * scale;
    used.borderTopColor = overrideStyle->box.borderTopColor;
    used.borderRightColor = overrideStyle->box.borderRightColor;
    used.borderBottomColor = overrideStyle->box.borderBottomColor;
    used.borderLeftColor = overrideStyle->box.borderLeftColor;
  }
  const auto& paint = overrideStyle ? overrideStyle->paint : box.style.paint;
  if (!paint.maskLayers.empty() || hasElementFilter(paint)) {
    const qreal scale = box.style.text.fontSizePx > 0 ? box.font.pointSizeF() * 96 / 72 / box.style.text.fontSizePx : 1;
    paintCssElement(
        painter, rect, box.visualOverflow.translated(offset), paint, scale, nullptr,
        [&](QPainter& target, const auto& neutral) {
          auto style = overrideStyle ? *overrideStyle : box.style;
          style.paint = neutral;
          paintLayoutBox(target, box, offset, &style, borders);
        },
        &box);
    return;
  }
  if (hasElementBackdrop(paint)) {
    if (auto* device = dynamic_cast<QImage*>(painter.device())) {
      auto effect = paint;
      effect.filters = paint.backdropFilters;
      effect.filterBlur = paint.backdropBlur;
      const auto expansion =
          cssFilterOutsets(effect, box.font.pointSizeF() * 96 / 72 / qMax<qreal>(1, box.style.text.fontSizePx));
      const auto area =
          painter.deviceTransform().mapRect(rect.marginsAdded(expansion)).toAlignedRect().intersected(device->rect());
      if (!area.isEmpty()) {
        auto backdrop = device->copy(area);
        applyElementBackdrop(backdrop, paint,
                             box.font.pointSizeF() * 96 / 72 / qMax<qreal>(1, box.style.text.fontSizePx));
        painter.save();
        const auto clip = painter.worldTransform().mapRect(rect);
        painter.setWorldTransform({});
        painter.setClipRect(clip, Qt::IntersectClip);
        painter.drawImage(QPointF(area.topLeft()) / device->devicePixelRatioF(), backdrop);
        painter.restore();
      }
    }
  }
  painter.save();
  painter.setOpacity(painter.opacity() * paint.opacity);
  DecorationPainter::paintBoxShadow(painter, rect, used.borderRadius, paint.boxShadowColor, paint.boxShadowOffsetX,
                                    paint.boxShadowOffsetY, paint.boxShadowBlur, paint.boxShadowSpread);
  QPainterPath clip;
  clip.addRoundedRect(rect, used.borderRadius, used.borderRadius);
  painter.setClipPath(clip, Qt::IntersectClip);
  painter.setPen(Qt::NoPen);
  const qreal scale = box.style.text.fontSizePx > 0 ? box.font.pointSizeF() * 96 / 72 / box.style.text.fontSizePx : 1;
  paintCssBackgrounds(painter, box, offset, paint, scale);
  if (borders == BoxBorders::Paint) {
    paintBorderSide(painter, QRectF(rect.left(), rect.top(), rect.width(), used.borderTopWidth), used.borderTopColor,
                    used.borderTopStyle, true);
    paintBorderSide(painter,
                    QRectF(rect.left(), rect.bottom() - used.borderBottomWidth, rect.width(), used.borderBottomWidth),
                    used.borderBottomColor, used.borderBottomStyle, true);
    paintBorderSide(painter, QRectF(rect.left(), rect.top(), used.borderLeftWidth, rect.height()), used.borderLeftColor,
                    used.borderLeftStyle, false);
    paintBorderSide(painter,
                    QRectF(rect.right() - used.borderRightWidth, rect.top(), used.borderRightWidth, rect.height()),
                    used.borderRightColor, used.borderRightStyle, false);
  }
  painter.restore();
}

} // namespace muffin
