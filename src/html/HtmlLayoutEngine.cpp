#include "html/HtmlLayoutEngine.h"
#include "html/HtmlTextMeasurer.h"
#include "render/ImageLoader.h"
#include "render/CssSizing.h"

#include <yoga/Yoga.h>

#include "render/TextLayout.h"
#include <QMap>
#include <QVector>
#include <QScopeGuard>

#include <cmath>
#include <numeric>

namespace muffin::html {
namespace {

constexpr qreal kDefaultImageWidth = 100.0;
constexpr qreal kDefaultImageHeight = 80.0;

YGSize measureTextCallback(YGNodeConstRef nodeRef, float width, YGMeasureMode widthMode, float, YGMeasureMode) {
  auto* ctx = static_cast<YogaContext*>(YGNodeGetContext(nodeRef));
  if (!ctx) {
    return {0, 0};
  }

  auto* box = ctx->box;
  if (ctx->measurer && ctx->textLayouts) {
    const qreal available = widthMode == YGMeasureModeUndefined ? 1e6 : qMax<qreal>(0, width);
    auto layout = ctx->pre ? ctx->measurer->buildPreLayout(*box, ctx->fontSize, available)
                           : ctx->measurer->buildInlineLayout(*box, ctx->fontSize, available, box->style().textAlign);
    const auto measured = QSizeF(layout->width, layout->height);
    ctx->textLayouts->at(static_cast<size_t>(box->textLayoutIndex())) = std::move(layout);
    return {static_cast<float>(widthMode == YGMeasureModeExactly ? width : qMin<qreal>(available, measured.width())),
            static_cast<float>(measured.height())};
  }
  const qreal textWidth = box->geometry().width;
  const qreal textHeight = box->geometry().height;
  const qreal measuredWidth =
      widthMode == YGMeasureModeExactly || widthMode == YGMeasureModeAtMost ? qMin<qreal>(textWidth, width) : textWidth;

  TextFontMetrics fm(box->style().font);
  qreal minHeight = fm.height();

  return {
      static_cast<float>(std::ceil(measuredWidth)),
      static_cast<float>(std::max(textHeight, minHeight))};
}

void freeContextTree(YGNodeRef node) {
  auto* ctx = static_cast<YogaContext*>(YGNodeGetContext(node));
  delete ctx;
  YGNodeSetContext(node, nullptr);

  uint32_t childCount = YGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; ++i) {
    freeContextTree(YGNodeGetChild(node, i));
  }
}

qreal horizontalBoxExtent(const HtmlComputedStyle& style) {
  return style.padding.left() + style.padding.right() + style.borderWidth.left() + style.borderWidth.right();
}

qreal verticalBoxExtent(const HtmlComputedStyle& style) {
  return style.padding.top() + style.padding.bottom() + style.borderWidth.top() + style.borderWidth.bottom();
}

ThemeElementStyle formattingStyle(const HtmlBox& box, qreal containingWidth) {
  auto computed = box.style().computed;
  const auto& childStyle = box.style();
  computed.box.padding = childStyle.paddingLengths.used(childStyle.padding, containingWidth, true);
  computed.box.margin = childStyle.marginLengths.used(childStyle.margin, containingWidth);
  computed.box.paddingLengths = childStyle.paddingLengths;
  computed.box.marginLengths = childStyle.marginLengths;
  computed.box.widthLength = childStyle.widthLength;
  computed.box.heightLength = childStyle.heightLength;
  computed.box.minWidthLength = childStyle.minWidthLength;
  computed.box.maxWidthLength = childStyle.maxWidthLength;
  if (childStyle.width >= 0 && computed.box.widthLength.status != CssLengthStatus::Valid)
    computed.box.widthLength = {CssLengthStatus::Valid, childStyle.width};
  if (childStyle.height >= 0 && computed.box.heightLength.status != CssLengthStatus::Valid)
    computed.box.heightLength = {CssLengthStatus::Valid, childStyle.height};
  computed.box.borderBox = childStyle.borderBox;
  computed.box.borderLeftWidth = childStyle.borderWidth.left();
  computed.box.borderRightWidth = childStyle.borderWidth.right();
  computed.box.borderTopWidth = childStyle.borderWidth.top();
  computed.box.borderBottomWidth = childStyle.borderWidth.bottom();
  return computed;
}

QSizeF naturalImageSize(const HtmlBox& box) {
  const auto natural = ImageLoader::instance().image(box.src()).size();
  return QSizeF(natural.width() > 0 ? natural.width() : kDefaultImageWidth, natural.height() > 0 ? natural.height() : kDefaultImageHeight) *
         box.style().cssScale * box.style().zoom;
}

bool isTableCellBox(const HtmlBox& box) {
  return box.style().visible && box.style().display == HtmlDisplay::TableCell;
}

bool isTableRowBox(const HtmlBox& box) {
  return box.style().visible && box.style().display == HtmlDisplay::TableRow;
}

void collectTableRows(HtmlBox& box, QVector<HtmlBox*>& rows) {
  if (!box.style().visible || box.style().display == HtmlDisplay::None || box.tag() == HtmlTag::Caption) {
    return;
  }
  if (isTableRowBox(box)) {
    rows.push_back(&box);
    return;
  }
  for (auto& child : box.children()) {
    collectTableRows(*child, rows);
  }
}

void collectRowCells(HtmlBox& row, QVector<HtmlBox*>& cells) {
  for (auto& child : row.children()) {
    if (isTableCellBox(*child)) {
      cells.push_back(child.get());
    }
  }
}

HtmlBox* findTableCaption(HtmlBox& table) {
  for (auto& child : table.children()) {
    if (child->style().visible && child->tag() == HtmlTag::Caption) {
      return child.get();
    }
  }
  return nullptr;
}

bool isTableInternalBox(const HtmlBox& box) {
  return box.style().display == HtmlDisplay::TableRowGroup ||
         box.style().display == HtmlDisplay::TableRow ||
         box.style().display == HtmlDisplay::TableCell;
}

bool isRenderableChildFor(const HtmlBox& parent, const HtmlBox& child) {
  if (child.style().display == HtmlDisplay::None || !child.style().visible) {
    return false;
  }
  if (parent.tag() == HtmlTag::Details && !parent.detailsOpen() && child.tag() != HtmlTag::Summary) {
    return false;
  }
  return true;
}

bool containsReplacedInlineContent(const HtmlBox& box) {
  if (box.style().computed.layout.display == "inline-flex" || box.style().computed.layout.display == "inline-grid") return false;
  if (box.tag() == HtmlTag::Image || box.style().display == HtmlDisplay::InlineBlock) {
    return true;
  }
  for (const auto& child : box.children()) {
    if (child->style().display == HtmlDisplay::None || !child->style().visible) {
      continue;
    }
    if (containsReplacedInlineContent(*child)) {
      return true;
    }
  }
  return false;
}

}  // namespace

HtmlLayoutEngine::HtmlLayoutEngine() {
  measurer_.setAtomicLayout([this](HtmlBox& box, qreal width) { return layoutAtomicInline(box, width); });
}
HtmlTextLayout::AtomicInline HtmlLayoutEngine::layoutAtomicInline(HtmlBox& box, qreal width) {
  const auto style = formattingStyle(box, width);
  const auto inset = LayoutBox::insets(style.box);
  const auto margin = style.box.marginLengths.used(style.box.margin, width);
  const auto intrinsic = intrinsicMetrics(box);
  qreal outerWidth =
      style.box.widthLength.status == CssLengthStatus::Valid
          ? style.box.widthLength.used(width) + (style.box.borderBox ? 0 : inset.left() + inset.right())
          : qMax(intrinsic.minContent, qMin(intrinsic.maxContent, width - inset.left() - inset.right() - margin.left() - margin.right())) +
                inset.left() + inset.right();
  outerWidth = cssContentConstraints(style.box, 0, width).clamp(qMax<qreal>(0, outerWidth - inset.left() - inset.right())) + inset.left() +
               inset.right();
  const auto height = layoutAllocatedBox(box, {outerWidth, -1}, *activeTextLayouts_);
  const qreal baseline = box.firstBaseline >= 0 ? box.firstBaseline : height;
  return {&box, 0, {outerWidth, height}, baseline, margin, {}};
}
HtmlLayoutEngine::~HtmlLayoutEngine() = default;

void HtmlLayoutEngine::layout(
    HtmlBox& root,
    qreal availableWidth,
    qreal baseFontSize,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts,
    const HtmlColorPalette& palette) {
  textLayouts.clear();
  measurer_.setDefaultTextColor(palette.text);
  auto* previousLayouts = activeTextLayouts_;
  activeTextLayouts_ = &textLayouts;
  const auto restoreLayouts = qScopeGuard([&] { activeTextLayouts_ = previousLayouts; });

  YGNode* rootNode = createYogaNode(root, baseFontSize, availableWidth, textLayouts);

  YGNodeCalculateLayout(rootNode, static_cast<float>(availableWidth), YGUndefined, YGDirectionLTR);

  activeTextLayouts_ = &textLayouts;
  readLayoutBack(root, rootNode);
  const auto snapshot = [&](auto&& self, HtmlBox& box) -> void {
    const auto& style = box.style();
    auto used = style.computed.box;
    used.padding = style.padding;
    used.borderLeftWidth = style.borderWidth.left();
    used.borderTopWidth = style.borderWidth.top();
    used.borderRightWidth = style.borderWidth.right();
    used.borderBottomWidth = style.borderWidth.bottom();
    used.borderRadius = style.borderRadius;
    const auto& geometry = box.geometry();
    box.layoutBox = LayoutBox::place(box.cssTag, style.computed, used, QRectF(geometry.left, geometry.top, geometry.width, geometry.height),
                                     style.font);
    for (const auto& child : box.children()) self(self, *child);
  };
  snapshot(snapshot, root);

  // Free all YogaContext objects before freeing nodes
  // (YGNodeFreeRecursive doesn't call context destructors)
  freeContextTree(rootNode);

  YGNodeFreeRecursive(rootNode);
}

YGNode* HtmlLayoutEngine::createYogaNode(
    HtmlBox& box,
    qreal fontSize,
    qreal availableWidth,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  auto* previousLayouts = activeTextLayouts_;
  activeTextLayouts_ = &textLayouts;
  const auto restoreLayouts = qScopeGuard([&] { activeTextLayouts_ = previousLayouts; });
  YGNode* node = createCssLayoutNode();

  auto& style = box.style();

  // Resolve only after the parent content width is known. Keep expressions on
  // the box so a subsequent layout at a different width recomputes them.
  style.margin = style.marginLengths.used(style.margin, availableWidth);
  style.padding = style.paddingLengths.used(style.padding, availableWidth, true);
  if (style.widthLength.status == CssLengthStatus::Valid) style.width = qMax<qreal>(0, style.widthLength.used(availableWidth));
  if (style.heightLength.status == CssLengthStatus::Valid && !style.heightLength.hasPercentage)
    style.height = qMax<qreal>(0, style.heightLength.px);

  // Skip invisible boxes
  if (!style.visible || style.display == HtmlDisplay::None) {
    YGNodeStyleSetDisplay(node, YGDisplayNone);
    box.style().visible = false;
    return node;
  }

  // Apply box model styles
  applyBoxStyle(node, style);
  YGNodeStyleSetBoxSizing(node, style.borderBox ? YGBoxSizingBorderBox : YGBoxSizingContentBox);
  if (style.minWidthLength.status == CssLengthStatus::Valid)
    YGNodeStyleSetMinWidth(node, static_cast<float>(qMax<qreal>(0, style.minWidthLength.used(availableWidth))));
  if (style.maxWidthLength.status == CssLengthStatus::Valid)
    YGNodeStyleSetMaxWidth(node, static_cast<float>(qMax<qreal>(0, style.maxWidthLength.used(availableWidth))));
  if ((style.display == HtmlDisplay::TableRowGroup ||
       style.display == HtmlDisplay::TableRow) &&
      style.width < 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(qMax<qreal>(1.0, availableWidth)));
  }

  if (style.display == HtmlDisplay::Table) {
    layoutTableBox(box, availableWidth, textLayouts);
    YGNodeStyleSetWidth(node, static_cast<float>(box.geometry().width));
    YGNodeStyleSetHeight(node, static_cast<float>(box.geometry().height));
    return node;
  }

  // Determine if this box contains only inline children (text content)
  if ((style.display == HtmlDisplay::Flex || style.display == HtmlDisplay::Grid)) {
    layoutFormattingBox(box, availableWidth, textLayouts);
    YGNodeStyleSetBoxSizing(node, YGBoxSizingBorderBox);
    YGNodeStyleSetWidth(node, static_cast<float>(box.geometry().width));
    YGNodeStyleSetHeight(node, static_cast<float>(box.geometry().height));
    return node;
  }
  bool hasBlockChildren = false;
  bool hasInlineContent = false;
  bool hasReplacedInlineContent = false;
  for (const auto& child : box.children()) {
    if (!isRenderableChildFor(box, *child)) {
      continue;
    }
    if (child->style().display == HtmlDisplay::Block ||
        ((child->style().display == HtmlDisplay::Flex || child->style().display == HtmlDisplay::Grid) && !child->isInlineLevel()) ||
        child->style().display == HtmlDisplay::Table || child->style().display == HtmlDisplay::ListItem) {
      hasBlockChildren = true;
    }
    if (child->isInlineLevel() || child->isTextRun()) {
      hasInlineContent = true;
    }
    if (containsReplacedInlineContent(*child)) {
      hasReplacedInlineContent = true;
    }
  }

  if (box.tag() == HtmlTag::Image) {
    const auto item = formattingItem(box, availableWidth);
    const auto imageSize = cssReplacedSize(item, availableWidth);
    YGNodeStyleSetBoxSizing(node, YGBoxSizingBorderBox);
    YGNodeStyleSetWidth(node, static_cast<float>(imageSize.width()));
    YGNodeStyleSetHeight(node, static_cast<float>(imageSize.height()));
    box.geometry().width = imageSize.width();
    box.geometry().height = imageSize.height();
  } else if (box.tag() == HtmlTag::Pre && hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildPreLayout(
        box, style.fontSize, qMax<qreal>(1.0, availableWidth - horizontalBoxExtent(style)));
    qreal textHeight = textLayout->height;
    qreal textWidth = textLayout->width;

    auto* ctx = new YogaContext{&box, style.fontSize, &measurer_, &textLayouts, true};
    YGNodeSetContext(node, ctx);

    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
    box.geometry().width = textWidth;
    box.geometry().height = textHeight;

    YGNodeSetMeasureFunc(node, measureTextCallback);
  } else if ((hasInlineContent || box.isTextRun()) && !hasBlockChildren && !hasReplacedInlineContent) {
    // This is an inline formatting context — measure text as one unit
    auto textLayout = measurer_.buildInlineLayout(
        box, style.fontSize, qMax<qreal>(1.0, availableWidth - style.padding.left() - style.padding.right()),
        style.textAlign);
    qreal textHeight = textLayout->height;
    qreal textWidth = textLayout->width;

    // Set measurement callback data
    auto* ctx = new YogaContext{&box, style.fontSize, &measurer_, &textLayouts};
    YGNodeSetContext(node, ctx);

    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
    box.geometry().width = textWidth;
    box.geometry().height = textHeight;

    // Use Yoga measurement callback for text
    YGNodeSetMeasureFunc(node, measureTextCallback);
  } else {
    // Block-level container — recurse into children
    if (box.isInlineLevel() && hasReplacedInlineContent && !hasBlockChildren) {
      YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
      YGNodeStyleSetFlexWrap(node, YGWrapWrap);
    } else if (style.display != HtmlDisplay::Flex && style.display != HtmlDisplay::TableRow) {
      YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
    }
    int childIndex = 0;
    int tableCellCount = 0;
    if (style.display == HtmlDisplay::TableRow) {
      for (const auto& child : box.children()) {
        if (child->style().visible && child->style().display == HtmlDisplay::TableCell) {
          ++tableCellCount;
        }
      }
    }
    for (auto& child : box.children()) {
      if (child->style().display == HtmlDisplay::None || !child->style().visible) {
        continue;
      }
      // Collapsed <details>: hide non-summary children
      if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
        continue;
      }
      const qreal borderPadding = style.padding.left() + style.padding.right() + style.borderWidth.left() + style.borderWidth.right();
      qreal childAvailableWidth = style.width >= 0
                                      ? (style.borderBox ? qMax<qreal>(0, style.width - borderPadding) : style.width)
                                      : qMax<qreal>(0, availableWidth - style.margin.left() - style.margin.right() - borderPadding);
      if (style.display == HtmlDisplay::TableRow && child->style().display == HtmlDisplay::TableCell && tableCellCount > 0) {
        childAvailableWidth = qMax<qreal>(1.0, childAvailableWidth / tableCellCount);
      }
      YGNode* childNode = createYogaNode(*child, style.fontSize, childAvailableWidth, textLayouts);
      YGNodeInsertChild(node, childNode, childIndex);
      ++childIndex;
    }
  }

  return node;
}

void HtmlLayoutEngine::applyBoxStyle(YGNode* node, const HtmlComputedStyle& style) {
  YGNodeStyleSetBorder(node, YGEdgeTop, static_cast<float>(style.borderWidth.top()));
  YGNodeStyleSetBorder(node, YGEdgeRight, static_cast<float>(style.borderWidth.right()));
  YGNodeStyleSetBorder(node, YGEdgeBottom, static_cast<float>(style.borderWidth.bottom()));
  YGNodeStyleSetBorder(node, YGEdgeLeft, static_cast<float>(style.borderWidth.left()));
  // Margin (use percent API when a percentage was specified, pixel API otherwise)
  if (style.marginPercent.top() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeTop, static_cast<float>(style.marginPercent.top()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeTop, static_cast<float>(style.margin.top()));
  }
  if (style.marginPercent.bottom() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeBottom, static_cast<float>(style.marginPercent.bottom()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeBottom, static_cast<float>(style.margin.bottom()));
  }
  if (style.marginPercent.left() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeLeft, static_cast<float>(style.marginPercent.left()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeLeft, static_cast<float>(style.margin.left()));
  }
  if (style.marginPercent.right() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeRight, static_cast<float>(style.marginPercent.right()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeRight, static_cast<float>(style.margin.right()));
  }

  // Padding (use percent API when a percentage was specified, pixel API otherwise)
  if (style.paddingPercent.top() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeTop, static_cast<float>(style.paddingPercent.top()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeTop, static_cast<float>(style.padding.top()));
  }
  if (style.paddingPercent.bottom() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeBottom, static_cast<float>(style.paddingPercent.bottom()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeBottom, static_cast<float>(style.padding.bottom()));
  }
  if (style.paddingPercent.left() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeLeft, static_cast<float>(style.paddingPercent.left()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeLeft, static_cast<float>(style.padding.left()));
  }
  if (style.paddingPercent.right() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeRight, static_cast<float>(style.paddingPercent.right()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeRight, static_cast<float>(style.padding.right()));
  }

  // Width/Height
  if (style.widthPercent >= 0) {
    YGNodeStyleSetWidthPercent(node, static_cast<float>(style.widthPercent));
  } else if (style.width >= 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(style.width));
  }
  if (style.height >= 0) {
    YGNodeStyleSetHeight(node, static_cast<float>(style.height));
  } else if (style.heightLength.status == CssLengthStatus::Valid && style.heightLength.hasPercentage && style.heightLength.px == 0) {
    YGNodeStyleSetHeightPercent(node, static_cast<float>(style.heightLength.fraction * 100));
  }

  if (style.display == HtmlDisplay::Table || style.display == HtmlDisplay::TableRowGroup) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
  } else if (style.display == HtmlDisplay::TableRow) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
  } else if (style.display == HtmlDisplay::TableCell) {
    YGNodeStyleSetFlexGrow(node, 1);
    YGNodeStyleSetFlexShrink(node, 1);
  }

  // Default flex behavior: grow to fill available space in column layout
  if (style.width < 0 && style.display != HtmlDisplay::TableCell) {
    YGNodeStyleSetFlexGrow(node, 0);
    YGNodeStyleSetFlexShrink(node, 0);
  }
}

CssIntrinsicMetrics HtmlLayoutEngine::intrinsicMetrics(HtmlBox& box) {
  const auto& width = box.style().widthLength;
  if (width.status == CssLengthStatus::Valid && !width.hasPercentage) {
    const auto value = qMax<qreal>(0, width.px - (box.style().borderBox ? horizontalBoxExtent(box.style()) : 0));
    return {value, value};
  }
  if (box.style().computed.layout.establishesFormattingContext()) {
    std::vector<CssFormattingItem> items;
    for (const auto& child : box.children()) {
      if (isRenderableChildFor(box, *child) && !(child->isTextRun() && child->text().trimmed().isEmpty()))
        items.push_back(formattingItem(*child, 0));
    }
    return box.style().computed.layout.isGrid() ? intrinsicGridWidths(formattingStyle(box, 0), items)
                                                : intrinsicFlexWidths(formattingStyle(box, 0), items);
  }
  bool blockChildren = false;
  for (const auto& child : box.children())
    blockChildren = blockChildren || child->style().display == HtmlDisplay::Block ||
                    (child->style().display == HtmlDisplay::Flex || child->style().display == HtmlDisplay::Grid);
  if (!blockChildren && box.tag() != HtmlTag::Image) {
    const auto text = box.tag() == HtmlTag::Pre ? measurer_.buildPreLayout(box, box.style().fontSize, 1e6)
                                                : measurer_.buildInlineLayout(box, box.style().fontSize, 1e6);
    if (text->layout)
      return intrinsicTextWidths(
          *text->layout, box.style().whiteSpace == HtmlWhiteSpace::Pre,
          box.style().computed.layout.overflowWrap == "anywhere" || box.style().computed.layout.wordBreak == "break-all");
  }
  CssIntrinsicMetrics result;
  for (const auto& child : box.children()) {
    if (!isRenderableChildFor(box, *child)) continue;
    auto metrics = intrinsicMetrics(*child);
    const auto extra = horizontalBoxExtent(child->style());
    metrics.minContent += extra;
    metrics.maxContent += extra;
    result.minContent = qMax(result.minContent, metrics.minContent);
    if (box.style().computed.layout.isFlex() && box.style().computed.layout.direction.startsWith("row"))
      result.maxContent += metrics.maxContent;
    else
      result.maxContent = qMax(result.maxContent, metrics.maxContent);
  }
  if (box.tag() == HtmlTag::Image) {
    const auto natural = naturalImageSize(box);
    result = {natural.width(), natural.width()};
  }
  return result;
}

qreal HtmlLayoutEngine::layoutAllocatedBox(HtmlBox& box, QSizeF size, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts,
                                           const CssGridInheritance& inherited) {
  auto* previousLayouts = activeTextLayouts_;
  activeTextLayouts_ = &textLayouts;
  const auto restoreLayouts = qScopeGuard([&] { activeTextLayouts_ = previousLayouts; });
  const auto savedGrid = gridInheritance_.value(&box);
  const bool hadGrid = gridInheritance_.contains(&box);
  gridInheritance_.insert(&box, inherited);
  const auto saved = box.style();
  auto& style = box.style();
  style.widthLength = {};
  style.heightLength = {};
  style.minWidthLength = {};
  style.maxWidthLength = {};
  style.width = size.width();
  style.widthPercent = -1;
  style.height = size.height();
  style.borderBox = true;
  style.margin = {};
  style.marginLengths = {};
  style.marginPercent = {-1, -1, -1, -1};
  // The item padding has already resolved against its flex containing block,
  // not against the width allocated to the item itself.
  style.paddingLengths = {};
  style.paddingPercent = {-1, -1, -1, -1};
  auto* node = createYogaNode(box, style.fontSize, size.width(), textLayouts);
  YGNodeStyleSetFlexShrink(node, 0);
  YGNodeCalculateLayout(node, static_cast<float>(size.width()), YGUndefined, YGDirectionLTR);
  readLayoutBack(box, node);
  freeContextTree(node);
  YGNodeFreeRecursive(node);
  const auto height = box.geometry().height;
  const auto padding = style.padding;
  box.style() = saved;
  box.style().padding = padding;
  if (hadGrid)
    gridInheritance_.insert(&box, savedGrid);
  else
    gridInheritance_.remove(&box);
  return height;
}

CssFormattingItem HtmlLayoutEngine::formattingItem(HtmlBox& box, qreal containingWidth) {
  auto computed = formattingStyle(box, containingWidth);
  auto& childStyle = box.style();
  childStyle.padding = computed.box.padding;
  auto* childPtr = &box;
  CssFormattingItem item;
  item.style = computed;
  if (computed.layout.establishesFormattingContext() && !box.children().empty()) {
    item.children.emplace();
    for (const auto& child : box.children()) {
      if (isRenderableChildFor(box, *child) && !(child->isTextRun() && child->text().trimmed().isEmpty()))
        item.children->push_back(formattingItem(*child, containingWidth));
    }
    item.intrinsic =
        computed.layout.isGrid() ? intrinsicGridWidths(computed, *item.children) : intrinsicFlexWidths(computed, *item.children);
  } else
    item.intrinsic = intrinsicMetrics(box);
  if (box.tag() == HtmlTag::Image) item.naturalSize = naturalImageSize(box);
  if (item.naturalSize || cssPreferredRatio(item) > 0) {
    const auto preferred = cssReplacedSize(item, -1);
    const auto inset = childStyle.padding + childStyle.borderWidth;
    if (item.naturalSize || (computed.box.heightLength.status == CssLengthStatus::Valid && !computed.box.heightLength.hasPercentage))
      item.intrinsic = {qMax<qreal>(0, preferred.width() - inset.left() - inset.right()),
                        qMax<qreal>(0, preferred.width() - inset.left() - inset.right())};
  }
  const auto intrinsic = item.intrinsic;
  item.measure = [this, childPtr, intrinsic](const CssMeasureRequest& request) {
    const auto width = request.width, reference = request.containingWidth;
    const auto& inherited = request.inherited;
    auto& target = childPtr->style();
    target.padding = target.paddingLengths.used(target.padding, reference, true);
    const auto inset = target.padding + target.borderWidth;
    std::vector<std::unique_ptr<HtmlTextLayout>> measured;
    qreal allocatedHeight = request.allocatedHeight;
    if (allocatedHeight < 0 && request.containingHeight >= 0 && target.heightLength.status == CssLengthStatus::Valid)
      allocatedHeight =
          qMax<qreal>(0, target.heightLength.used(request.containingHeight)) + (target.borderBox ? 0 : inset.top() + inset.bottom());
    const auto height =
        layoutAllocatedBox(*childPtr, {width < 0 ? 1e6 : width + inset.left() + inset.right(), allocatedHeight}, measured, inherited);
    qreal baseline = childPtr->hasTextContent()
                         ? (childPtr->firstBaseline >= 0 ? childPtr->firstBaseline - inset.top() : height - inset.top() - inset.bottom())
                         : -1;
    qreal lastBaseline = childPtr->hasTextContent() && childPtr->lastBaseline >= 0 ? childPtr->lastBaseline - inset.top() : baseline;
    if (childPtr->textLayoutIndex() >= 0 && childPtr->textLayoutIndex() < static_cast<int>(measured.size())) {
      const auto& text = measured[static_cast<size_t>(childPtr->textLayoutIndex())];
      if (text->layout && text->layout->lineCount()) {
        baseline = text->layout->lineAt(0).y() + text->layout->lineAt(0).ascent();
        const auto line = text->layout->lineAt(text->layout->lineCount() - 1);
        lastBaseline = line.y() + line.ascent();
      }
    }
    return CssMeasuredContent{
        {width < 0 ? intrinsic.maxContent : qMin(width, intrinsic.maxContent), qMax<qreal>(0, height - inset.top() - inset.bottom())},
        baseline,
        lastBaseline};
  };
  return item;
}

void HtmlLayoutEngine::layoutFormattingBox(HtmlBox& box, qreal availableWidth, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  auto& style = box.style();
  const qreal extra = horizontalBoxExtent(style);
  qreal outerWidth = style.width >= 0 ? style.width + (style.borderBox ? 0 : extra)
                                      : qMax<qreal>(0, availableWidth - style.margin.left() - style.margin.right());
  if (style.width < 0) {
    const auto kind = style.computed.layout.sizes[0];
    if (kind == CssIntrinsicSize::MinContent || kind == CssIntrinsicSize::MaxContent || kind == CssIntrinsicSize::FitContent) {
      const auto intrinsic = intrinsicMetrics(box);
      outerWidth =
          (kind == CssIntrinsicSize::MinContent   ? intrinsic.minContent
           : kind == CssIntrinsicSize::MaxContent ? intrinsic.maxContent
                                                  : qMax(intrinsic.minContent, qMin(intrinsic.maxContent, availableWidth - extra))) +
          extra;
    }
  }
  auto container = formattingStyle(box, availableWidth);
  outerWidth = cssContentConstraints(container.box, 0, availableWidth).clamp(qMax<qreal>(0, outerWidth - extra)) + extra;
  const qreal contentWidth = qMax<qreal>(0, outerWidth - extra);
  const qreal contentHeight = style.height < 0 ? -1 : qMax<qreal>(0, style.height - (style.borderBox ? verticalBoxExtent(style) : 0));
  std::vector<HtmlBox*> children;
  std::vector<CssFormattingItem> items;
  for (auto& child : box.children()) {
    if (!isRenderableChildFor(box, *child) || (child->isTextRun() && child->text().trimmed().isEmpty())) continue;
    children.push_back(child.get());
    items.push_back(formattingItem(*child, contentWidth));
  }
  const auto result = layoutFormattingItems(container, items, contentWidth, contentHeight, 1,
                                            contentGridInheritance(gridInheritance_.value(&box), style.padding + style.borderWidth));
  for (size_t i = 0; i < children.size(); ++i) {
    auto& child = *children[i];
    const auto& rect = result.items[i];
    child.style().padding = child.style().paddingLengths.used(child.style().padding, result.containingWidths[i], true);
    layoutAllocatedBox(child, rect.size(), textLayouts, result.inheritedGrids[i]);
    child.geometry() = {rect.x() + style.padding.left() + style.borderWidth.left(),
                        rect.y() + style.padding.top() + style.borderWidth.top(), rect.width(), rect.height()};
  }
  box.geometry().width = outerWidth;
  box.geometry().height = result.size.height() + verticalBoxExtent(style);
  box.firstBaseline = result.firstBaseline < 0 ? -1 : result.firstBaseline + style.padding.top() + style.borderWidth.top();
  box.lastBaseline = result.lastBaseline < 0 ? -1 : result.lastBaseline + style.padding.top() + style.borderWidth.top();
  box.formattingPaintOrder.clear();
  for (size_t i = 0; i < box.children().size(); ++i) box.formattingPaintOrder.push_back(i);
  std::stable_sort(box.formattingPaintOrder.begin(), box.formattingPaintOrder.end(), [&](size_t a, size_t b) {
    return box.children()[a]->style().computed.layout.order < box.children()[b]->style().computed.layout.order;
  });
}

void HtmlLayoutEngine::layoutTableBox(HtmlBox& table, qreal availableWidth, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  QVector<HtmlBox*> rows;
  collectTableRows(table, rows);

  // Build a grid accounting for colspan and rowspan
  struct CellEntry {
    HtmlBox* box = nullptr;
    int colSpan = 1;
    int rowSpan = 1;
  };

  int gridCols = 0;
  QVector<QVector<CellEntry>> grid;
  grid.reserve(rows.size());
  for (HtmlBox* row : rows) {
    QVector<HtmlBox*> cells;
    collectRowCells(*row, cells);
    // Determine effective column count accounting for spans
    int rowCols = 0;
    QVector<CellEntry> entries;
    entries.reserve(cells.size());
    for (HtmlBox* cell : cells) {
      CellEntry entry;
      entry.box = cell;
      entry.colSpan = qMax(1, cell->colSpan());
      entry.rowSpan = qMax(1, cell->rowSpan());
      rowCols += entry.colSpan;
      entries.push_back(entry);
    }
    gridCols = qMax(gridCols, rowCols);
    grid.push_back(std::move(entries));
  }

  const qreal availableInnerWidth = qMax<qreal>(
      1.0,
      availableWidth - table.style().margin.left() - table.style().margin.right() -
          horizontalBoxExtent(table.style()));

  // Compute per-column widths from non-spanning cells
  QVector<qreal> columnWidths(qMax(0, gridCols), 0.0);
  for (int rowIdx = 0; rowIdx < grid.size(); ++rowIdx) {
    int col = 0;
    // Skip columns occupied by previous rowspans
    // (simplified: we only account for colspan for width calculation)
    for (const auto& entry : grid[rowIdx]) {
      if (entry.colSpan == 1) {
        if (col < columnWidths.size()) {
          columnWidths[col] = qMax(columnWidths[col], intrinsicOuterWidth(*entry.box, availableInnerWidth));
        }
      }
      col += entry.colSpan;
    }
  }

  qreal naturalTableContentWidth = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0);
  qreal tableContentWidth = table.style().width >= 0 ? table.style().width : naturalTableContentWidth;
  tableContentWidth = qBound<qreal>(1.0, tableContentWidth, availableInnerWidth);

  if (gridCols > 0 && naturalTableContentWidth > 0) {
    if (!qFuzzyCompare(tableContentWidth, naturalTableContentWidth)) {
      const qreal scale = tableContentWidth / naturalTableContentWidth;
      for (qreal& width : columnWidths) {
        width = qMax<qreal>(1.0, width * scale);
      }
    }
  } else {
    tableContentWidth = 0;
  }

  const qreal actualColumnSum = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0);
  if (actualColumnSum > 0) {
    tableContentWidth = actualColumnSum;
  }

  qreal y = 0;
  if (HtmlBox* caption = findTableCaption(table)) {
    const qreal captionHeight = layoutFixedWidthBox(*caption, qMax<qreal>(1.0, tableContentWidth), textLayouts);
    caption->geometry() = HtmlLayoutGeometry{0, y, qMax<qreal>(1.0, tableContentWidth), captionHeight};
    y += captionHeight;
  }

  // Rowspan tracking: for each row, which columns are occupied by a cell from above
  // and the remaining height to extend. Maps col → {remainingRows, height}
  struct RowSpanSlot {
    int remainingRows = 0;
    qreal height = 0;
  };
  QVector<QMap<int, RowSpanSlot>> rowSpanGrid(rows.size());

  QVector<qreal> rowTops(rows.size(), 0.0);
  QVector<qreal> rowHeights(rows.size(), 0.0);
  for (int rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
    rowTops[rowIdx] = y;
    qreal rowHeight = 0;

    // First pass: compute cell heights, place cells accounting for spans
    int col = 0;
    for (auto& entry : grid[rowIdx]) {
      // Skip columns occupied by rowspans from above
      while (col < gridCols && rowSpanGrid[rowIdx].contains(col)) {
        auto& slot = rowSpanGrid[rowIdx][col];
        rowHeight = qMax(rowHeight, slot.height);
        col += 1;  // rowspan occupies 1 column-width per slot (simplified)
      }

      if (col >= gridCols) break;

      // Compute width for this cell (sum of spanned columns)
      qreal cellWidth = 0;
      for (int c = col; c < col + entry.colSpan && c < gridCols; ++c) {
        cellWidth += (c < columnWidths.size()) ? columnWidths[c] : 1.0;
      }
      cellWidth = qMax<qreal>(1.0, cellWidth);

      const qreal cellHeight = layoutFixedWidthBox(*entry.box, cellWidth, textLayouts);
      entry.box->geometry() = HtmlLayoutGeometry{
          col < columnWidths.size() ? std::accumulate(columnWidths.begin(), columnWidths.begin() + col, 0.0) : 0.0,
          0, cellWidth, cellHeight};
      rowHeight = qMax(rowHeight, cellHeight);

      // Propagate rowspan to subsequent rows
      if (entry.rowSpan > 1) {
        for (int r = 1; r < entry.rowSpan && rowIdx + r < rows.size(); ++r) {
          for (int c = col; c < col + entry.colSpan && c < gridCols; ++c) {
            rowSpanGrid[rowIdx + r][c] = {entry.rowSpan - r, cellHeight};
          }
        }
      }

      col += entry.colSpan;
    }

    // Account for any remaining rowspan slots in this row
    for (auto it = rowSpanGrid[rowIdx].constBegin(); it != rowSpanGrid[rowIdx].constEnd(); ++it) {
      rowHeight = qMax(rowHeight, it.value().height);
    }

    // Equalize heights for all cells in this row
    for (auto& entry : grid[rowIdx]) {
      entry.box->geometry().height = rowHeight;
    }

    rows[rowIdx]->geometry() = HtmlLayoutGeometry{0, y, tableContentWidth, rowHeight};
    rowHeights[rowIdx] = rowHeight;
    y += rowHeight;
  }

  // A rowspan cell spans the TOTAL height of the rows it crosses (CSS table semantics). Above,
  // its height was frozen at its starting row's height — if a later spanned row grew taller,
  // the cell's background/border ended early and left a hole under it. All row heights are
  // final now; re-derive each spanning cell's height from them.
  for (int rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
    for (auto& entry : grid[rowIdx]) {
      if (entry.rowSpan <= 1) {
        continue;
      }
      const int lastRow = qMin(rowIdx + entry.rowSpan, static_cast<int>(rows.size()));
      qreal spanHeight = 0;
      for (int r = rowIdx; r < lastRow; ++r) {
        spanHeight += rowHeights[r];
      }
      entry.box->geometry().height = spanHeight;
    }
  }

  for (auto& child : table.children()) {
    if (!child->style().visible || child->style().display == HtmlDisplay::None || child->tag() == HtmlTag::Caption) {
      continue;
    }
    if (child->style().display != HtmlDisplay::TableRowGroup) {
      continue;
    }
    qreal groupTop = 0;
    qreal groupBottom = 0;
    bool sawRow = false;
    for (auto& rowChild : child->children()) {
      if (!isTableRowBox(*rowChild)) {
        continue;
      }
      for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        if (rows[rowIndex] == rowChild.get()) {
          if (!sawRow) {
            groupTop = rowTops[rowIndex];
            groupBottom = rowTops[rowIndex] + rowHeights[rowIndex];
            sawRow = true;
          } else {
            groupTop = qMin(groupTop, rowTops[rowIndex]);
            groupBottom = qMax(groupBottom, rowTops[rowIndex] + rowHeights[rowIndex]);
          }
          rowChild->geometry().top -= groupTop;
          break;
        }
      }
    }
    if (sawRow) {
      child->geometry() = HtmlLayoutGeometry{0, groupTop, tableContentWidth, groupBottom - groupTop};
    }
  }

  table.geometry().width = qMax<qreal>(tableContentWidth, 1.0) + horizontalBoxExtent(table.style());
  table.geometry().height = y + verticalBoxExtent(table.style());
}

qreal HtmlLayoutEngine::layoutFixedWidthBox(
    HtmlBox& box,
    qreal width,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  const auto& style = box.style();
  const qreal contentWidth = qMax<qreal>(1.0, width - horizontalBoxExtent(style));

  bool hasBlockChildren = false;
  bool hasInlineContent = false;
  bool hasReplacedInlineContent = false;
  for (const auto& child : box.children()) {
    if (!isRenderableChildFor(box, *child)) {
      continue;
    }
    if (child->style().display == HtmlDisplay::Block ||
        (child->style().display == HtmlDisplay::Flex || child->style().display == HtmlDisplay::Grid) ||
        child->style().display == HtmlDisplay::Table ||
        child->style().display == HtmlDisplay::ListItem) {
      hasBlockChildren = true;
    }
    if (child->isInlineLevel() || child->isTextRun()) {
      hasInlineContent = true;
    }
    if (containsReplacedInlineContent(*child)) {
      hasReplacedInlineContent = true;
    }
  }

  qreal contentHeight = 0;
  if (box.tag() == HtmlTag::Pre && hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildPreLayout(box, style.fontSize, contentWidth);
    contentHeight = textLayout->height;
    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
  } else if (hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildInlineLayout(box, style.fontSize, contentWidth, style.textAlign);
    contentHeight = textLayout->height;
    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
  } else {
    qreal y = 0;
    for (auto& child : box.children()) {
      if (!child->style().visible || child->style().display == HtmlDisplay::None) {
        continue;
      }
      if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
        continue;
      }
      if (isTableInternalBox(*child)) {
        continue;
      }
      const qreal childHeight = layoutFixedWidthBox(*child, contentWidth, textLayouts);
      child->geometry() = HtmlLayoutGeometry{0, y, contentWidth, childHeight};
      y += childHeight;
    }
    contentHeight = y;
  }

  const qreal totalHeight = qMax<qreal>(contentHeight + verticalBoxExtent(style), TextFontMetrics(style.font).height());
  box.geometry().width = width;
  box.geometry().height = totalHeight;
  return totalHeight;
}

qreal HtmlLayoutEngine::intrinsicOuterWidth(const HtmlBox& box, qreal availableWidth) const {
  const auto& style = box.style();
  if (box.tag() == HtmlTag::Pre) {
    auto textLayout = measurer_.buildPreLayout(box, style.fontSize, qMax<qreal>(1.0, availableWidth));
    return qMax<qreal>(1.0, textLayout->width + horizontalBoxExtent(style));
  }
  const qreal measuredWidth = measurer_.measureInlineContext(box, style.fontSize, qMax<qreal>(1.0, availableWidth)).width();
  return qMax<qreal>(1.0, measuredWidth + horizontalBoxExtent(style));
}

void HtmlLayoutEngine::readLayoutBack(HtmlBox& box, YGNode* node) {
  if (!box.style().visible) {
    return;
  }

  auto& geo = box.geometry();
  geo.left = YGNodeLayoutGetLeft(node);
  geo.top = YGNodeLayoutGetTop(node);
  geo.width = YGNodeLayoutGetWidth(node);
  geo.height = YGNodeLayoutGetHeight(node);
  if (auto* context = static_cast<YogaContext*>(YGNodeGetContext(node)); context && context->measurer && context->textLayouts) {
    const auto contentWidth = qMax<qreal>(0, geo.width - horizontalBoxExtent(box.style()));
    auto text = context->pre ? measurer_.buildPreLayout(box, context->fontSize, contentWidth)
                             : measurer_.buildInlineLayout(box, context->fontSize, contentWidth, box.style().textAlign);
    context->textLayouts->at(static_cast<size_t>(box.textLayoutIndex())) = std::move(text);
  }

  // Read children layouts. Walk the SAME child filter createYogaNode used when building the
  // Yoga tree (isRenderableChildFor) — a child skipped there has no Yoga node, and counting
  // it here desynchronizes yogaIndex: the next sibling's geometry would land in the skipped
  // box (e.g. a collapsed <details>' hidden child borrowing the summary's rect) while the
  // real owner keeps a stale/zero rect.
  uint32_t childCount = YGNodeGetChildCount(node);
  uint32_t yogaIndex = 0;
  for (auto& child : box.children()) {
    if (!isRenderableChildFor(box, *child)) {
      continue;
    }
    if (yogaIndex < childCount) {
      YGNode* childNode = YGNodeGetChild(node, yogaIndex);
      readLayoutBack(*child, childNode);
      ++yogaIndex;
    }
  }

  if (box.ownsTextLayout() && activeTextLayouts_ && box.textLayoutIndex() >= 0 && box.textLayoutIndex() < int(activeTextLayouts_->size())) {
    const auto& text = (*activeTextLayouts_)[box.textLayoutIndex()];
    if (text->layout && text->layout->lineCount()) {
      const auto first = text->layout->lineAt(0), last = text->layout->lineAt(text->layout->lineCount() - 1);
      const qreal inset = box.style().padding.top() + box.style().borderWidth.top();
      box.firstBaseline = inset + first.y() + first.ascent();
      box.lastBaseline = inset + last.y() + last.ascent();
    }
  }
  // For inline context boxes, the text layout already has the correct height.
  // Override Yoga's height if text measurement was done.
  // (The measurement callback should have returned the correct size already.)
}

}  // namespace muffin::html
