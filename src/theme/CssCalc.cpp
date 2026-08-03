#include "theme/CssCalc.h"

#include <QtGlobal>

#include <QString>
#include <QStringView>

#include <algorithm>  // std::min/std::max for vmin/vmax
#include <cmath>      // std::isfinite (overflow guard)

namespace muffin {

qreal absoluteCssLengthToPx(qreal value, const QString& unit, bool* recognised) {
  qreal scale = 0.0;
  bool ok = true;
  if (unit.isEmpty() || unit == QStringLiteral("px")) scale = 1.0;
  else if (unit == QStringLiteral("pt")) scale = 96.0 / 72.0;
  else if (unit == QStringLiteral("pc")) scale = 16.0;
  else if (unit == QStringLiteral("in")) scale = 96.0;
  else if (unit == QStringLiteral("cm")) scale = 96.0 / 2.54;
  else if (unit == QStringLiteral("mm")) scale = 96.0 / 25.4;
  else if (unit == QStringLiteral("q")) scale = 96.0 / 101.6;
  else ok = false;
  if (recognised) *recognised = ok;
  return ok ? value * scale : 0.0;
}

CssLengthResult resolveCssLengthToPx(QStringView raw, const CssLengthContext& ctx) {
  // ASCII case-insensitive units and exponent marker (the whole value is lower-cased).
  const QString s = raw.toString().trimmed().toLower();
  if (s.isEmpty()) return {CssLengthStatus::Missing, 0.0};
  // CSS <number> magnitude: [+-]? ( digits ('.' digits?)? | '.' digits ), then an
  // optional exponent. At least one mantissa digit is required, else this is not a
  // number ("foo", ".", "+").
  int i = 0;
  if (i < s.size() && (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-'))) ++i;
  bool anyDigit = false;
  while (i < s.size() && s.at(i).isDigit()) { ++i; anyDigit = true; }
  if (i < s.size() && s.at(i) == QLatin1Char('.')) {
    ++i;
    while (i < s.size() && s.at(i).isDigit()) { ++i; anyDigit = true; }
  }
  if (!anyDigit) return {CssLengthStatus::Invalid, 0.0};
  // Exponent: 'e' then an optional sign and >=1 digit. If 'e' is NOT followed by
  // [+-]?digit it is the START OF THE UNIT (e.g. "1em"), so the magnitude ends
  // here and 'e' is left for the unit scan below — "1em" must NOT parse as a
  // (bogus) exponent. The value is already lower-cased, so only 'e' can appear.
  if (i < s.size() && s.at(i) == QLatin1Char('e')) {
    int k = i + 1;
    if (k < s.size() && (s.at(k) == QLatin1Char('+') || s.at(k) == QLatin1Char('-'))) ++k;
    if (k < s.size() && s.at(k).isDigit()) {
      i = k;
      while (i < s.size() && s.at(i).isDigit()) ++i;
    }
  }
  bool ok = false;
  const qreal n = s.left(i).toDouble(&ok);
  if (!ok || !std::isfinite(n)) return {CssLengthStatus::Invalid, 0.0};  // overflow -> inf
  // Negatives are returned as Valid(negative px): the resolver is property-
  // agnostic. stroke-width (which rejects negatives) maps Valid<0 to its CSS
  // initial; letter-spacing/word-spacing accept negatives directly.
  // Unit = the run of letters after the magnitude; any trailing char is junk.
  int u = i;
  while (u < s.size() && s.at(u).isLetter()) ++u;
  if (u != s.size()) return {CssLengthStatus::Invalid, 0.0};  // trailing junk
  const QString unit = s.mid(i, u - i);
  bool absolute = false;
  const qreal absPx = absoluteCssLengthToPx(n, unit, &absolute);  // px/pt/pc/in/cm/mm/q/bare
  if (absolute) return {CssLengthStatus::Valid, absPx};
  qreal px = 0.0;
  if (unit == QLatin1String("em")) px = n * ctx.emPx;
  else if (unit == QLatin1String("rem")) px = n * ctx.remPx;
  else if (unit == QLatin1String("ex")) px = n * ctx.exPx;
  else if (unit == QLatin1String("ch")) px = n * ctx.chPx;
  else if (unit == QLatin1String("vw")) px = n / 100.0 * ctx.viewportPx.width();
  else if (unit == QLatin1String("vh")) px = n / 100.0 * ctx.viewportPx.height();
  else if (unit == QLatin1String("vmin"))
    px = n / 100.0 * std::min(ctx.viewportPx.width(), ctx.viewportPx.height());
  else if (unit == QLatin1String("vmax"))
    px = n / 100.0 * std::max(ctx.viewportPx.width(), ctx.viewportPx.height());
  else return {CssLengthStatus::Invalid, 0.0};  // unknown unit
  return {CssLengthStatus::Valid, px};
}

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
    bool absolute = false;
    out = absoluteCssLengthToPx(n, unit, &absolute);
    if (!absolute && unit == QStringLiteral("em")) { out = n * emPx; }
    else if (absolute) { return true; }
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
