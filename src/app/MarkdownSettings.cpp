#include "app/MarkdownSettings.h"

#include <QSettings>

namespace muffin {

ParseOptions markdownParseOptions() {
  ParseOptions options;  // defaults match the parser: all GFM extensions + front matter on
  options.enableAutolink = QSettings().value(QStringLiteral("markdown/autoLink"), true).toBool();
  options.enableMath = QSettings().value(QStringLiteral("markdown/inlineMath"), true).toBool();
  options.enableAlertBox = QSettings().value(QStringLiteral("markdown/alertBox"), true).toBool();
  options.enableHighlight = QSettings().value(QStringLiteral("markdown/highlight"), false).toBool();
  options.enableSubscript = QSettings().value(QStringLiteral("markdown/subscript"), false).toBool();
  options.enableSuperscript = QSettings().value(QStringLiteral("markdown/superscript"), false).toBool();
  // Strict mode = vanilla CommonMark: disable every GFM/non-standard extension (front matter stays
  // on, since turning it off can corrupt documents that rely on a leading metadata block).
  if (QSettings().value(QStringLiteral("markdown/strictMode"), false).toBool()) {
    options.enableTable = false;
    options.enableStrikethrough = false;
    options.enableTaskList = false;
    options.enableAutolink = false;
    options.enableMath = false;
    options.enableAlertBox = false;
    options.enableHighlight = false;
    options.enableSubscript = false;
    options.enableSuperscript = false;
  }
  return options;
}

}  // namespace muffin
