#pragma once

#include "render/CssFormattingContext.h"
#include <limits>

namespace muffin {
// Negative references explicitly mean indefinite. Cyclic percentages contribute
// zero to intrinsic measurement; the absolute part of calc() is retained.
inline qreal cssGap(const CssLengthPercentage& length, qreal reference, qreal scale = 1) {
  return length.status == CssLengthStatus::Valid ? qMax<qreal>(0, length.px * scale + length.fraction * qMax<qreal>(0, reference)) : 0;
}
struct CssAxisConstraints {
  qreal minimum = 0;
  qreal maximum = std::numeric_limits<qreal>::max();
  qreal clamp(qreal value) const { return qMax(minimum, qMin(maximum, value)); }
};
inline CssAxisConstraints cssContentConstraints(const ThemeElementBoxStyle& box, int axis, qreal reference = -1, qreal scale = 1) {
  const qreal extra = axis == 0 ? box.padding.left() + box.padding.right() + box.borderLeftWidth + box.borderRightWidth
                                : box.padding.top() + box.padding.bottom() + box.borderTopWidth + box.borderBottomWidth;
  const auto resolve = [&](const CssLengthPercentage& length, qreal fallback) {
    if (length.status != CssLengthStatus::Valid || (length.hasPercentage && reference < 0)) return fallback;
    return qMax<qreal>(0, length.px * scale + length.fraction * reference - (box.borderBox ? extra * scale : 0));
  };
  return {resolve(axis == 0 ? box.minWidthLength : box.minHeightLength, 0),
          resolve(axis == 0 ? box.maxWidthLength : box.maxHeightLength, std::numeric_limits<qreal>::max())};
}
inline qreal cssPreferredRatio(const CssFormattingItem& item) {
  const auto& ratio = item.style.layout.aspectRatio;
  if (item.naturalSize && ratio.automatic && item.naturalSize->width() > 0 && item.naturalSize->height() > 0)
    return item.naturalSize->width() / item.naturalSize->height();
  return ratio.value;
}
// Return a border-box size. Intrinsic image ratios use the content box; authored
// ratios honor box-sizing. A forced size is an allocation from Flex/Grid.
inline QSizeF cssReplacedSize(const CssFormattingItem& item, qreal containingWidth, qreal containingHeight = -1, qreal forcedWidth = -1,
                              qreal forcedHeight = -1) {
  const auto& box = item.style.box;
  const auto padding = box.paddingLengths.used(box.padding, containingWidth, true);
  const qreal horizontal = padding.left() + padding.right() + box.borderLeftWidth + box.borderRightWidth;
  const qreal vertical = padding.top() + padding.bottom() + box.borderTopWidth + box.borderBottomWidth;
  const qreal ratio = cssPreferredRatio(item);
  const bool ratioBorderBox =
      box.borderBox && item.style.layout.aspectRatio.value > 0 && (!item.naturalSize || !item.style.layout.aspectRatio.automatic);
  const qreal ratioHorizontal = ratioBorderBox ? 0 : horizontal, ratioVertical = ratioBorderBox ? 0 : vertical;
  const auto specified = [](const CssLengthPercentage& length, qreal reference) {
    return length.status == CssLengthStatus::Valid && (!length.hasPercentage || reference >= 0);
  };
  const bool widthSet = forcedWidth >= 0 || specified(box.widthLength, containingWidth);
  const bool heightSet = forcedHeight >= 0 || specified(box.heightLength, containingHeight);
  qreal width = forcedWidth >= 0 ? forcedWidth
                : widthSet       ? qMax<qreal>(0, box.widthLength.used(containingWidth)) + (box.borderBox ? 0 : horizontal)
                                 : (item.naturalSize ? item.naturalSize->width() : item.intrinsic.maxContent) + horizontal;
  qreal height = forcedHeight >= 0 ? forcedHeight
                 : heightSet       ? qMax<qreal>(0, box.heightLength.used(containingHeight)) + (box.borderBox ? 0 : vertical)
                                   : (item.naturalSize ? item.naturalSize->height() : 0) + vertical;
  const auto widthFromHeight = [&] { return qMax<qreal>(0, height - ratioVertical) * ratio + ratioHorizontal; };
  const auto heightFromWidth = [&] { return ratio > 0 ? qMax<qreal>(0, width - ratioHorizontal) / ratio + ratioVertical : height; };
  if (!widthSet && heightSet && ratio > 0)
    width = widthFromHeight();
  else if (!heightSet && ratio > 0)
    height = heightFromWidth();
  const auto widths = cssContentConstraints(box, 0, containingWidth);
  const auto heights = cssContentConstraints(box, 1, containingHeight);
  const auto clampWidth = [&] { width = widths.clamp(qMax<qreal>(0, width - horizontal)) + horizontal; };
  const auto clampHeight = [&] { height = heights.clamp(qMax<qreal>(0, height - vertical)) + vertical; };
  clampWidth();
  if (!heightSet && ratio > 0) height = heightFromWidth();
  clampHeight();
  if (!widthSet && ratio > 0) {
    width = widthFromHeight();
    clampWidth();
  }
  return {qMax(horizontal, width), qMax(vertical, height)};
}
}  // namespace muffin
