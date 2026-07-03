#pragma once

#include <QChar>
#include <QRegularExpression>
#include <QString>

namespace muffin {

// Shared CSS-selector helpers used by BOTH the flat semantic mapper (CssThemeMapper) and the
// computed-style engine (CssComputedStyleEngine). Previously each TU carried its own copy —
// they drifted in naming (specificity vs specificityOf) while staying algorithmically identical.
// Keep them here so a fix applies to both engines at once.

// A CSS identifier character: letter / digit / '-' / '_'.
inline bool isIdentChar(QChar c) {
  return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_');
}

// True when the selector targets an export/outline/sidebar shell that is absent from Muffin's
// live editor DOM (typora-export / -sidebar / -content). If these entered the semantic cascade
// as plain `#write` or `h2` rules, their higher specificity would let export-only page sizing
// and decorations override the live editor style (e.g. `width: 90%`). Such selectors are
// dropped from the live cascade and kept only for export.
inline bool selectorRequiresExportContext(const QString& selector) {
  int paren = 0, brk = 0;
  bool inString = false;
  QChar quote;
  for (int i = 0; i < selector.size(); ++i) {
    const QChar c = selector.at(i);
    if (inString) {
      if (c == quote) { inString = false; }
      else if (c == QLatin1Char('\\') && i + 1 < selector.size()) { ++i; }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inString = true; quote = c; continue; }
    if (c == QLatin1Char('(')) { ++paren; continue; }
    if (c == QLatin1Char(')')) { paren = qMax(0, paren - 1); continue; }
    if (c == QLatin1Char('[')) { ++brk; continue; }
    if (c == QLatin1Char(']')) { brk = qMax(0, brk - 1); continue; }
    if (paren != 0 || brk != 0 || c != QLatin1Char('.')) { continue; }
    int j = i + 1;
    while (j < selector.size() && isIdentChar(selector.at(j))) { ++j; }
    const QString cls = selector.mid(i + 1, j - i - 1).toLower();
    if (cls == QStringLiteral("typora-export") || cls == QStringLiteral("typora-export-sidebar") ||
        cls == QStringLiteral("typora-export-content")) {
      return true;
    }
    i = j - 1;
  }
  return false;
}

// Coarse CSS specificity for the whole selector: (a = #id, b = class/attr/pseudo, c = type),
// packed so larger == more specific. Good enough for cascade ties (themes rarely set the same
// token on conflicting selectors).
inline int specificityOf(const QString& selector) {
  int a = selector.count(QLatin1Char('#'));
  int b = selector.count(QLatin1Char('.')) + selector.count(QLatin1Char('[')) + selector.count(QLatin1Char(':'));
  // type selectors: a letter starting a compound (after a combinator or at start),
  // i.e. not attached to # . : [
  static const QRegularExpression tagRe(QStringLiteral("(^|[\\s>+~])[a-zA-Z]"));
  int c = 0;
  auto it = tagRe.globalMatch(selector);
  while (it.hasNext()) { ++c; it.next(); }
  return a * 10000 + b * 100 + c;
}

}  // namespace muffin
