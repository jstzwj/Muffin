#include "editor/DocumentSearch.h"

#include <QRegularExpression>

#include <utility>

namespace muffin {

SearchResults DocumentSearch::findAll(const QString& text, const QString& pattern,
                                      SearchOptions options) {
  SearchResults result;
  if (pattern.isEmpty()) { return result; }
  const QString expression = options.regularExpression
      ? pattern : QRegularExpression::escape(pattern);
  QRegularExpression::PatternOptions patternOptions;
  if (!options.caseSensitive) {
    patternOptions |= QRegularExpression::CaseInsensitiveOption;
  }
  const QRegularExpression regex(expression, patternOptions);
  if (!regex.isValid()) {
    result.valid = false;
    result.error = regex.errorString();
    return result;
  }
  auto iterator = regex.globalMatch(text);
  while (iterator.hasNext()) {
    const QRegularExpressionMatch match = iterator.next();
    SearchMatch item;
    item.start = match.capturedStart();
    item.length = match.capturedLength();
    item.captures.reserve(match.lastCapturedIndex() + 1);
    for (int i = 0; i <= match.lastCapturedIndex(); ++i) {
      item.captures.append(match.captured(i));
    }
    result.matches.append(std::move(item));
  }
  return result;
}

QString DocumentSearch::expandReplacement(const QString& replacement,
                                          const SearchMatch& match,
                                          bool regularExpression) {
  if (!regularExpression) { return replacement; }
  QString expanded;
  expanded.reserve(replacement.size());
  for (qsizetype i = 0; i < replacement.size(); ++i) {
    const QChar ch = replacement.at(i);
    if ((ch == QLatin1Char('$') || ch == QLatin1Char('\\'))
        && i + 1 < replacement.size() && replacement.at(i + 1).isDigit()) {
      int capture = 0;
      qsizetype j = i + 1;
      while (j < replacement.size() && replacement.at(j).isDigit()) {
        capture = capture * 10 + replacement.at(j).digitValue();
        ++j;
      }
      if (capture >= 0 && capture < match.captures.size()) {
        expanded += match.captures.at(capture);
      }
      i = j - 1;
    } else {
      expanded += ch;
    }
  }
  return expanded;
}

SearchResults DocumentSearch::replaceAll(const QString& text, const QString& pattern,
                                         const QString& replacement, SearchOptions options,
                                         QString* output) {
  SearchResults result = findAll(text, pattern, options);
  if (!result.valid) { return result; }
  QString replaced = text;
  for (auto it = result.matches.crbegin(); it != result.matches.crend(); ++it) {
    replaced.replace(it->start, it->length,
                     expandReplacement(replacement, *it, options.regularExpression));
  }
  if (output) { *output = std::move(replaced); }
  return result;
}

}  // namespace muffin
