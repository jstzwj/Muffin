#include "app/MarkdownSettings.h"

#include <QSettings>

namespace muffin {

ParseOptions markdownParseOptions() {
  ParseOptions options;  // defaults match the parser: all GFM extensions + front matter on
  options.enableAutolink = QSettings().value(QStringLiteral("markdown/autoLink"), true).toBool();
  options.enableMath = QSettings().value(QStringLiteral("markdown/inlineMath"), true).toBool();
  options.relaxedInlineMath = QSettings().value(QStringLiteral("markdown/relaxedInlineMath"), true).toBool();
  options.enableAlertBox = QSettings().value(QStringLiteral("markdown/alertBox"), true).toBool();
  options.enableHighlight = QSettings().value(QStringLiteral("markdown/highlight"), false).toBool();
  options.enableSubscript = QSettings().value(QStringLiteral("markdown/subscript"), false).toBool();
  options.enableSuperscript = QSettings().value(QStringLiteral("markdown/superscript"), false).toBool();
  options.enableUnicodeRemap = QSettings().value(QStringLiteral("markdown/remapUnicode"), false).toBool();
  // Strict Mode = plain CommonMark structure. It disables only the always-on GFM extensions that
  // have NO individual switch in Preferences (tables, strikethrough, task lists). Every extension
  // the user can toggle (formulas, auto links, highlight, alert boxes, sub/superscript, unicode
  // remap) always follows its own switch — Strict Mode must never silently override an explicit
  // choice, otherwise "I enabled Inline Formula" appears to do nothing.
  if (QSettings().value(QStringLiteral("markdown/strictMode"), false).toBool()) {
    options.enableTable = false;
    options.enableStrikethrough = false;
    options.enableTaskList = false;
  }
  return options;
}

}  // namespace muffin
