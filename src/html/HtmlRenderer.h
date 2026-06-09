#pragma once

#include "html/HtmlLayoutResult.h"

#include <QString>

namespace muffin::html {

class HtmlRenderer {
public:
  HtmlRenderer();
  ~HtmlRenderer();

  // Render HTML text to a layout result.
  // baseFontSize: font size from the theme (typically 16px).
  // availableWidth: the content width for layout.
  HtmlLayoutResult render(const QString& html, qreal baseFontSize, qreal availableWidth, QString baseDirectory = {});
};

}  // namespace muffin::html
