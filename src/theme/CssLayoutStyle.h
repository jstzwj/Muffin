#pragma once

#include "theme/CssCalc.h"
#include "theme/CssGridStyle.h"
#include <QString>
#include <array>

namespace muffin {
class CssComputedStyle;

enum class CssIntrinsicSize { Auto, Length, MinContent, MaxContent, FitContent, None };

// Formatting properties are computed once, independently of the source tree.
// Both native Markdown and HTML adapters consume this same layout input.
struct CssLayoutStyle {
  QString display = QStringLiteral("inline");
  QString direction = QStringLiteral("row");
  QString wrap = QStringLiteral("nowrap");
  QString justifyContent = QStringLiteral("normal");
  QString alignItems = QStringLiteral("normal");
  QString alignSelf = QStringLiteral("auto");
  QString alignContent = QStringLiteral("normal");
  QString justifyItems = QStringLiteral("normal");
  QString justifySelf = QStringLiteral("auto");
  CssGridTrackList gridColumns, gridRows, gridAutoColumns, gridAutoRows;
  CssGridAreas gridAreas;
  std::array<CssGridLine, 4> gridLines{};  // column start/end, row start/end
  bool gridFlowColumn = false;
  bool gridDense = false;
  QString overflowX = QStringLiteral("visible");
  QString overflowY = QStringLiteral("visible");
  QString overflowWrap = QStringLiteral("normal");
  QString wordBreak = QStringLiteral("normal");
  qreal grow = 0;
  qreal shrink = 1;
  int order = 0;
  CssLengthPercentage basis, rowGap, columnGap;
  QString basisKeyword = QStringLiteral("auto");
  std::array<CssIntrinsicSize, 6> sizes{};  // width, height, min-width, max-width, min-height, max-height
  std::array<bool, 4> autoMargins{};        // top, right, bottom, left

  bool isFlex() const { return display == QLatin1String("flex") || display == QLatin1String("inline-flex"); }
  bool isGrid() const { return display == QLatin1String("grid") || display == QLatin1String("inline-grid"); }
  bool establishesFormattingContext() const { return isFlex() || isGrid(); }
  void scaleLengths(qreal scale);
  bool clipsX() const { return overflowX != QLatin1String("visible"); }
  bool clipsY() const { return overflowY != QLatin1String("visible"); }
  static CssLayoutStyle fromComputed(const CssComputedStyle& style);
};
}  // namespace muffin
