#include "theme/CssLayoutStyle.h"
#include "theme/CssComputedStyleEngine.h"

namespace muffin {
CssLayoutStyle CssLayoutStyle::fromComputed(const CssComputedStyle& style) {
  CssLayoutStyle result;
  const auto keyword = [&](const char* property, QString& target) {
    const auto value = style.resolvedValue(QString::fromLatin1(property)).trimmed().toLower();
    if (!value.isEmpty()) target = value;
  };
  keyword("display", result.display);
  keyword("flex-direction", result.direction);
  keyword("flex-wrap", result.wrap);
  keyword("justify-content", result.justifyContent);
  keyword("align-items", result.alignItems);
  keyword("align-self", result.alignSelf);
  keyword("align-content", result.alignContent);
  keyword("justify-items", result.justifyItems);
  keyword("justify-self", result.justifySelf);
  for (const auto& [property, target] :
       {std::pair{"grid-template-columns", &result.gridColumns}, std::pair{"grid-template-rows", &result.gridRows},
        std::pair{"grid-auto-columns", &result.gridAutoColumns}, std::pair{"grid-auto-rows", &result.gridAutoRows}}) {
    *target = style.computedGridTracks_.value(property);
  }
  const char* lines[] = {"grid-column-start", "grid-column-end", "grid-row-start", "grid-row-end"};
  if (const auto areas = parseCssGridAreas(style.resolvedValue("grid-template-areas"))) result.gridAreas = *areas;
  for (int i = 0; i < 4; ++i)
    if (auto line = parseCssGridLine(style.resolvedValue(lines[i]))) result.gridLines[i] = *line;
  const auto flow = style.resolvedValue("grid-auto-flow").toLower();
  result.gridFlowColumn = flow.contains("column");
  result.gridDense = flow.contains("dense");
  keyword("overflow-x", result.overflowX);
  keyword("overflow-y", result.overflowY);
  keyword("overflow-wrap", result.overflowWrap);
  keyword("word-break", result.wordBreak);
  // CSS overflow visible/clip computes to auto/hidden alongside a scrollable axis.
  const auto scrollable = [](const QString& value) { return value != "visible" && value != "clip"; };
  const bool xScroll = scrollable(result.overflowX), yScroll = scrollable(result.overflowY);
  if (yScroll && result.overflowX == "visible") result.overflowX = "auto";
  if (yScroll && result.overflowX == "clip") result.overflowX = "hidden";
  if (xScroll && result.overflowY == "visible") result.overflowY = "auto";
  if (xScroll && result.overflowY == "clip") result.overflowY = "hidden";
  if (style.hasProperty("flex-grow")) result.grow = style.resolvedValue("flex-grow").toDouble();
  if (style.hasProperty("flex-shrink")) result.shrink = style.resolvedValue("flex-shrink").toDouble();
  result.order = style.resolvedValue("order").toInt();
  result.basis = style.length("flex-basis");
  keyword("flex-basis", result.basisKeyword);
  result.rowGap = style.length("row-gap");
  result.columnGap = style.length("column-gap");
  const char* sizes[] = {"width", "height", "min-width", "max-width", "min-height", "max-height"};
  for (int i = 0; i < 6; ++i) {
    const auto value = style.resolvedValue(QString::fromLatin1(sizes[i])).toLower();
    result.sizes[i] = value == "min-content"                                                         ? CssIntrinsicSize::MinContent
                      : value == "max-content"                                                       ? CssIntrinsicSize::MaxContent
                      : value == "fit-content"                                                       ? CssIntrinsicSize::FitContent
                      : value == "none"                                                              ? CssIntrinsicSize::None
                      : style.length(QString::fromLatin1(sizes[i])).status == CssLengthStatus::Valid ? CssIntrinsicSize::Length
                                                                                                     : CssIntrinsicSize::Auto;
  }
  const char* margins[] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
  for (int i = 0; i < 4; ++i) result.autoMargins[i] = style.resolvedValue(QString::fromLatin1(margins[i])) == "auto";
  return result;
}
void CssLayoutStyle::scaleLengths(qreal scale) {
  for (auto* value : {&basis, &rowGap, &columnGap}) value->px *= scale;
  for (auto* list : {&gridColumns, &gridRows, &gridAutoColumns, &gridAutoRows}) {
    for (auto& track : *list) {
      track.minimum.length.px *= scale;
      track.maximum.length.px *= scale;
    }
    if (list->automatic)
      for (auto& track : list->automatic->tracks) {
        track.minimum.length.px *= scale;
        track.maximum.length.px *= scale;
      }
  }
}
}  // namespace muffin
