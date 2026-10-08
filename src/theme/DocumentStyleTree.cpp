#include "theme/DocumentStyleTree.h"
#include "DocumentBaseStyle.h"

namespace muffin {
CssThemeSheet documentStyleSheet(const CssThemeSheet& author) {
  static const auto base = CssThemeParser::parse(QString::fromUtf8(documentBaseCss), {});
  auto sheet = base;
  sheet.mergeIn(author);
  return sheet;
}
}  // namespace muffin
