#pragma once

#include "html/HtmlBox.h"

#include <memory>

namespace muffin::html {

class HtmlStyleResolver {
public:
  HtmlStyleResolver();
  ~HtmlStyleResolver();

  // Apply default styles and cascade inline styles to all boxes in the tree.
  // baseFontSize: the font size inherited from the theme (typically 16px).
  // palette: theme colours for tag defaults (<a>, <pre>, borders, the <body>
  //          canvas, ...). Defaults to the historical hardcoded light values.
  void resolve(HtmlBox& root, qreal baseFontSize, const HtmlColorPalette& palette = HtmlColorPalette::defaultLight());

private:
  void resolveBox(HtmlBox& box, qreal fontSize, bool inheritColor, QColor parentColor, const QString& parentFontFamily, const HtmlColorPalette& palette);
  void applyTagDefaults(HtmlBox& box, qreal fontSize, const HtmlColorPalette& palette);
  qreal resolveFontSize(const HtmlComputedStyle& style, qreal parentFontSize) const;
};

}  // namespace muffin::html
