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

// Functional selectors contribute their most specific argument; :where()
// contributes zero. Strings/attribute values never contribute selector tokens.
inline int specificityOf(const QString& selector) {
  int result = 0;
  bool typePosition = true;
  for (int i = 0; i < selector.size();) {
    const QChar c = selector[i];
    if (c.isSpace() || c == '>' || c == '+' || c == '~' || c == ',') {
      typePosition = true;
      ++i;
      continue;
    }
    if (c == '[') {
      result += 100;
      QChar quote;
      for (++i; i < selector.size(); ++i) {
        if (!quote.isNull()) {
          if (selector[i] == QLatin1Char(0x5c))
            ++i;
          else if (selector[i] == quote)
            quote = {};
        } else if (selector[i] == QLatin1Char(0x27) || selector[i] == '"')
          quote = selector[i];
        else if (selector[i] == ']') {
          ++i;
          break;
        }
      }
      typePosition = false;
      continue;
    }
    if (c == '#' || c == '.') {
      result += c == '#' ? 10000 : 100;
      for (++i; i < selector.size() && isIdentChar(selector[i]); ++i) {
      }
    } else if (c == ':') {
      const bool element = i + 1 < selector.size() && selector[i + 1] == ':';
      i += element ? 2 : 1;
      const int start = i;
      while (i < selector.size() && isIdentChar(selector[i])) ++i;
      const auto name = selector.mid(start, i - start).toLower();
      const bool functional = name == QLatin1String("is") || name == QLatin1String("not") || name == QLatin1String("has");
      if (name != QLatin1String("where") && !functional)
        result += element || name == QLatin1String("before") || name == QLatin1String("after") ? 1 : 100;
      if (i < selector.size() && selector[i] == '(') {
        ++i;
        int depth = 1, best = 0, argument = i;
        QChar quote;
        for (; i < selector.size() && depth > 0; ++i) {
          const QChar v = selector[i];
          if (!quote.isNull()) {
            if (v == QLatin1Char(0x5c))
              ++i;
            else if (v == quote)
              quote = {};
            continue;
          }
          if (v == QLatin1Char(0x27) || v == '"') {
            quote = v;
            continue;
          }
          if (v == '(') ++depth;
          if (v == ')') --depth;
          if ((v == ',' && depth == 1) || depth == 0) {
            if (functional) best = qMax(best, specificityOf(selector.mid(argument, i - argument)));
            argument = i + 1;
          }
        }
        if (functional) result += best;
      }
    } else {
      if (typePosition && c.isLetter()) ++result;
      if (isIdentChar(c))
        while (i < selector.size() && isIdentChar(selector[i])) ++i;
      else
        ++i;
    }
    typePosition = false;
  }
  return result;
}

// Shared cascade-winner comparison: importance beats specificity beats source order, last wins
// on full ties (mirroring CSS). Used by BOTH the computed-style engine and the decoration
// extractor — previously two structurally identical `beats()` copies that could drift apart
// (e.g. a future origin/layer rule added to one and not the other). Candidate types need
// `.important`, `.specificity` and `.order` members.
template <typename Candidate>
bool cascadeBeats(const Candidate& a, const Candidate& b) {
  if (a.important != b.important) { return a.important; }
  if (a.specificity != b.specificity) { return a.specificity > b.specificity; }
  return a.order > b.order;
}

}  // namespace muffin
