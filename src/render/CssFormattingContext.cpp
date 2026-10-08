#include "render/CssFormattingContext.h"
#include "render/LayoutBox.h"
#include <yoga/Yoga.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>
#include <QTextLayout>

namespace muffin {
CssFormattingResult layoutFormattingItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items,
                                          qreal contentWidth, qreal contentHeight, qreal scale, const CssGridInheritance& inherited) {
  return container.layout.isGrid() ? layoutGridItems(container, items, contentWidth, contentHeight, scale, inherited)
                                   : layoutFlexItems(container, items, contentWidth, contentHeight, scale);
}
namespace {
YGAlign alignment(const QString& value, YGAlign normal) {
  if (value == "auto") return YGAlignAuto;
  if (value == "start" || value == "flex-start") return YGAlignFlexStart;
  if (value == "end" || value == "flex-end") return YGAlignFlexEnd;
  if (value == "center") return YGAlignCenter;
  if (value == "baseline") return YGAlignBaseline;
  if (value == "stretch") return YGAlignStretch;
  if (value == "space-between") return YGAlignSpaceBetween;
  if (value == "space-around") return YGAlignSpaceAround;
  if (value == "space-evenly") return YGAlignSpaceEvenly;
  return normal;
}
YGJustify justification(const QString& value) {
  if (value == "center") return YGJustifyCenter;
  if (value == "end" || value == "flex-end") return YGJustifyFlexEnd;
  if (value == "space-between") return YGJustifySpaceBetween;
  if (value == "space-around") return YGJustifySpaceAround;
  if (value == "space-evenly") return YGJustifySpaceEvenly;
  return YGJustifyFlexStart;
}
struct MeasureContext {
  const CssFormattingItem* item;
  qreal containingWidth = 0;
  qreal baseline = 0;
};
YGSize measureItem(YGNodeConstRef node, float width, YGMeasureMode widthMode, float, YGMeasureMode) {
  auto& context = *static_cast<MeasureContext*>(YGNodeGetContext(node));
  const auto result = context.item->measure(widthMode == YGMeasureModeUndefined ? -1 : qMax<qreal>(0, width), context.containingWidth, {});
  context.baseline = result.baseline;
  return {static_cast<float>(widthMode == YGMeasureModeExactly  ? width
                             : widthMode == YGMeasureModeAtMost ? qMin<qreal>(width, result.size.width())
                                                                : result.size.width()),
          static_cast<float>(result.size.height())};
}
float baselineItem(YGNodeConstRef node, float, float) {
  auto& context = *static_cast<MeasureContext*>(YGNodeGetContext(node));
  const qreal inset = YGNodeLayoutGetPadding(node, YGEdgeTop) + YGNodeLayoutGetBorder(node, YGEdgeTop);
  if (context.baseline == 0)
    context.baseline =
        context.item
            ->measure(YGNodeLayoutGetWidth(node) - YGNodeLayoutGetPadding(node, YGEdgeLeft) - YGNodeLayoutGetPadding(node, YGEdgeRight) -
                          YGNodeLayoutGetBorder(node, YGEdgeLeft) - YGNodeLayoutGetBorder(node, YGEdgeRight),
                      context.containingWidth, {})
            .baseline;
  return static_cast<float>(context.baseline + inset);
}
}  // namespace
CssIntrinsicMetrics intrinsicTextWidths(const QTextLayout& text, bool noWrap, bool anywhereMinimum) {
  QTextLayout intrinsic(text.text(), text.font());
  intrinsic.setFormats(text.formats());
  auto option = text.textOption();
  option.setWrapMode(noWrap ? QTextOption::NoWrap : anywhereMinimum ? QTextOption::WrapAnywhere : QTextOption::WordWrap);
  option.setAlignment(Qt::AlignLeft);
  intrinsic.setTextOption(option);
  intrinsic.beginLayout();
  while (true) {
    auto line = intrinsic.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(1e6);
  }
  intrinsic.endLayout();
  const qreal maximum = intrinsic.maximumWidth();
  return {noWrap ? maximum : intrinsic.minimumWidth(), maximum};
}
YGNode* createCssLayoutNode() {
  static const auto config = [] {
    auto* value = YGConfigNew();
    YGConfigSetUseWebDefaults(value, true);
    YGConfigSetPointScaleFactor(value, 0);  // preserve CSS subpixels for paint and input
    return value;
  }();
  return YGNodeNewWithConfig(config);
}
void applyCssFormattingStyle(YGNode* node, const CssLayoutStyle& style, qreal scale) {
  if (style.display == "none") YGNodeStyleSetDisplay(node, YGDisplayNone);
  const auto direction = style.direction == "column"           ? YGFlexDirectionColumn
                         : style.direction == "column-reverse" ? YGFlexDirectionColumnReverse
                         : style.direction == "row-reverse"    ? YGFlexDirectionRowReverse
                                                               : YGFlexDirectionRow;
  YGNodeStyleSetFlexDirection(node, direction);
  YGNodeStyleSetFlexWrap(node, style.wrap == "wrap" ? YGWrapWrap : style.wrap == "wrap-reverse" ? YGWrapWrapReverse : YGWrapNoWrap);
  YGNodeStyleSetJustifyContent(node, justification(style.justifyContent));
  YGNodeStyleSetAlignItems(node, alignment(style.alignItems, YGAlignStretch));
  YGNodeStyleSetAlignSelf(node, alignment(style.alignSelf, YGAlignAuto));
  YGNodeStyleSetAlignContent(node, alignment(style.alignContent, YGAlignStretch));
  YGNodeStyleSetFlexGrow(node, static_cast<float>(style.grow));
  YGNodeStyleSetFlexShrink(node, static_cast<float>(style.shrink));
  if (style.basis.status == CssLengthStatus::Valid) {
    if (style.basis.hasPercentage && style.basis.px == 0)
      YGNodeStyleSetFlexBasisPercent(node, static_cast<float>(style.basis.fraction * 100));
    else if (!style.basis.hasPercentage)
      YGNodeStyleSetFlexBasis(node, static_cast<float>(style.basis.px * scale));
  } else
    YGNodeStyleSetFlexBasisAuto(node);
  for (const auto& [gutter, length] : {std::pair{YGGutterRow, style.rowGap}, std::pair{YGGutterColumn, style.columnGap}}) {
    if (length.status != CssLengthStatus::Valid) continue;
    if (length.hasPercentage && length.px == 0)
      YGNodeStyleSetGapPercent(node, gutter, static_cast<float>(length.fraction * 100));
    else if (!length.hasPercentage)
      YGNodeStyleSetGap(node, gutter, static_cast<float>(qMax<qreal>(0, length.px * scale)));
  }
  const YGEdge edges[] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};
  for (int i = 0; i < 4; ++i)
    if (style.autoMargins[i]) YGNodeStyleSetMarginAuto(node, edges[i]);
  YGNodeStyleSetOverflow(node, style.overflowX == "scroll" || style.overflowY == "scroll" ? YGOverflowScroll
                               : style.clipsX() || style.clipsY()                         ? YGOverflowHidden
                                                                                          : YGOverflowVisible);
}
CssFormattingResult layoutFlexItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                                    qreal contentHeight, qreal scale) {
  auto* root = createCssLayoutNode();
  applyCssFormattingStyle(root, container.layout, scale);
  for (const auto& [gutter, length, reference] : {std::tuple{YGGutterColumn, container.layout.columnGap, contentWidth},
                                                  std::tuple{YGGutterRow, container.layout.rowGap, contentHeight}}) {
    if (length.status == CssLengthStatus::Valid && length.hasPercentage && length.px != 0 && reference >= 0)
      YGNodeStyleSetGap(root, gutter, static_cast<float>(qMax<qreal>(0, length.px * scale + length.fraction * reference)));
  }
  YGNodeStyleSetWidth(root, static_cast<float>(contentWidth));
  if (contentHeight >= 0) YGNodeStyleSetHeight(root, static_cast<float>(contentHeight));
  std::vector<YGNode*> nodes(items.size());
  std::vector<MeasureContext> contexts(items.size());
  std::vector<qreal> minimumWidths(items.size(), -1), minimumHeights(items.size(), -1);
  std::vector<size_t> order(items.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return items[a].style.layout.order < items[b].style.layout.order; });
  const bool row = container.layout.direction.startsWith("row");
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    const auto& style = item.style;
    const auto& b = style.box;
    auto* node = nodes[i] = createCssLayoutNode();
    applyCssFormattingStyle(node, style.layout, scale);
    YGNodeStyleSetBoxSizing(node, b.borderBox ? YGBoxSizingBorderBox : YGBoxSizingContentBox);
    const auto padding = b.paddingLengths.used(b.padding, contentWidth / scale, true);
    const auto margin = b.marginLengths.used(b.margin, contentWidth / scale);
    const qreal pads[] = {padding.top(), padding.right(), padding.bottom(), padding.left()};
    const qreal margins[] = {margin.top(), margin.right(), margin.bottom(), margin.left()};
    const qreal borders[] = {b.borderTopWidth, b.borderRightWidth, b.borderBottomWidth, b.borderLeftWidth};
    const YGEdge edges[] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};
    for (int e = 0; e < 4; ++e) {
      YGNodeStyleSetPadding(node, edges[e], static_cast<float>(pads[e] * scale));
      YGNodeStyleSetBorder(node, edges[e], static_cast<float>(borders[e] * scale));
      if (!style.layout.autoMargins[e]) YGNodeStyleSetMargin(node, edges[e], static_cast<float>(margins[e] * scale));
    }
    const auto size = [&](int index, const CssLengthPercentage& length, auto set) {
      qreal value = -1;
      const bool height = index == 1 || index >= 4;
      if (length.status == CssLengthStatus::Valid && (!height || !length.hasPercentage || contentHeight >= 0))
        value = length.px * scale + length.fraction * (height ? contentHeight : contentWidth);
      else if (height &&
               (style.layout.sizes[index] == CssIntrinsicSize::MinContent || style.layout.sizes[index] == CssIntrinsicSize::MaxContent ||
                style.layout.sizes[index] == CssIntrinsicSize::FitContent))
        value = item.measure(contentWidth, contentWidth, {}).size.height();
      else if (style.layout.sizes[index] == CssIntrinsicSize::MinContent)
        value = item.intrinsic.minContent;
      else if (style.layout.sizes[index] == CssIntrinsicSize::MaxContent)
        value = item.intrinsic.maxContent;
      else if (style.layout.sizes[index] == CssIntrinsicSize::FitContent)
        value = qMax(item.intrinsic.minContent, qMin(item.intrinsic.maxContent, contentWidth));
      if (value >= 0 && b.borderBox && style.layout.sizes[index] != CssIntrinsicSize::Length)
        value += height ? (pads[0] + pads[2] + borders[0] + borders[2]) * scale : (pads[1] + pads[3] + borders[1] + borders[3]) * scale;
      if (value >= 0) set(node, static_cast<float>(value));
    };
    size(0, b.widthLength, YGNodeStyleSetWidth);
    size(1, b.heightLength, YGNodeStyleSetHeight);
    size(2, b.minWidthLength, [&](YGNode*, float value) { minimumWidths[i] = value; });
    size(3, b.maxWidthLength, YGNodeStyleSetMaxWidth);
    size(4, b.minHeightLength, [&](YGNode*, float value) { minimumHeights[i] = value; });
    size(5, b.maxHeightLength, YGNodeStyleSetMaxHeight);
    if (row && style.layout.sizes[2] == CssIntrinsicSize::Auto &&
        (style.layout.overflowX == "visible" || style.layout.overflowX == "clip")) {
      qreal minimum = item.intrinsic.minContent;
      const qreal extra = b.borderBox ? (pads[1] + pads[3] + borders[1] + borders[3]) * scale : 0;
      if (b.widthLength.status == CssLengthStatus::Valid)
        minimum = qMin(minimum, qMax<qreal>(0, b.widthLength.px * scale + b.widthLength.fraction * contentWidth - extra));
      if (b.maxWidthLength.status == CssLengthStatus::Valid)
        minimum = qMin(minimum, qMax<qreal>(0, b.maxWidthLength.px * scale + b.maxWidthLength.fraction * contentWidth - extra));
      minimumWidths[i] = minimum + extra;
    }
    if (!row && style.layout.sizes[4] == CssIntrinsicSize::Auto &&
        (style.layout.overflowY == "visible" || style.layout.overflowY == "clip")) {
      const qreal horizontal = (pads[1] + pads[3] + borders[1] + borders[3]) * scale;
      const qreal vertical = (pads[0] + pads[2] + borders[0] + borders[2]) * scale;
      const qreal measuredWidth =
          b.widthLength.status == CssLengthStatus::Valid
              ? qMax<qreal>(0, b.widthLength.px * scale + b.widthLength.fraction * contentWidth - (b.borderBox ? horizontal : 0))
              : qMax<qreal>(0, contentWidth - horizontal);
      const auto measured = item.measure(measuredWidth, contentWidth, {});
      qreal minimum = measured.size.height();
      if (b.heightLength.status == CssLengthStatus::Valid && (!b.heightLength.hasPercentage || contentHeight >= 0))
        minimum = qMin(minimum,
                       qMax<qreal>(0, b.heightLength.px * scale + b.heightLength.fraction * contentHeight - (b.borderBox ? vertical : 0)));
      if (b.maxHeightLength.status == CssLengthStatus::Valid && (!b.maxHeightLength.hasPercentage || contentHeight >= 0))
        minimum = qMin(minimum, qMax<qreal>(0, b.maxHeightLength.px * scale + b.maxHeightLength.fraction * contentHeight -
                                                   (b.borderBox ? vertical : 0)));
      minimumHeights[i] = minimum + (b.borderBox ? vertical : 0);
    }
    if (style.layout.basisKeyword == "content" || style.layout.basisKeyword == "max-content")
      YGNodeStyleSetFlexBasis(node, static_cast<float>(item.intrinsic.maxContent));
    if (style.layout.basisKeyword == "min-content") YGNodeStyleSetFlexBasis(node, static_cast<float>(item.intrinsic.minContent));
    if (style.layout.basis.hasPercentage && style.layout.basis.px != 0 && (row || contentHeight >= 0))
      YGNodeStyleSetFlexBasis(
          node, static_cast<float>(style.layout.basis.px * scale + style.layout.basis.fraction * (row ? contentWidth : contentHeight)));
    contexts[i] = {&item, contentWidth};
    YGNodeSetContext(node, &contexts[i]);
    YGNodeSetMeasureFunc(node, measureItem);
    YGNodeSetBaselineFunc(node, baselineItem);
  }
  for (size_t i = 0; i < order.size(); ++i) YGNodeInsertChild(root, nodes[order[i]], static_cast<uint32_t>(i));
  const auto calculate = [&] {
    YGNodeCalculateLayout(root, static_cast<float>(contentWidth), contentHeight < 0 ? YGUndefined : static_cast<float>(contentHeight),
                          YGDirectionLTR);
  };
  calculate();
  // Yoga floors flex bases by min sizes before distributing free space. CSS
  // distributes from the unclamped bases, then freezes items violating min/max.
  // Apply minimums only to violating items and repeat the shared solve.
  std::vector<bool> frozenWidth(items.size()), frozenHeight(items.size());
  for (size_t pass = 0; pass <= items.size(); ++pass) {
    bool changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
      auto* node = nodes[i];
      const qreal horizontal = YGNodeLayoutGetPadding(node, YGEdgeLeft) + YGNodeLayoutGetPadding(node, YGEdgeRight) +
                               YGNodeLayoutGetBorder(node, YGEdgeLeft) + YGNodeLayoutGetBorder(node, YGEdgeRight);
      const qreal vertical = YGNodeLayoutGetPadding(node, YGEdgeTop) + YGNodeLayoutGetPadding(node, YGEdgeBottom) +
                             YGNodeLayoutGetBorder(node, YGEdgeTop) + YGNodeLayoutGetBorder(node, YGEdgeBottom);
      if (!frozenWidth[i] && minimumWidths[i] >= 0 &&
          YGNodeLayoutGetWidth(node) + .001 < minimumWidths[i] + (items[i].style.box.borderBox ? 0 : horizontal)) {
        YGNodeStyleSetMinWidth(node, static_cast<float>(minimumWidths[i]));
        if (row) {
          YGNodeStyleSetFlexBasis(node, static_cast<float>(minimumWidths[i]));
          YGNodeStyleSetFlexGrow(node, 0);
          YGNodeStyleSetFlexShrink(node, 0);
        }
        frozenWidth[i] = true;
        changed = true;
      }
      if (!frozenHeight[i] && minimumHeights[i] >= 0 &&
          YGNodeLayoutGetHeight(node) + .001 < minimumHeights[i] + (items[i].style.box.borderBox ? 0 : vertical)) {
        YGNodeStyleSetMinHeight(node, static_cast<float>(minimumHeights[i]));
        if (!row) {
          YGNodeStyleSetFlexBasis(node, static_cast<float>(minimumHeights[i]));
          YGNodeStyleSetFlexGrow(node, 0);
          YGNodeStyleSetFlexShrink(node, 0);
        }
        frozenHeight[i] = true;
        changed = true;
      }
    }
    if (!changed) break;
    calculate();
  }
  CssFormattingResult result;
  result.containingWidths.assign(items.size(), contentWidth);
  result.inheritedGrids.resize(items.size());
  result.size = {YGNodeLayoutGetWidth(root), YGNodeLayoutGetHeight(root)};
  for (auto* node : nodes)
    result.items.emplace_back(YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node), YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node));
  YGNodeFreeRecursive(root);
  return result;
}
}  // namespace muffin
