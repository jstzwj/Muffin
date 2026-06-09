#include "html/HtmlLayoutEngine.h"
#include "html/HtmlTextMeasurer.h"

#include <yoga/Yoga.h>

#include <QFontMetricsF>
#include <QImageReader>
#include <QVector>

#include <cmath>

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
  const qreal textWidth = box->geometry().width;
  const qreal textHeight = box->geometry().height;
  const qreal measuredWidth =
      widthMode == YGMeasureModeExactly || widthMode == YGMeasureModeAtMost ? qMin<qreal>(textWidth, width) : textWidth;

  QFontMetricsF fm(box->style().font);
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

}  // namespace

HtmlLayoutEngine::HtmlLayoutEngine() = default;
HtmlLayoutEngine::~HtmlLayoutEngine() = default;

void HtmlLayoutEngine::layout(
    HtmlBox& root,
    qreal availableWidth,
    qreal baseFontSize,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  textLayouts.clear();

  YGNode* rootNode = createYogaNode(root, baseFontSize, availableWidth, textLayouts);

  YGNodeCalculateLayout(rootNode, static_cast<float>(availableWidth), YGUndefined, YGDirectionLTR);

  readLayoutBack(root, rootNode);

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
  YGNode* node = YGNodeNew();

  const auto& style = box.style();

  // Skip invisible boxes
  if (!style.visible || style.display == HtmlDisplay::None) {
    YGNodeStyleSetDisplay(node, YGDisplayNone);
    box.style().visible = false;
    return node;
  }

  // Apply box model styles
  applyBoxStyle(node, style);
  if ((style.display == HtmlDisplay::Table ||
       style.display == HtmlDisplay::TableRowGroup ||
       style.display == HtmlDisplay::TableRow) &&
      style.width < 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(qMax<qreal>(1.0, availableWidth)));
  }

  // Determine if this box contains only inline children (text content)
  bool hasBlockChildren = false;
  bool hasInlineContent = false;
  bool hasReplacedInlineContent = false;
  for (const auto& child : box.children()) {
    if (child->style().display == HtmlDisplay::None || !child->style().visible) {
      continue;
    }
    if (child->style().display == HtmlDisplay::Block ||
        child->style().display == HtmlDisplay::Flex ||
        child->style().display == HtmlDisplay::Table ||
        child->style().display == HtmlDisplay::ListItem) {
      hasBlockChildren = true;
    }
    if (child->isInlineLevel() || child->isTextRun()) {
      hasInlineContent = true;
    }
    if (child->tag() == HtmlTag::Image) {
      hasReplacedInlineContent = true;
    }
  }

  if (box.tag() == HtmlTag::Image) {
    QSize naturalSize;
    if (!box.src().isEmpty()) {
      QImageReader reader(box.src());
      naturalSize = reader.size();
    }
    qreal imageWidth = style.width >= 0 ? style.width : (naturalSize.width() > 0 ? naturalSize.width() : kDefaultImageWidth);
    qreal imageHeight = style.height >= 0 ? style.height : (naturalSize.height() > 0 ? naturalSize.height() : kDefaultImageHeight);
    if (style.width >= 0 && style.height < 0 && naturalSize.width() > 0 && naturalSize.height() > 0) {
      imageHeight = imageWidth * naturalSize.height() / naturalSize.width();
    } else if (style.height >= 0 && style.width < 0 && naturalSize.width() > 0 && naturalSize.height() > 0) {
      imageWidth = imageHeight * naturalSize.width() / naturalSize.height();
    }
    const qreal maxWidth = qMax<qreal>(1.0, availableWidth - style.margin.left() - style.margin.right());
    if (imageWidth > maxWidth) {
      const qreal scale = maxWidth / imageWidth;
      imageWidth *= scale;
      imageHeight *= scale;
    }
    YGNodeStyleSetWidth(node, static_cast<float>(imageWidth));
    YGNodeStyleSetHeight(node, static_cast<float>(imageHeight));
    box.geometry().width = imageWidth;
    box.geometry().height = imageHeight;
  } else if (hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    // This is an inline formatting context — measure text as one unit
    auto textLayout = measurer_.buildInlineLayout(
        box, style.fontSize, qMax<qreal>(1.0, availableWidth - style.padding.left() - style.padding.right()),
        style.textAlign);
    qreal textHeight = textLayout->height;
    qreal textWidth = textLayout->width;

    // Set measurement callback data
    auto* ctx = new YogaContext{&box, style.fontSize};
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
    if (style.display != HtmlDisplay::Flex && style.display != HtmlDisplay::TableRow) {
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
      qreal childAvailableWidth = availableWidth;
      if (style.display == HtmlDisplay::TableRow && child->style().display == HtmlDisplay::TableCell && tableCellCount > 0) {
        childAvailableWidth = qMax<qreal>(1.0, availableWidth / tableCellCount);
      }
      YGNode* childNode = createYogaNode(*child, style.fontSize, childAvailableWidth, textLayouts);
      YGNodeInsertChild(node, childNode, childIndex);
      ++childIndex;
    }
  }

  return node;
}

void HtmlLayoutEngine::applyBoxStyle(YGNode* node, const HtmlComputedStyle& style) {
  // Margin
  YGNodeStyleSetMargin(node, YGEdgeTop, static_cast<float>(style.margin.top()));
  YGNodeStyleSetMargin(node, YGEdgeBottom, static_cast<float>(style.margin.bottom()));
  YGNodeStyleSetMargin(node, YGEdgeLeft, static_cast<float>(style.margin.left()));
  YGNodeStyleSetMargin(node, YGEdgeRight, static_cast<float>(style.margin.right()));

  // Padding
  YGNodeStyleSetPadding(node, YGEdgeTop, static_cast<float>(style.padding.top()));
  YGNodeStyleSetPadding(node, YGEdgeBottom, static_cast<float>(style.padding.bottom()));
  YGNodeStyleSetPadding(node, YGEdgeLeft, static_cast<float>(style.padding.left()));
  YGNodeStyleSetPadding(node, YGEdgeRight, static_cast<float>(style.padding.right()));

  // Width/Height
  if (style.width >= 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(style.width));
  }
  if (style.height >= 0) {
    YGNodeStyleSetHeight(node, static_cast<float>(style.height));
  }

  // Flex direction for flex containers
  if (style.display == HtmlDisplay::Flex) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
    YGNodeStyleSetFlexWrap(node, YGWrapWrap);
  } else if (style.display == HtmlDisplay::Table ||
             style.display == HtmlDisplay::TableRowGroup) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
  } else if (style.display == HtmlDisplay::TableRow) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
  } else if (style.display == HtmlDisplay::TableCell) {
    YGNodeStyleSetFlexGrow(node, 1);
    YGNodeStyleSetFlexShrink(node, 1);
  }

  // Default flex behavior: grow to fill available space in column layout
  if (style.width < 0) {
    YGNodeStyleSetFlexGrow(node, 0);
    YGNodeStyleSetFlexShrink(node, 0);
  }
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

  // Read children layouts
  uint32_t childCount = YGNodeGetChildCount(node);
  uint32_t yogaIndex = 0;
  for (auto& child : box.children()) {
    if (!child->style().visible || child->style().display == HtmlDisplay::None) {
      continue;
    }
    if (yogaIndex < childCount) {
      YGNode* childNode = YGNodeGetChild(node, yogaIndex);
      readLayoutBack(*child, childNode);
      ++yogaIndex;
    }
  }

  // For inline context boxes, the text layout already has the correct height.
  // Override Yoga's height if text measurement was done.
  // (The measurement callback should have returned the correct size already.)
}

}  // namespace muffin::html
