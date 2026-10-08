#pragma once

#include "theme/ThemeDefinition.h"
#include <QFont>
#include <QRectF>
#include <QHash>
#include <memory>

class QPainter;

namespace muffin {

// Used values and geometry belong to the layout snapshot. Painting and input
// must not query CSS again or reconstruct content coordinates from theme tokens.
struct LayoutBox {
  QString hostKey;
  ThemeElementStyle style;
  ThemeElementPaintStyle hoverPaint, focusPaint;
  ThemeElementBoxStyle usedBox;
  QFont font;
  qreal lineHeight = 0;
  qreal beforeAdvance = 0;
  QRectF flowRect;
  QRectF borderBox;
  QRectF paddingBox;
  QRectF contentBox;
  QPointF inlineTextOrigin;
  QRectF visualOverflow;
  bool valid = false;

  static QMarginsF borders(const ThemeElementBoxStyle& box) {
    return QMarginsF(box.borderLeftWidth, box.borderTopWidth, box.borderRightWidth, box.borderBottomWidth);
  }
  static QMarginsF insets(const ThemeElementBoxStyle& box) { return box.padding + borders(box); }
  static qreal borderWidth(const ThemeElementBoxStyle& box, qreal containingWidth, qreal intrinsicWidth = -1) {
    const auto inset = insets(box);
    const qreal extra = box.borderBox ? 0 : inset.left() + inset.right();
    qreal width = box.widthLength.status == CssLengthStatus::Valid ? box.widthLength.used(containingWidth) + extra : containingWidth;
    if (box.widthFitContent && intrinsicWidth >= 0) width = qMin(width, intrinsicWidth + inset.left() + inset.right());
    if (box.maxWidthLength.status == CssLengthStatus::Valid) width = qMin(width, box.maxWidthLength.used(containingWidth) + extra);
    if (box.minWidthLength.status == CssLengthStatus::Valid) width = qMax(width, box.minWidthLength.used(containingWidth) + extra);
    return qMax<qreal>(1, width);
  }
  static qreal borderHeight(const ThemeElementBoxStyle& box, qreal intrinsicHeight) {
    const auto inset = insets(box);
    const qreal extra = box.borderBox ? 0 : inset.top() + inset.bottom();
    qreal height = intrinsicHeight + inset.top() + inset.bottom();
    if (box.heightLength.status == CssLengthStatus::Valid && !box.heightLength.hasPercentage) height = box.heightLength.px + extra;
    if (box.maxHeightLength.status == CssLengthStatus::Valid && !box.maxHeightLength.hasPercentage)
      height = qMin(height, box.maxHeightLength.px + extra);
    if (box.minHeightLength.status == CssLengthStatus::Valid && !box.minHeightLength.hasPercentage)
      height = qMax(height, box.minHeightLength.px + extra);
    return qMax<qreal>(0, height);
  }
  static LayoutBox place(QString key, ThemeElementStyle style, ThemeElementBoxStyle used, QRectF rect, QFont font = {}, qreal before = 0) {
    LayoutBox box;
    box.hostKey = std::move(key);
    box.style = std::move(style);
    box.usedBox = std::move(used);
    box.font = std::move(font);
    box.beforeAdvance = before;
    box.flowRect = box.borderBox = rect;
    box.paddingBox = rect.marginsRemoved(borders(box.usedBox));
    box.contentBox = box.paddingBox.marginsRemoved(box.usedBox.padding);
    box.inlineTextOrigin = box.contentBox.topLeft() + QPointF(before, 0);
    const qreal overflow = qMax(box.style.paint.boxShadowBlur + box.style.paint.boxShadowSpread, box.style.paint.filterBlur + 2);
    box.visualOverflow = rect.adjusted(-overflow, -overflow, overflow, overflow);
    box.valid = true;
    return box;
  }
};

void paintLayoutBox(QPainter& painter, const LayoutBox& box, QPointF offset = {});

// One immutable used-style snapshot can serve every equivalent inline run in a
// layout generation. Positioned fragments keep their own geometry separately.
class LayoutStyleCache {
 public:
  std::shared_ptr<const LayoutBox> snapshot(const ThemeElementStyle& style, const ThemeElementBoxStyle& used, const QFont& font,
                                            qreal containingWidth) {
    const auto key = style.key + QLatin1Char('/') + QString::number(style.fingerprint) + QLatin1Char('/') + font.key() + QLatin1Char('/') +
                     QString::number(containingWidth, 'g', 17);
    if (const auto found = entries_.constFind(key); found != entries_.cend()) return found.value();
    auto value = std::make_shared<const LayoutBox>(LayoutBox::place(style.key, style, used, {}, font));
    entries_.insert(key, value);
    return value;
  }
  void clear() { entries_.clear(); }

 private:
  QHash<QString, std::shared_ptr<const LayoutBox>> entries_;
};

}  // namespace muffin
