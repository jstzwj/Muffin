#include "theme/CssValueParser.h"

#include "theme/CssCalc.h"
#include "theme/CssThemeParser.h"
#include "theme/ThemeDefinition.h"

#include <QColor>
#include <QList>
#include <QMarginsF>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

#include <vector>

namespace muffin {

Q_LOGGING_CATEGORY(themeWarn, "muffin.theme.warn", QtCriticalMsg)

namespace {

// Parse one rgb/rgba/hsl/hsla functional-colour argument group into a QColor.
// QColor's string ctor is unreliable for rgba() with a float alpha across Qt
// versions, so the numeric path is the source of truth for functional notation.
qreal channel255(const QString& part) {
  QString t = part.trimmed();
  const bool pct = t.endsWith(QLatin1Char('%'));
  if (pct) { t.chop(1); }
  bool ok = false;
  const double v = t.toDouble(&ok);
  if (!ok) { return -1.0; }
  return pct ? v / 100.0 * 255.0 : v;
}

QColor parseFunctionalColor(const QString& fn, const QString& args) {
  const QStringList parts = CssThemeParser::splitTopLevelCommas(args);
  auto alphaOf = [](const QString& p) -> int {
    QString a = p.trimmed();
    const bool pct = a.endsWith(QLatin1Char('%'));
    if (pct) { a.chop(1); }
    bool ok = false;
    const double av = a.toDouble(&ok);
    if (!ok) { return 255; }
    return qBound(0, qRound(pct ? av / 100.0 * 255.0 : (av <= 1.0 ? av * 255.0 : av)), 255);
  };
  if (fn == QStringLiteral("rgb") || fn == QStringLiteral("rgba")) {
    if (parts.size() < 3) { return QColor(); }
    const double r = channel255(parts[0]);
    const double g = channel255(parts[1]);
    const double b = channel255(parts[2]);
    if (r < 0 || g < 0 || b < 0) { return QColor(); }
    const int alpha = parts.size() >= 4 ? alphaOf(parts[3]) : 255;
    return QColor(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255), qBound(0, qRound(b), 255), alpha);
  }
  if (fn == QStringLiteral("hsl") || fn == QStringLiteral("hsla")) {
    if (parts.size() < 3) { return QColor(); }
    QString h0 = parts[0].trimmed();
    if (h0.endsWith(QStringLiteral("deg"))) { h0.chop(3); }
    bool ok = false;
    const double h = h0.toDouble(&ok);
    if (!ok) { return QColor(); }
    auto sl = [](const QString& p) -> double {
      QString t = p.trimmed();
      if (t.endsWith(QLatin1Char('%'))) { t.chop(1); }
      return t.toDouble();
    };
    const double s = sl(parts[1]);
    const double l = sl(parts[2]);
    QColor c = QColor::fromHslF(h / 360.0, qBound(0.0, s / 100.0, 1.0), qBound(0.0, l / 100.0, 1.0));
    c.setAlpha(parts.size() >= 4 ? alphaOf(parts[3]) : 255);
    return c;
  }
  return QColor();
}

// --- color-mix() (CSS Color 4) ------------------------------------------------
// Community themes tint backgrounds/borders with color-mix(in srgb, <color>,
// transparent N%). It was previously unimplemented, so extractColor fell
// through to the #hex scanner and grabbed the operand verbatim — e.g. a code
// background of color-mix(in srgb, var(--primary-color), transparent 90%)
// became FULL-SATURATION #00f3ff instead of a 10% cyan wash, and a blockquote
// border became full #bd93f9 (the "headings/code/blockquote turned cyan-purple"
// regression). We evaluate the premultiplied blend per spec and KEEP alpha, so
// the painter composites the tint over the page background correctly. Only
// `in srgb` (and any other space, treated as srgb — good enough for community
// themes, which only ever use srgb) is supported; hue-interpolation hints are
// accepted and ignored.

// Locate the first balanced color-mix(...) call in `s`; return its inner args
// (between the parens), or empty. Balanced because operands may themselves
// contain parentheses (rgba(), nested color-mix()). Works whether the call is
// the whole value or embedded in a shorthand like `1px solid color-mix(...)`.
QString findColorMixInner(const QString& s) {
  const int at = s.indexOf(QStringLiteral("color-mix("), 0, Qt::CaseInsensitive);
  if (at < 0) { return QString(); }
  const int openParen = at + QStringLiteral("color-mix").length();  // index of '('
  int depth = 0;
  for (int i = openParen; i < s.size(); ++i) {
    const QChar ch = s.at(i);
    if (ch == QLatin1Char('(')) {
      ++depth;
    } else if (ch == QLatin1Char(')')) {
      --depth;
      if (depth == 0) { return s.mid(openParen + 1, i - openParen - 1); }
    }
  }
  return QString();  // unbalanced — leave to the legacy fallback
}

// Resolve one color-mix operand. `transparent` is a valid zero-alpha colour,
// but the legacy word scanner skips it, so handle it explicitly here.
// (extractColor is declared in the header; the call below resolves at link time.)
QColor operandColor(const QString& token, const QHash<QString, QString>& vars) {
  const QString t = token.trimmed();
  if (t.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) == 0) { return QColor(0, 0, 0, 0); }
  if (t.compare(QStringLiteral("currentcolor"), Qt::CaseInsensitive) == 0) { return QColor(); }  // unresolvable
  return extractColor(t, vars);
}

// Premultiplied blend of two colours by normalised weights; alpha preserved
// so the result composites correctly when painted over the page.
QColor blendColors(const QColor& c1, const QColor& c2, double w1, double w2) {
  const double a1 = c1.alphaF();
  const double a2 = c2.alphaF();
  const double ra = w1 * a1 + w2 * a2;
  if (ra <= 0.0) { return QColor(0, 0, 0, 0); }
  const auto channel = [&](int v1, int v2) {
    const double prem = w1 * (v1 / 255.0) * a1 + w2 * (v2 / 255.0) * a2;  // premultiplied
    return qBound(0, qRound(prem / ra * 255.0), 255);                      // un-premultiply
  };
  return QColor(channel(c1.red(), c2.red()), channel(c1.green(), c2.green()),
                channel(c1.blue(), c2.blue()), qBound(0, qRound(ra * 255.0), 255));
}

struct MixOperand { QColor color; double pct; };  // pct = -1 when no percentage given

// Parse one operand group "<color> [<p>%]" → colour + optional weight.
MixOperand readMixOperand(const QString& group, const QHash<QString, QString>& vars) {
  QString t = group.trimmed();
  double pct = -1.0;
  static const QRegularExpression trailingPct(QStringLiteral("\\s+(-?[0-9]+(?:\\.[0-9]+)?)%\\s*$"));
  const QRegularExpressionMatch m = trailingPct.match(t);
  if (m.hasMatch()) {
    pct = m.captured(1).toDouble();
    t = t.left(m.capturedStart()).trimmed();
  }
  return {operandColor(t, vars), pct};
}

// Parse "in <space>[, <hint>], c1 [p1%], c2 [p2%]" and return the blend.
QColor parseColorMix(const QString& inner, const QHash<QString, QString>& vars) {
  const QStringList groups = CssThemeParser::splitTopLevelCommas(inner);
  int idx = 0;
  // Optional leading interpolation spec: `in srgb`, `in oklch longer hue`, …
  if (idx < groups.size() && groups[idx].trimmed().startsWith(QStringLiteral("in"), Qt::CaseInsensitive)) {
    ++idx;
  }
  if (groups.size() - idx < 2) { return QColor(); }
  const MixOperand a = readMixOperand(groups[idx], vars);
  const MixOperand b = readMixOperand(groups[idx + 1], vars);
  if (!a.color.isValid() || !b.color.isValid()) { return QColor(); }
  double w1, w2;
  if (a.pct < 0 && b.pct < 0) {
    w1 = w2 = 0.5;
  } else if (a.pct < 0) {
    w2 = qBound(0.0, b.pct / 100.0, 1.0);
    w1 = 1.0 - w2;
  } else if (b.pct < 0) {
    w1 = qBound(0.0, a.pct / 100.0, 1.0);
    w2 = 1.0 - w1;
  } else {
    const double s = a.pct + b.pct;
    if (s <= 0.0) { w1 = w2 = 0.5; } else { w1 = a.pct / s; w2 = b.pct / s; }
  }
  return blendColors(a.color, b.color, w1, w2);
}

// --- gradients (linear/radial/conic) -----------------------------------------
// Reuses extractColor (handles var/color-mix/rgb/hex/named), so stop colours resolve through
// the same path as every other theme colour. Carried as rect-independent data; GradientPainter
// builds a QGradient per target rect.

QString gradientParenInner(const QString& s) {
  const int open = s.indexOf(QLatin1Char('('));
  if (open < 0) { return s; }
  int depth = 0;
  for (int i = open; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if (c == QLatin1Char('(')) { ++depth; }
    else if (c == QLatin1Char(')')) { --depth; if (depth == 0) { return s.mid(open + 1, i - open - 1); } }
  }
  return s.mid(open + 1);
}

bool gradientPartIsColor(const QString& part, const QHash<QString, QString>& vars) {
  if (part.contains(QStringLiteral("transparent"), Qt::CaseInsensitive)) { return true; }
  return extractColor(part, vars).isValid();
}

GradientStop parseGradientStop(const QString& part, const QHash<QString, QString>& vars) {
  GradientStop s;
  if (part.contains(QStringLiteral("transparent"), Qt::CaseInsensitive)) {
    s.color = QColor(Qt::transparent);
  } else {
    s.color = extractColor(part, vars);
  }
  s.position = -1.0;  // unset marker
  static const QRegularExpression pctRe(QStringLiteral("(^|\\s)([0-9.]+)%"));
  const QRegularExpressionMatch m = pctRe.match(part);
  if (m.hasMatch()) { s.position = m.captured(2).toDouble() / 100.0; }
  return s;
}

void assignImplicitStopPositions(std::vector<GradientStop>& stops) {
  if (stops.empty()) { return; }
  if (stops.front().position < 0.0) { stops.front().position = 0.0; }
  if (stops.back().position < 0.0) { stops.back().position = 1.0; }
  for (size_t i = 0; i < stops.size(); ++i) {
    if (stops[i].position >= 0.0) { continue; }
    size_t lo = i;
    while (lo > 0 && stops[lo].position < 0.0) { --lo; }
    size_t hi = i;
    while (hi + 1 < stops.size() && stops[hi].position < 0.0) { ++hi; }
    const qreal plo = stops[lo].position;
    const qreal phi = stops[hi].position;
    const qreal count = static_cast<qreal>(hi - lo);
    stops[i].position = plo + (phi - plo) * static_cast<qreal>(i - lo) / count;
  }
  for (auto& s : stops) { s.position = qBound(0.0, s.position, 1.0); }
}

void parseGradientDirection(const QString& part, GradientSpec& spec) {
  const QString p = part.trimmed().toLower();
  if (spec.kind == GradientSpec::Kind::Linear) {
    bool ok = false;
    if (p.endsWith(QStringLiteral("deg"))) {
      const qreal a = p.left(p.size() - 3).trimmed().toDouble(&ok);
      if (ok) { spec.angleDeg = a; }
    } else if (p.endsWith(QStringLiteral("rad"))) {
      const qreal a = p.left(p.size() - 3).trimmed().toDouble(&ok);
      if (ok) { spec.angleDeg = qRadiansToDegrees(a); }
    } else if (p.startsWith(QStringLiteral("to "))) {
      const QStringList sides = p.mid(3).split(QLatin1Char(' '), Qt::SkipEmptyParts);
      qreal dx = 0.0, dy = 0.0;
      for (const QString& s : sides) {
        if (s == QStringLiteral("top")) { dy = -1.0; }
        else if (s == QStringLiteral("bottom")) { dy = 1.0; }
        else if (s == QStringLiteral("left")) { dx = -1.0; }
        else if (s == QStringLiteral("right")) { dx = 1.0; }
      }
      if (dx != 0.0 || dy != 0.0) {
        qreal a = qRadiansToDegrees(qAtan2(dx, -dy));  // CSS: 0=to top, clockwise
        if (a < 0.0) { a += 360.0; }
        spec.angleDeg = a;
      }
    }
  } else if (spec.kind == GradientSpec::Kind::Radial) {
    QString pos;
    const int atIdx = p.indexOf(QStringLiteral("at"));
    if (atIdx >= 0) { pos = p.mid(atIdx + 2).trimmed(); }
    static const QHash<QString, QPointF> named = {
        {QStringLiteral("center"), QPointF(0.5, 0.5)}, {QStringLiteral("centre"), QPointF(0.5, 0.5)},
        {QStringLiteral("top"), QPointF(0.5, 0.0)}, {QStringLiteral("bottom"), QPointF(0.5, 1.0)},
        {QStringLiteral("left"), QPointF(0.0, 0.5)}, {QStringLiteral("right"), QPointF(1.0, 0.5)},
        {QStringLiteral("top left"), QPointF(0.0, 0.0)}, {QStringLiteral("left top"), QPointF(0.0, 0.0)},
        {QStringLiteral("top right"), QPointF(1.0, 0.0)}, {QStringLiteral("right top"), QPointF(1.0, 0.0)},
        {QStringLiteral("bottom left"), QPointF(0.0, 1.0)}, {QStringLiteral("left bottom"), QPointF(0.0, 1.0)},
        {QStringLiteral("bottom right"), QPointF(1.0, 1.0)}, {QStringLiteral("right bottom"), QPointF(1.0, 1.0)}};
    if (!pos.isEmpty()) {
      if (named.contains(pos)) {
        spec.radialCenter = named.value(pos);
      } else if (pos.contains(QLatin1Char('%'))) {
        const QStringList xy = pos.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (xy.size() == 2 && xy.at(0).endsWith(QLatin1Char('%')) && xy.at(1).endsWith(QLatin1Char('%'))) {
          bool okx = false, oky = false;
          const qreal fx = xy.at(0).left(xy.at(0).size() - 1).toDouble(&okx) / 100.0;
          const qreal fy = xy.at(1).left(xy.at(1).size() - 1).toDouble(&oky) / 100.0;
          if (okx && oky) { spec.radialCenter = QPointF(fx, fy); }
        }
      } else {
        // Word combination: "center bottom", "top left", … Start centred and
        // apply each keyword (e.g. phycat's h2 `ellipse at center bottom`).
        QPointF c(0.5, 0.5);
        for (const QString& w : pos.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
          if (w == QStringLiteral("top")) { c.setY(0.0); }
          else if (w == QStringLiteral("bottom")) { c.setY(1.0); }
          else if (w == QStringLiteral("left")) { c.setX(0.0); }
          else if (w == QStringLiteral("right")) { c.setX(1.0); }
        }
        spec.radialCenter = c;
      }
    }
    if (p.contains(QStringLiteral("closest-side"))) { spec.radialRadius = 0.5; }
    else if (p.contains(QStringLiteral("farthest-side"))) { spec.radialRadius = 1.0; }
    else if (p.contains(QStringLiteral("farthest-corner"))) { spec.radialRadius = 0.7071; }
  }
}

// Conic direction: `from <angle> [at <position>]`. Only `from` (the start angle)
// is parsed here; the `at` center defaults to the box centre, which is what the
// vast majority of conic themes use. Angles accept deg/rad/turn/grad.
void parseConicDirection(const QString& part, GradientSpec& spec) {
  const QString p = part.trimmed();
  static const QRegularExpression fromRe(
      QStringLiteral("from\\s+([+-]?\\d*\\.?\\d+)\\s*(deg|rad|turn|grad)?"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = fromRe.match(p);
  if (m.hasMatch()) {
    bool ok = false;
    qreal n = m.captured(1).toDouble(&ok);
    if (ok) {
      const QString u = m.captured(2).toLower();
      if (u == QStringLiteral("rad")) { n = qRadiansToDegrees(n); }
      else if (u == QStringLiteral("turn")) { n *= 360.0; }
      else if (u == QStringLiteral("grad")) { n *= 0.9; }
      spec.conicStartDeg = n;
    }
  }
}

}  // namespace

QColor extractColor(const QString& value, const QHash<QString, QString>& vars) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty()) { return QColor(); }
  // color-mix() — evaluate before the #hex scanner can grab the operand verbatim.
  if (const QString mixInner = findColorMixInner(resolved); !mixInner.isEmpty()) {
    if (QColor mixed = parseColorMix(mixInner, vars); mixed.isValid()) { return mixed; }
  }
  QColor direct = cssColor(resolved);
  if (direct.isValid()) { return direct; }
  // Functional rgb()/rgba()/hsl()/hsla() — parsed numerically.
  static const QRegularExpression funcRe(QStringLiteral("(rgba?|hsla?)\\(([^)]*)\\)"),
                                         QRegularExpression::CaseInsensitiveOption);
  auto fit = funcRe.globalMatch(resolved);
  while (fit.hasNext()) {
    const QRegularExpressionMatch m = fit.next();
    QColor c = parseFunctionalColor(m.captured(1).toLower(), m.captured(2));
    if (c.isValid()) { return c; }
  }
  // #hex token inside a shorthand (e.g. `1px solid #d0d7de`).
  static const QRegularExpression hexRe(QStringLiteral("#[0-9a-fA-F]{3,8}"));
  auto hit = hexRe.globalMatch(resolved);
  while (hit.hasNext()) {
    QColor c = cssColor(hit.next().captured(0));
    if (c.isValid()) { return c; }
  }
  // Bare named colour word (e.g. `red`) — last resort, skipping style keywords.
  static const QRegularExpression wordRe(QStringLiteral("[a-zA-Z]+"));
  auto wit = wordRe.globalMatch(resolved);
  while (wit.hasNext()) {
    const QString w = wit.next().captured(0);
    if (w == QStringLiteral("solid") || w == QStringLiteral("dashed") || w == QStringLiteral("dotted") ||
        w == QStringLiteral("none") || w == QStringLiteral("double") || w == QStringLiteral("ridge") ||
        w == QStringLiteral("groove") || w == QStringLiteral("outset") || w == QStringLiteral("inset") ||
        w == QStringLiteral("transparent") || w == QStringLiteral("currentColor")) {
      continue;
    }
    QColor c(w);
    if (c.isValid()) { return c; }
  }
  // Non-empty input survived no parse strategy — a malformed/unresolved colour that would
  // otherwise silently become "no value" upstream (and, reaching a QPainter brush unguarded,
  // solid black). Surface it so the failure is observable during theme work — enable with
  // QT_LOGGING_RULES="muffin.theme.warn=true".
  qCWarning(themeWarn).nospace() << "unresolved colour value \"" << resolved << "\"";
  return QColor();
}

// --- length / box / border parsing -------------------------------------------

QStringList splitTopLevelSpaces(const QString& text) {
  QStringList out;
  QString cur;
  int paren = 0;
  bool inString = false;
  QChar quote;
  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (inString) {
      cur += c;
      if (c == quote) { inString = false; }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inString = true; quote = c; cur += c; continue; }
    if (c == QLatin1Char('(')) { ++paren; cur += c; continue; }
    if (c == QLatin1Char(')')) { if (paren > 0) --paren; cur += c; continue; }
    if (paren == 0 && c.isSpace()) {
      if (!cur.trimmed().isEmpty()) { out << cur.trimmed(); cur.clear(); }
    } else {
      cur += c;
    }
  }
  if (!cur.trimmed().isEmpty()) { out << cur.trimmed(); }
  return out;
}

// CSS length → points. Handles px/rem/em/%/pt/numbers relative to the supplied em size.
qreal lengthToPt(const QString& value, const QHash<QString, QString>& vars, qreal emPx) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty()) { return 0.0; }
  // Parse leading number (optional sign, digits, decimal point).
  int i = 0;
  while (i < resolved.size() && (resolved.at(i).isDigit() || resolved.at(i) == QLatin1Char('.') ||
                                 resolved.at(i) == QLatin1Char('-') || resolved.at(i) == QLatin1Char('+'))) {
    ++i;
  }
  const QString numStr = resolved.left(i);
  const QString unit = resolved.mid(i).trimmed().toLower();
  bool ok = false;
  const qreal n = numStr.toDouble(&ok);
  if (!ok) { return 0.0; }
  if (unit == QStringLiteral("px")) { return pxToPt(n); }
  if (unit == QStringLiteral("pt")) { return n; }
  if (unit == QStringLiteral("rem")) { return pxToPt(n * 16.0); }
  if (unit == QStringLiteral("em")) { return pxToPt(n * emPx); }
  if (unit == QStringLiteral("%")) { return pxToPt(n / 100.0 * emPx); }
  if (unit.isEmpty()) { return pxToPt(n); }  // bare number → treat as px
  return 0.0;
}

qreal lengthToPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx, qreal rootPx, qreal containingPx) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty() || resolved == QStringLiteral("auto")) { return 0.0; }
  // calc(<expr>) — a full + - * / expression with nested parens and per-term
  // units (px/pt/em/rem/%). evalCalcPx resolves it to px; 0 ⇒ parse failure
  // (treated as "unset", same as an unrecognised single value below).
  if (resolved.startsWith(QStringLiteral("calc("), Qt::CaseInsensitive) && resolved.endsWith(QLatin1Char(')'))) {
    return evalCalcPx(resolved.mid(5, resolved.size() - 6), emPx, rootPx, containingPx);
  }
  int i = 0;
  while (i < resolved.size() && (resolved.at(i).isDigit() || resolved.at(i) == QLatin1Char('.') ||
                                 resolved.at(i) == QLatin1Char('-') || resolved.at(i) == QLatin1Char('+'))) {
    ++i;
  }
  bool ok = false;
  const qreal n = resolved.left(i).toDouble(&ok);
  if (!ok) { return 0.0; }
  const QString unit = resolved.mid(i).trimmed().toLower();
  if (unit == QStringLiteral("px") || unit.isEmpty()) { return n; }
  if (unit == QStringLiteral("pt")) { return n * 96.0 / 72.0; }
  if (unit == QStringLiteral("em")) { return n * emPx; }
  // CSS `rem` is the ROOT em (the html element's font, 16px by default) — NOT the
  // current element's em. When a theme sets `body { font-size: 1.5rem }` (→ 24px),
  // resolving subsequent `rem` values against that body em (the old `emPx` fallback)
  // made EVERY rem size 1.5× too big: pixyll's `h2 { font-size: 1.5rem }` became 36px
  // instead of 24px. The root reference (16) is the same base bodyPx itself is computed
  // against, so this keeps rem consistent with how the body size is derived.
  if (unit == QStringLiteral("rem")) { return n * (rootPx > 0.0 ? rootPx : 16.0); }
  // A `%` is normally em-relative (local box shorthand). When the caller supplies
  // a real containing-block dimension (containingPx > 0), resolve against THAT —
  // for pseudo width/height like phycat's `h3::before { height: 61% }`, where the %
  // is relative to the rendered heading height, not the font size.
  if (unit == QStringLiteral("%")) { return n / 100.0 * (containingPx > 0.0 ? containingPx : emPx); }
  return 0.0;
}

QMarginsF boxToMarginsPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx, qreal rootPx) {
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(value, vars));
  if (parts.isEmpty()) { return QMarginsF(); }
  qreal v[4] = {};
  for (int i = 0; i < qMin(4, parts.size()); ++i) { v[i] = lengthToPx(parts.at(i), vars, emPx, rootPx); }
  qreal top = v[0], right = v[0], bottom = v[0], left = v[0];
  if (parts.size() == 2) { right = left = v[1]; }
  else if (parts.size() == 3) { right = left = v[1]; bottom = v[2]; }
  else if (parts.size() >= 4) { right = v[1]; bottom = v[2]; left = v[3]; }
  return QMarginsF(left, top, right, bottom);
}

qreal borderWidthPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx) {
  for (const QString& part : splitTopLevelSpaces(CssThemeParser::resolveVars(value, vars))) {
    const qreal px = lengthToPx(part, vars, emPx);
    if (px > 0.0) { return px; }
  }
  return 0.0;
}

qreal shadowBlurPx(const QString& shadowRaw, const QHash<QString, QString>& vars) {
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(shadowRaw, vars));
  QList<qreal> nums;
  for (const QString& p : parts) {
    const QString t = p.trimmed();
    // Only treat length tokens (leading digit/sign/dot); skip colour tokens like rgba(...).
    if (t.isEmpty() || !(t.at(0).isDigit() || t.at(0) == QLatin1Char('+') ||
                          t.at(0) == QLatin1Char('-') || t.at(0) == QLatin1Char('.'))) {
      continue;
    }
    nums.append(lengthToPx(t, vars, 16.0));  // handles 15px / 0 / em
  }
  if (nums.size() >= 3) { return nums.at(2); }
  if (nums.size() >= 2) { return 8.0; }
  return 0.0;
}

GradientSpec parseGradientSpec(const QString& raw, const QHash<QString, QString>& vars) {
  GradientSpec spec;
  const QString resolved = CssThemeParser::resolveVars(raw, vars).trimmed();
  if (resolved.isEmpty()) { return spec; }
  QString inner;
  const auto startsWithKw = [&](const char* kw) {
    return resolved.startsWith(QString::fromLatin1(kw), Qt::CaseInsensitive);
  };
  if (startsWithKw("linear-gradient")) { spec.kind = GradientSpec::Kind::Linear; inner = gradientParenInner(resolved); }
  else if (startsWithKw("radial-gradient")) { spec.kind = GradientSpec::Kind::Radial; inner = gradientParenInner(resolved); }
  else if (startsWithKw("conic-gradient")) { spec.kind = GradientSpec::Kind::Conic; inner = gradientParenInner(resolved); }
  else { return spec; }
  const QStringList parts = CssThemeParser::splitTopLevelCommas(inner);
  if (parts.isEmpty()) { spec.kind = GradientSpec::Kind::None; return spec; }
  int idx = 0;
  if (!parts.first().trimmed().isEmpty() && !gradientPartIsColor(parts.first(), vars)) {
    if (spec.kind == GradientSpec::Kind::Conic) { parseConicDirection(parts.first(), spec); }
    else { parseGradientDirection(parts.first(), spec); }
    idx = 1;
  }
  std::vector<GradientStop> stops;
  for (; idx < parts.size(); ++idx) {
    const GradientStop s = parseGradientStop(parts.at(idx), vars);
    if (s.color.isValid()) { stops.push_back(s); }
  }
  if (stops.empty()) { spec.kind = GradientSpec::Kind::None; return spec; }
  assignImplicitStopPositions(stops);
  spec.stops = stops;
  return spec;
}

}  // namespace muffin
