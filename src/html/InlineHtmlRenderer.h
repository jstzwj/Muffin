#pragma once

#include "html/HtmlTextMeasurer.h"

#include <QString>
#include <QStringView>
#include <vector>

namespace muffin::html {

struct InlineHtmlFormatResult {
  QString text;
  std::vector<TextFormatSpan> formatSpans;
  std::vector<HtmlTextLayout::LinkSpan> links;
};

class InlineHtmlRenderer {
public:
  InlineHtmlFormatResult render(const QString& htmlFragment, qreal baseFontSize) const;

  static bool isRenderableTag(QStringView tagName);
};

}  // namespace muffin::html
