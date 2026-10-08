#pragma once

#include "theme/ThemeDefinition.h"
#include <QSizeF>
#include <QRectF>
#include <functional>
#include <memory>
#include <QByteArray>
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
  qreal lastBaseline = -1;
  bool operator==(const CssMeasuredContent&) const = default;
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
enum class CssLayoutPhase { Intrinsic, InlineAllocation, BlockAllocation, DependentAllocation, Final };
struct CssMeasureRequest {
  qreal width = -1;
  qreal containingWidth = -1;
  qreal containingHeight = -1;  // Negative means indefinite, never a frozen cyclic size.
  qreal allocatedHeight = -1;
  CssGridInheritance inherited;
  CssLayoutPhase phase = CssLayoutPhase::BlockAllocation;
};
struct CssMeasurementCache {
  struct Entry {
    QByteArray key;
    CssMeasuredContent value;
  };
  std::vector<Entry> entries;
  quint64 hits = 0, misses = 0;
};
struct CssFormattingItem {
  QString measurementIdentity;
  ThemeElementStyle style;
  CssIntrinsicMetrics intrinsic;
  // Measures content at the width allocated by the formatting algorithm.
  // A negative width requests the unconstrained preferred size.
  // containingWidth is the item's actual CSS containing block (its Grid area
  // or Flex content box), also used to resolve percentage padding.
  std::function<CssMeasuredContent(const CssMeasureRequest&)> measure;
  std::shared_ptr<CssMeasurementCache> measurements = std::make_shared<CssMeasurementCache>();
  // Present for a grid whose adapter exposes its formatting children. A leaf
  // still uses measure(), even when its CSS display happens to be grid.
  std::optional<std::vector<CssFormattingItem>> children;
  std::optional<QSizeF> naturalSize;  // replaced content, in layout pixels
};
struct CssMeasurementObservation {
  QString identity;
  CssMeasureRequest request;
  CssMeasuredContent value;
};
class CssMeasurementTrace {
 public:
  CssMeasurementTrace();
  ~CssMeasurementTrace();
  std::vector<CssMeasurementObservation> observations;
};
CssMeasuredContent measureCssItem(const CssFormattingItem& item, const CssMeasureRequest& request);
QByteArray cssMeasureKey(const CssMeasureRequest& request);
struct CssGridContribution {
  int start = 0, span = 1;
  qreal minimum = 0, maximum = 0, automaticMinimum = 0;
};
struct CssFormattingResult {
  QSizeF size;
  qreal firstBaseline = -1, lastBaseline = -1;
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
CssIntrinsicMetrics intrinsicFlexWidths(const ThemeElementStyle& style, const std::vector<CssFormattingItem>& items);
}  // namespace muffin
