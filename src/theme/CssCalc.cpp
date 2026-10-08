#include "theme/CssCalc.h"

#include <QtGlobal>

#include <QString>
#include <QStringView>
#include <QRegularExpression>

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
struct Dimension {
  qreal px = 0, fraction = 0;
  bool number = false, percentage = false;
};

// Dimensional arithmetic prevents e.g. calc(2px * 3px) from becoming a length.
class LengthExpression {
 public:
  QString text;
  const CssLengthContext& context;
  qsizetype pos = 0;
  bool valid = true;
  int depth = 0;

  void spaces() {
    while (pos < text.size() && text[pos].isSpace()) ++pos;
  }
  Dimension sum() {
    Dimension a = product();
    while (valid) {
      const qsizetype end = pos;
      spaces();
      if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) break;
      // CSS requires whitespace on both sides of binary + and -.
      const bool subtract = text[pos] == '-';
      if (pos == end || pos + 1 >= text.size() || !text[pos + 1].isSpace()) {
        valid = false;
        break;
      }
      ++pos;
      Dimension b = product();
      if (a.number != b.number) {
        valid = false;
        break;
      }
      a.px += (subtract ? -1 : 1) * b.px;
      a.fraction += (subtract ? -1 : 1) * b.fraction;
      a.percentage = a.percentage || b.percentage;
    }
    return a;
  }
  Dimension product() {
    Dimension a = factor();
    while (valid) {
      const qsizetype before = pos;
      spaces();
      if (pos >= text.size() || (text[pos] != '*' && text[pos] != '/')) {
        pos = before;
        break;
      }
      const bool divide = text[pos++] == '/';
      Dimension b = factor();
      if (divide) {
        if (!b.number || b.px == 0) {
          valid = false;
          break;
        }
        a.px /= b.px;
        a.fraction /= b.px;
      } else {
        if (!a.number && !b.number) {
          valid = false;
          break;
        }
        if (a.number) std::swap(a, b);
        a.px *= b.px;
        a.fraction *= b.px;
      }
    }
    return a;
  }
  Dimension factor() {
    spaces();
    if (++depth > 64 || pos >= text.size()) {
      valid = false;
      --depth;
      return {};
    }
    Dimension result;
    if (text.mid(pos, 5) == QLatin1String("calc(")) pos += 4;
    if (text[pos] == '(') {
      ++pos;
      result = sum();
      spaces();
      if (pos >= text.size() || text[pos++] != ')') valid = false;
    } else {
      static const QRegularExpression token(QStringLiteral(R"([+-]?(?:\d*\.\d+|\d+)(?:e[+-]?\d+)?(?:%|[a-z]+)?)"));
      const auto m = token.match(text, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
      if (!m.hasMatch())
        valid = false;
      else {
        const QString value = m.captured();
        pos = m.capturedEnd();
        if (value.endsWith('%')) {
          result.fraction = value.left(value.size() - 1).toDouble() / 100;
          result.percentage = true;
        } else {
          bool number = false;
          result.px = value.toDouble(&number);
          result.number = number;
          if (!number) {
            const auto length = resolveCssLengthToPx(QStringView(value), context);
            valid = length.status == CssLengthStatus::Valid;
            result.px = length.px;
          }
        }
      }
    }
    --depth;
    valid = valid && std::isfinite(result.px) && std::isfinite(result.fraction);
    return result;
  }
};
}  // namespace

CssLengthPercentage parseCssLengthPercentage(QStringView raw, const CssLengthContext& context, bool allowUnitless) {
  const QString text = raw.toString().trimmed().toLower();
  if (text.isEmpty()) return {};
  LengthExpression parser{text, context};
  if (text.startsWith(QLatin1Char('('))) return {CssLengthStatus::Invalid};
  const Dimension result = parser.factor();
  parser.spaces();
  if (!parser.valid || parser.pos != text.size() || (result.number && result.px != 0 && !allowUnitless)) return {CssLengthStatus::Invalid};
  return {CssLengthStatus::Valid, result.px, result.fraction, result.percentage};
}

qreal evalCalcPx(const QString& expression, qreal emPx, qreal rootPx, qreal containingPx) {
  CssLengthContext context;
  context.emPx = emPx;
  context.remPx = rootPx >= 0 ? rootPx : emPx;
  const auto value = parseCssLengthPercentage(QStringView(QStringLiteral("calc(") + expression + QLatin1Char(')')), context, true);
  return value.status == CssLengthStatus::Valid ? value.used(containingPx >= 0 ? containingPx : emPx) : 0;
}

}  // namespace muffin
