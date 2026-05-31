#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>

namespace muffin {

QString HtmlSanitizer::sanitizedPreview(QString html) const {
  html.remove(QRegularExpression(
      QStringLiteral("<\\s*script\\b[^>]*>.*?<\\s*/\\s*script\\s*>"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
  html.remove(QRegularExpression(
      QStringLiteral("\\s+on[a-zA-Z0-9_-]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"),
      QRegularExpression::CaseInsensitiveOption));
  html.replace(QRegularExpression(
                   QStringLiteral("(href|src)\\s*=\\s*([\"'])\\s*javascript:[^\"']*\\2"),
                   QRegularExpression::CaseInsensitiveOption),
               QStringLiteral("\\1=\"#\""));
  html.replace(QRegularExpression(
                   QStringLiteral("(href|src)\\s*=\\s*javascript:[^\\s>]+"),
                   QRegularExpression::CaseInsensitiveOption),
               QStringLiteral("\\1=\"#\""));
  return html;
}

}  // namespace muffin
