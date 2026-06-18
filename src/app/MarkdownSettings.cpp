#include "app/MarkdownSettings.h"

#include <QSettings>

namespace muffin {

ParseOptions markdownParseOptions() {
  ParseOptions options;  // defaults match the parser: all GFM extensions + front matter on
  options.enableAutolink = QSettings().value(QStringLiteral("markdown/autoLink"), true).toBool();
  options.enableMath = QSettings().value(QStringLiteral("markdown/inlineMath"), true).toBool();
  return options;
}

}  // namespace muffin
