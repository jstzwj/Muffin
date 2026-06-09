#pragma once

#include "html/HtmlBox.h"
#include "html/HtmlTextMeasurer.h"

#include <memory>

struct YGNode;

namespace muffin::html {

struct YogaContext {
  HtmlBox* box;
  qreal fontSize;
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
      std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);

private:
  YGNode* createYogaNode(
      HtmlBox& box,
      qreal fontSize,
      qreal availableWidth,
      std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);

  void layoutTableBox(
      HtmlBox& box,
      qreal availableWidth,
      std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);
  qreal layoutFixedWidthBox(
      HtmlBox& box,
      qreal width,
      std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts);
  qreal intrinsicOuterWidth(const HtmlBox& box, qreal availableWidth) const;

  void applyBoxStyle(YGNode* node, const HtmlComputedStyle& style);
  void readLayoutBack(HtmlBox& box, YGNode* node);

  HtmlTextMeasurer measurer_;
};

}  // namespace muffin::html
