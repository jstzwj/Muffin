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
  void resolve(HtmlBox& root, qreal baseFontSize);

private:
  void resolveBox(HtmlBox& box, qreal fontSize, bool inheritColor, QColor parentColor, const QString& parentFontFamily);
  void applyTagDefaults(HtmlBox& box, qreal fontSize);
  qreal resolveFontSize(const HtmlComputedStyle& style, qreal parentFontSize) const;
};

}  // namespace muffin::html
