#include "theme/CssCalc.h"

#include <QtGlobal>

#include <QString>

namespace muffin {

namespace {

// Recursive-descent calc() evaluator. Grammar:
//   expr  := term (('+' | '-') term)*
//   term  := factor (('*' | '/') factor)*
//   factor := number unit | '(' expr ')' | ('+' | '-') factor
// Each "number unit" resolves to px (px=1, pt=96/72, em=emPx, rem=rootPx,
// %=containingPx). On any parse error the whole evaluation yields 0 (the
// surrounding lengthToPx treats 0 as "unset"), matching the single-value path.
struct CalcParser {
  const QString s;
  int i = 0;
  qreal emPx;
  qreal rootPx;
  qreal containingPx;
  bool ok = true;

  void skipSpace() {
    while (i < s.size() && s.at(i).isSpace()) { ++i; }
  }

  // number-with-unit → px. A number is [+-]? digits [. digits]; the unit is the
  // following run of letters or '%'. A nested `calc(...)` factor is evaluated by
  // recursing into a sub-parser on its balanced inner content.
  bool readValue(qreal& out) {
    skipSpace();
    if (s.mid(i, 5).startsWith(QStringLiteral("calc("), Qt::CaseInsensitive)) {
      int depth = 0;
      int j = i;
      for (; j < s.size(); ++j) {
        if (s.at(j) == QLatin1Char('(')) { ++depth; }
        else if (s.at(j) == QLatin1Char(')')) { --depth; if (depth == 0) { break; } }
      }
      if (j >= s.size()) { ok = false; return false; }
      CalcParser sub{s.mid(i + 5, j - (i + 5)).trimmed(), 0, emPx, rootPx, containingPx, true};
      out = sub.parseExpr();
      if (!sub.ok) { ok = false; return false; }
      i = j + 1;
      return true;
    }
    const int start = i;
    if (i < s.size() && (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-'))) { ++i; }
    bool anyDigit = false;
    while (i < s.size() && s.at(i).isDigit()) { ++i; anyDigit = true; }
    if (i < s.size() && s.at(i) == QLatin1Char('.')) {
      ++i;
      while (i < s.size() && s.at(i).isDigit()) { ++i; anyDigit = true; }
    }
    if (!anyDigit) { return false; }
    bool good = false;
    const qreal n = s.mid(start, i - start).toDouble(&good);
    if (!good) { return false; }
    int u = i;
    while (u < s.size() && (s.at(u).isLetter() || s.at(u) == QLatin1Char('%'))) { ++u; }
    const QString unit = s.mid(i, u - i).toLower();
    i = u;
    if (unit == QStringLiteral("px") || unit.isEmpty()) { out = n; }
    else if (unit == QStringLiteral("pt")) { out = n * 96.0 / 72.0; }
    else if (unit == QStringLiteral("em")) { out = n * emPx; }
    else if (unit == QStringLiteral("rem")) { out = n * (rootPx > 0.0 ? rootPx : emPx); }
    else if (unit == QStringLiteral("%")) { out = n / 100.0 * (containingPx > 0.0 ? containingPx : emPx); }
    else { return false; }
    return true;
  }

  qreal parseExpr() {
    qreal v = parseTerm();
    while (ok) {
      skipSpace();
      if (i >= s.size()) { break; }
      const QChar c = s.at(i);
      if (c == QLatin1Char('+')) { ++i; v += parseTerm(); }
      else if (c == QLatin1Char('-')) { ++i; v -= parseTerm(); }
      else { break; }
    }
    return v;
  }

  qreal parseTerm() {
    qreal v = parseFactor();
    while (ok) {
      skipSpace();
      if (i >= s.size()) { break; }
      const QChar c = s.at(i);
      if (c == QLatin1Char('*')) { ++i; v *= parseFactor(); }
      else if (c == QLatin1Char('/')) {
        ++i;
        const qreal d = parseFactor();
        if (qFuzzyIsNull(d)) { ok = false; return 0.0; }
        v /= d;
      } else { break; }
    }
    return v;
  }

  qreal parseFactor() {
    skipSpace();
    if (i >= s.size()) { ok = false; return 0.0; }
    const QChar c = s.at(i);
    if (c == QLatin1Char('(')) {
      ++i;
      const qreal v = parseExpr();
      skipSpace();
      if (i >= s.size() || s.at(i) != QLatin1Char(')')) { ok = false; return 0.0; }
      ++i;
      return v;
    }
    if (c == QLatin1Char('+')) { ++i; return parseFactor(); }
    if (c == QLatin1Char('-')) { ++i; return -parseFactor(); }
    qreal v = 0.0;
    if (!readValue(v)) { ok = false; return 0.0; }
    return v;
  }
};

}  // namespace

qreal evalCalcPx(const QString& expression, qreal emPx, qreal rootPx, qreal containingPx) {
  CalcParser p{expression.trimmed(), 0, emPx, rootPx > 0.0 ? rootPx : emPx, containingPx, true};
  const qreal v = p.parseExpr();
  p.skipSpace();
  if (!p.ok || p.i != p.s.size()) { return 0.0; }  // trailing junk ⇒ no match
  return v;
}

}  // namespace muffin
