#pragma once

#include "html/HtmlBox.h"
#include "html/HtmlTextMeasurer.h"
#include "render/CssFormattingContext.h"

#include <memory>

struct YGNode;

namespace muffin::html {

struct YogaContext {
  HtmlBox* box;
  qreal fontSize;
  HtmlTextMeasurer* measurer = nullptr;
  std::vector<std::unique_ptr<HtmlTextLayout>>* textLayouts = nullptr;
  bool pre = false;
};

class HtmlLayoutEngine {
public:
  HtmlLayoutEngine();
  ~HtmlLayoutEngine();

  // Run Yoga layout on the box tree.
  // After this call, each HtmlBox has its geometry field populated.
  // textLayouts is filled with pre-built text layouts for painting.
  void layout(
      HtmlBox& root,
      qreal availableWidth,
      qreal baseFontSize,
      std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts,
      const HtmlColorPalette& palette = HtmlColorPalette::defaultLight());

private:
 std::vector<std::unique_ptr<HtmlTextLayout>>* activeTextLayouts_ = nullptr;
 HtmlTextLayout::AtomicInline layoutAtomicInline(HtmlBox& box, qreal width);
 void layoutFormattingBox(HtmlBox& box, qreal availableWidth, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);
 qreal layoutAllocatedBox(HtmlBox& box, QSizeF size, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts, const CssGridInheritance& inherited = {});
 CssIntrinsicMetrics intrinsicMetrics(HtmlBox& box);
 CssFormattingItem formattingItem(HtmlBox& box, qreal containingWidth);
 QHash<HtmlBox*, CssGridInheritance> gridInheritance_;
 YGNode* createYogaNode(HtmlBox& box, qreal fontSize, qreal availableWidth, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);

 void layoutTableBox(HtmlBox& box, qreal availableWidth, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);
 qreal layoutFixedWidthBox(HtmlBox& box, qreal width, std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);
 qreal intrinsicOuterWidth(const HtmlBox& box, qreal availableWidth) const;

 void applyBoxStyle(YGNode* node, const HtmlComputedStyle& style);
 void readLayoutBack(HtmlBox& box, YGNode* node);

 HtmlTextMeasurer measurer_;
};

}  // namespace muffin::html
