#pragma once

#include "theme/ThemeDefinition.h"
#include <QSizeF>
#include <QRectF>
#include <functional>
#include <vector>

struct YGNode;
class QTextLayout;
namespace muffin {
struct CssIntrinsicMetrics {
  qreal minContent = 0;
  qreal maxContent = 0;
};
struct CssMeasuredContent {
  QSizeF size;
  qreal baseline = 0;
};
struct CssGridAxisGeometry {
  std::vector<qreal> starts, sizes;
  std::vector<QStringList> lineNames;
  std::vector<CssGridTrack> definitions;
  qreal gap = 0;
};
struct CssGridInheritance {
  std::optional<CssGridAxisGeometry> columns, rows;
};
struct CssFormattingItem {
  ThemeElementStyle style;
  CssIntrinsicMetrics intrinsic;
  // Measures content at the width allocated by the formatting algorithm.
  // A negative width requests the unconstrained preferred size.
  // containingWidth is the item's actual CSS containing block (its Grid area
  // or Flex content box), also used to resolve percentage padding.
  std::function<CssMeasuredContent(qreal width, qreal containingWidth, const CssGridInheritance&)> measure;
  // Present for a grid whose adapter exposes its formatting children. A leaf
  // still uses measure(), even when its CSS display happens to be grid.
  std::optional<std::vector<CssFormattingItem>> children;
  std::optional<QSizeF> naturalSize;  // replaced content, in layout pixels
};
struct CssGridContribution {
  int start = 0, span = 1;
  qreal minimum = 0, maximum = 0, automaticMinimum = 0;
};
struct CssFormattingResult {
  QSizeF size;
  std::vector<QRectF> items;            // source order, regardless of visual order
  std::vector<qreal> containingWidths;  // Flex content width or Grid area width
  std::vector<CssGridInheritance> inheritedGrids;
  // Intrinsic contributions remain track-relative so a parent can size shared
  // tracks from subgrid descendants, including descendants spanning tracks.
  std::vector<CssGridContribution> columnContributions, rowContributions;
  CssIntrinsicMetrics intrinsic;
};

// Shared Yoga configuration and CSS mapping. No Markdown/HTML defaults here.
YGNode* createCssLayoutNode();
CssIntrinsicMetrics intrinsicTextWidths(const QTextLayout& text, bool noWrap = false, bool anywhereMinimum = false);
void applyCssFormattingStyle(YGNode* node, const CssLayoutStyle& style, qreal scale = 1);
CssFormattingResult layoutFlexItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                                    qreal contentHeight = -1, qreal scale = 1);
CssFormattingResult layoutGridItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                                    qreal contentHeight = -1, qreal scale = 1, const CssGridInheritance& inherited = {});
CssFormattingResult layoutFormattingItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items,
                                          qreal contentWidth, qreal contentHeight = -1, qreal scale = 1,
                                          const CssGridInheritance& inherited = {});
CssGridInheritance contentGridInheritance(CssGridInheritance inherited, QMarginsF insets);
CssIntrinsicMetrics intrinsicGridWidths(const ThemeElementStyle& style, const std::vector<CssFormattingItem>& items);
}  // namespace muffin
