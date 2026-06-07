#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>

namespace muffin {

QString HtmlSanitizer::sanitizedPreview(QString html) const {
  static const QRegularExpression scriptRe(
      QStringLiteral("<\\s*script\\b[^>]*>.*?<\\s*/\\s*script\\s*>"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression onAttrRe(
      QStringLiteral("\\s+on[a-zA-Z0-9_-]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression jsHrefQuotedRe(
      QStringLiteral("(href|src)\\s*=\\s*([\"'])\\s*javascript:[^\"']*\\2"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression jsHrefUnquotedRe(
      QStringLiteral("(href|src)\\s*=\\s*javascript:[^\\s>]+"),
      QRegularExpression::CaseInsensitiveOption);
  html.remove(scriptRe);
  html.remove(onAttrRe);
  html.replace(jsHrefQuotedRe, QStringLiteral("\\1=\"#\""));
  html.replace(jsHrefUnquotedRe, QStringLiteral("\\1=\"#\""));
  return html;
}

}  // namespace muffin
