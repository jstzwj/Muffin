// Requirement text-style resolution + text-transform engine (Commit 3). See the
// header for the full contract. Pure: identical inputs -> identical output, so it
// is called from BOTH RequirementLayout (measure) and RequirementScene (build).
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and the public functions use
// fully-qualified names.

#include "mermaid/requirement/RequirementTextStyle.h"

#include "mermaid/flowchart/FlowLabel.h"  // makeFlowLabelFont
#include "mermaid/theme/MermaidColor.h"   // color::isParsableColor / toQColor
#include "theme/CssCalc.h"                // CssLengthContext, resolveCssLengthToPx

#include <QChar>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QSizeF>
#include <QString>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>

namespace flowchart = muffin::mermaid::flowchart;
using muffin::CssLengthContext;
using muffin::CssLengthResult;
using muffin::CssLengthStatus;
using muffin::resolveCssLengthToPx;

namespace muffin::mermaid::requirement {
namespace {

// mmdc default raster profile (same value as RequirementScene.cpp's
// kMmdcDefaultCssViewport); kept file-local so this TU is self-contained.
const QSizeF kMmdcViewport{800.0, 600.0};

// Chrome clamps the computed font-size to 10000px at every cascade layer
// (probed: 10001px / 99999px / 10em -> 10000). See STEP0F §1.4.
constexpr qreal kChromeFontCapPx = 10000.0;

// One CSS bolder/lighter step relative to the parent weight (CSS2 §15.5.1 bins).
// Reproduces STEP0D B.3: bolder 400->700->900 (cap), lighter -> 100 (cap).
QFont::Weight bolderStep(QFont::Weight w) {
  const int n = static_cast<int>(w);
  if (n < 400) return QFont::Normal;   // 100/200/300 -> 400
  if (n < 600) return QFont::Bold;     // 400/500 -> 700
  return QFont::Black;                 // 600..900 -> 900
}
QFont::Weight lighterStep(QFont::Weight w) {
  const int n = static_cast<int>(w);
  if (n >= 800) return QFont::Bold;    // 800/900 -> 700
  if (n >= 600) return QFont::Normal;  // 600/700 -> 400
  return QFont::Thin;                  // ..500 -> 100
}

// Resolve a single font-weight declaration against the parent weight.
QFont::Weight resolveWeightOne(const QString& text, QFont::Weight parentWeight) {
  const QString t = text.trimmed().toLower();
  if (t.isEmpty()) return parentWeight;        // unset -> inherit
  if (t == QLatin1String("normal")) return QFont::Normal;
  if (t == QLatin1String("bold")) return QFont::Bold;
  if (t == QLatin1String("bolder")) return bolderStep(parentWeight);
  if (t == QLatin1String("lighter")) return lighterStep(parentWeight);
  // CSS-wide keywords (probed vs mermaid 11.16.0: inherit/initial/unset/revert/
  // revert-layer all resolve to 400 in the default theme). font-weight is an
  // INHERITED property, so inherit/unset take the parent weight; initial/revert/
  // revert-layer fall back to the property's initial value (Normal = 400).
  if (t == QLatin1String("inherit") || t == QLatin1String("unset"))
    return parentWeight;
  if (t == QLatin1String("initial") || t == QLatin1String("revert") ||
      t == QLatin1String("revert-layer"))
    return QFont::Normal;
  bool ok = false;
  const double w = t.toDouble(&ok);
  if (ok && w >= 1.0 && w <= 1000.0)
    return static_cast<QFont::Weight>(static_cast<int>(std::round(w)));
  return parentWeight;  // invalid -> inherit (probe: inert)
}

// A VALID font-weight declaration. Used to decide whether the name row's default
// bold still applies: a declared weight wins on every row, so the default bold is
// suppressed only when this is true (unset/invalid -> inherit reqTitle bold).
// Per the probe (mermaid 11.16.0), the CSS-wide keywords inherit/initial/unset/
// revert/revert-layer are VALID declarations too (they all resolve to 400 and
// suppress the default bold); only a truly garbage value (e.g. "foo") is inert.
bool isValidFontWeight(const QString& text) {
  const QString t = text.trimmed().toLower();
  if (t.isEmpty()) return false;
  if (t == QLatin1String("normal") || t == QLatin1String("bold") ||
      t == QLatin1String("bolder") || t == QLatin1String("lighter"))
    return true;
  if (t == QLatin1String("inherit") || t == QLatin1String("initial") ||
      t == QLatin1String("unset") || t == QLatin1String("revert") ||
      t == QLatin1String("revert-layer"))
    return true;
  bool ok = false;
  const double w = t.toDouble(&ok);
  return ok && w >= 1.0 && w <= 1000.0;
}

// A CssLengthContext for a length property resolved against `emPx` + the
// font-metric ex/ch of `metricFont` (the layer's actual font). viewport/rem are
// layer-invariant; emPx/exPx/chPx are parent-coupled in the font-size loop.
CssLengthContext layerCtx(qreal emPx, const QFont& metricFont) {
  const QFontMetricsF m(metricFont);
  return CssLengthContext{emPx, 16.0, m.xHeight(), m.horizontalAdvance(QChar('0')),
                          kMmdcViewport};
}

// True iff the ENTIRE trimmed string is a CSS <number> with NO unit — i.e. a
// bare multiplier for line-height (line-height:1e1 == 10 x font-size, distinct
// from the length line-height:1e1px == 10px). Mirrors resolveCssLengthToPx's
// number scan ([+-]? (\d+ | \d*\.\d+) ([eE] [+-]? \d+)?), but requires the WHOLE
// string to be consumed: a trailing unit run (em/px/...) or a stray letter
// ("1e" with no exponent digits) makes this false so the caller falls back to
// the length resolver. Replaces a naive "contains no letter" check that mistook
// the exponent 'e' (1e1) for a unit and parsed both 1e1 and 1e1px as 10px.
bool isUnitlessCssNumber(const QString& raw) {
  const QString t = raw.trimmed();
  if (t.isEmpty()) return false;
  int i = 0;
  const int n = t.size();
  if (i < n && (t.at(i) == QLatin1Char('+') || t.at(i) == QLatin1Char('-'))) ++i;
  bool anyDigit = false;
  while (i < n && t.at(i).isDigit()) { ++i; anyDigit = true; }
  if (i < n && t.at(i) == QLatin1Char('.')) {
    ++i;
    while (i < n && t.at(i).isDigit()) { ++i; anyDigit = true; }
  }
  if (!anyDigit) return false;
  if (i < n && (t.at(i) == QLatin1Char('e') || t.at(i) == QLatin1Char('E'))) {
    int k = i + 1;
    if (k < n && (t.at(k) == QLatin1Char('+') || t.at(k) == QLatin1Char('-'))) ++k;
    if (k < n && t.at(k).isDigit()) {
      i = k;
      while (i < n && t.at(i).isDigit()) ++i;
    } else {
      return false;  // 'e' with no exponent digits -> not a pure number
    }
  }
  return i == n;  // whole string consumed -> no unit
}

// Case-convert (upper/lower) a source string while preserving `$$...$$` block-math
// spans verbatim (math is exempt from text-transform — STEP0F §7).
QString caseWithMathPreserved(const QString& src, bool upper) {
  QString out;
  out.reserve(src.size());
  int i = 0;
  while (i < src.size()) {
    const int dd = src.indexOf(QStringLiteral("$$"), i);
    if (dd < 0) {
      out += upper ? src.mid(i).toUpper() : src.mid(i).toLower();
      break;
    }
    out += upper ? src.mid(i, dd - i).toUpper() : src.mid(i, dd - i).toLower();
    const int close = src.indexOf(QStringLiteral("$$"), dd + 2);
    const int end = (close < 0) ? src.size() : close + 2;
    out += src.mid(dd, end - dd);  // math verbatim
    i = end;
  }
  return out;
}

// Capitalize using Unicode word boundaries (QTextBoundaryFinder::Word == Chrome's
// UAX#29). Markdown markers `*` and backtick are transparent (skipped, do not
// restart a word); `$$...$$` math spans are preserved and act as a word boundary.
// Only the first CASED character of each word is titled; other chars unchanged.
QString capitalizeWithMathPreserved(const QString& src) {
  QString mapped;
  QVector<int> srcPos;
  mapped.reserve(src.size());
  int i = 0;
  while (i < src.size()) {
    if (i + 1 < src.size() && src.at(i) == QLatin1Char('$') &&
        src.at(i + 1) == QLatin1Char('$')) {
      const int close = src.indexOf(QStringLiteral("$$"), i + 2);
      const int end = (close < 0) ? src.size() : close + 2;
      mapped += QLatin1Char(' ');  // math -> word boundary
      srcPos.append(i);            // no source letter to title here
      i = end;
    } else {
      const QChar ch = src.at(i);
      if (ch == QLatin1Char('*') || ch == QLatin1Char('`')) {
        // transparent markdown marker: skip (no mapped char, word state unchanged)
      } else {
        mapped += ch;
        srcPos.append(i);
      }
      ++i;
    }
  }
  QString result = src;
  QTextBoundaryFinder bf(QTextBoundaryFinder::Word, mapped);
  bf.toStart();
  for (int pos = 0; pos <= mapped.size();) {
    if (pos < mapped.size() &&
        (bf.boundaryReasons() & QTextBoundaryFinder::StartOfItem)) {
      const QChar c = mapped.at(pos);
      if (c.isLetter()) {
        const int sp = srcPos.value(pos, -1);
        if (sp >= 0 && sp < result.size()) {
          const QChar titled = c.toTitleCase();
          if (titled != QChar::Null) result.replace(sp, 1, titled);
        }
      }
    }
    const int next = bf.toNextBoundary();
    if (next < 0 || next == pos) break;
    pos = next;
  }
  return result;
}

}  // namespace

RequirementTextStyle resolveRequirementTextStyle(
    const QStringList& cssStyles,
    const QString& themeFontFamily, qreal themeFontSize,
    QFont::Weight themeFontWeight, qreal themeLineHeightPx) {
  RequirementTextStyle s;
  if (cssStyles.isEmpty()) return s;  // all defaults (use theme)

  // styles2Map: last value per key wins (split on ':', keep first two segments),
  // mirroring resolveBoxStyle / upstream styles2Map.
  QHash<QString, QString> map;
  for (const QString& decl : cssStyles) {
    const QStringList parts = decl.split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.first().trimmed().isEmpty()) continue;
    const QString key = parts.first().trimmed();
    const QString value = parts.size() >= 2 ? parts.at(1).trimmed() : QString();
    map.insert(key, value);
  }

  // font-family: first comma-token only (mermaid stylesOpt drops the rest).
  if (map.contains(QStringLiteral("font-family"))) {
    const QString v = map.value(QStringLiteral("font-family"));
    if (!v.isEmpty()) {
      const int comma = v.indexOf(QLatin1Char(','));
      s.fontFamily = (comma >= 0 ? v.left(comma) : v).trimmed();
    }
  }
  // font-style: italic/oblique -> StyleItalic (probe: oblique computes as italic
  // upstream, so the two are visually equivalent — STEP0F §5).
  if (map.contains(QStringLiteral("font-style"))) {
    const QString v = map.value(QStringLiteral("font-style")).trimmed().toLower();
    if (v == QLatin1String("italic") || v == QLatin1String("oblique"))
      s.fontStyle = QFont::StyleItalic;
  }

  // --- 3-DOM-layer font-size + font-weight cascade ---
  // em compounds N^3 x root, ex/ch compound through the same 3 layers against each
  // layer's grown font (theme font at L1, node font at L2-3), bolder/lighter step
  // per layer, Chrome clamps computed font-size to 10000px per layer. rem/absolute/
  // viewport are layer-invariant (resolveCssLengthToPx ignores emPx/exPx/chPx for
  // them), so the loop is harmless for those and only compounds em/ex/ch. Probed in
  // STEP0F §1.3 / §0F++ item 2. font-size values containing '.' never reach here
  // (the style-state lexer rejects them upstream -> whole-render parse error).
  const QString nodeFamily = s.fontFamily.isEmpty() ? themeFontFamily : s.fontFamily;
  const QString sizeValue = map.value(QStringLiteral("font-size"));
  const QString weightValue = map.value(QStringLiteral("font-weight"));
  const bool hasWeight =
      map.contains(QStringLiteral("font-weight")) && !weightValue.isEmpty();
  // A valid font-weight declaration wins over the name row's default bold on
  // every row (see isValidFontWeight / the struct comment).
  const bool fontWeightResolved = hasWeight && isValidFontWeight(weightValue);
  s.fontWeightResolved = fontWeightResolved;

  // Validate font-size once at the root layer: negative/invalid -> inert (theme).
  bool sizeActive = false;
  if (map.contains(QStringLiteral("font-size")) && !sizeValue.isEmpty()) {
    const QFont themeFont =
        flowchart::makeFlowLabelFont(themeFontFamily, themeFontSize, themeFontWeight);
    const CssLengthResult r = resolveCssLengthToPx(sizeValue, layerCtx(themeFontSize, themeFont));
    if (r.status == CssLengthStatus::Valid && r.px >= 0.0) sizeActive = true;
  }

  if (sizeActive || hasWeight) {
    qreal parentSize = themeFontSize;
    QFont::Weight parentWeight = themeFontWeight;
    QFont parentFont =
        flowchart::makeFlowLabelFont(themeFontFamily, themeFontSize, themeFontWeight);
    for (int layer = 0; layer < 3; ++layer) {  // foreignObject -> DIV -> SPAN
      qreal size = parentSize;
      if (sizeActive) {
        const CssLengthResult r = resolveCssLengthToPx(sizeValue, layerCtx(parentSize, parentFont));
        if (r.status == CssLengthStatus::Valid)
          size = std::min(r.px, kChromeFontCapPx);
      }
      // Once font-size collapses to 0 the node has no text (STEP0F §2: skip font
      // build/measure/paint). Stop the cascade so we never construct a 0px QFont
      // for a later layer (Qt rejects non-positive pixel sizes). The collapsed
      // node paints nothing, so its resolved weight is irrelevant.
      if (sizeActive && size == 0.0) {
        parentSize = 0.0;
        break;
      }
      const QFont::Weight weight = resolveWeightOne(weightValue, parentWeight);
      parentSize = size;
      parentWeight = weight;
      // next layer's parent font = NODE font at this layer's resolved size+weight
      parentFont = flowchart::makeFlowLabelFont(nodeFamily, parentSize, parentWeight, s.fontStyle);
    }
    if (sizeActive) s.fontSizePx = parentSize;
    s.fontWeight = parentWeight;
  }

  // line-height (AFTER font-size so em uses the compounded px). bare <number> is a
  // unitless multiplier x fontSize (line-height:2 -> 2x fs); a length uses the
  // resolver; normal -> natural font height; negative/invalid -> theme default.
  // noFont: font-size:0 collapses the node (no text ink, STEP0F §2), so line-height
  // and spacing are irrelevant AND must skip their makeFlowLabelFont call — emBasis
  // is 0 there, so building a metric QFont would violate Qt's positive-pixel-size
  // contract. (line-height:0 WITHOUT font-size:0 still resolves normally: emBasis
  // is positive and the layout collapses via effLineHeight == 0.)
  const bool noFont = (s.fontSizePx == 0.0);
  const qreal emBasis = (s.fontSizePx >= 0.0) ? s.fontSizePx : themeFontSize;
  if (map.contains(QStringLiteral("line-height"))) {
    const QString v = map.value(QStringLiteral("line-height"));
    const QString tl = v.trimmed().toLower();
    if (tl == QLatin1String("normal")) {
      s.lineHeightNormal = true;
    } else if (!tl.isEmpty() && !noFont) {
      // A unitless <number> (incl. scientific: 1e1, 1e-1) is a multiplier of the
      // compounded font-size; anything else (2em, 20px, 1e1px, 120%) is a length.
      if (isUnitlessCssNumber(tl)) {
        bool ok = false;
        const double n = tl.toDouble(&ok);
        if (ok) s.lineHeightPx = (n < 0.0) ? -1.0 : n * emBasis;
      } else {
        const QFont mf = flowchart::makeFlowLabelFont(nodeFamily, emBasis, s.fontWeight, s.fontStyle);
        const CssLengthResult r = resolveCssLengthToPx(v, layerCtx(emBasis, mf));
        if (r.status == CssLengthStatus::Valid)
          s.lineHeightPx = (r.px < 0.0) ? -1.0 : r.px;
      }
    }
  }
  (void)themeLineHeightPx;  // theme default carried by the caller's sentinel (-1)

  // letter-spacing / word-spacing: em basis = compounded fontSize; negatives are
  // LIVE (CSS allows them; probe §3). bare number == px (resolveCssLengthToPx).
  const auto resolveSpacing = [&](const QString& key, qreal& out) {
    if (!map.contains(key)) return;
    const QString v = map.value(key);
    const QString tl = v.trimmed().toLower();
    if (tl.isEmpty() || tl == QLatin1String("normal")) {
      out = 0.0;
      return;
    }
    if (noFont) return;  // collapsed node: spacing irrelevant, no 0px QFont
    const QFont mf = flowchart::makeFlowLabelFont(nodeFamily, emBasis, s.fontWeight, s.fontStyle);
    const CssLengthResult r = resolveCssLengthToPx(v, layerCtx(emBasis, mf));
    if (r.status == CssLengthStatus::Valid) out = r.px;  // negatives kept
  };
  resolveSpacing(QStringLiteral("letter-spacing"), s.letterSpacingPx);
  resolveSpacing(QStringLiteral("word-spacing"), s.wordSpacingPx);

  // text-decoration: single keyword only (probe: space-separated combos are
  // dropped by the lexer -> none, so only one of these is ever set).
  if (map.contains(QStringLiteral("text-decoration"))) {
    const QString v = map.value(QStringLiteral("text-decoration")).trimmed().toLower();
    if (v == QLatin1String("underline")) s.underline = true;
    else if (v == QLatin1String("overline")) s.overline = true;
    else if (v == QLatin1String("line-through")) s.strikeOut = true;
  }
  if (map.contains(QStringLiteral("text-transform"))) {
    const QString v = map.value(QStringLiteral("text-transform")).trimmed().toLower();
    if (v == QLatin1String("uppercase")) s.transform = RequirementTextTransform::UpperCase;
    else if (v == QLatin1String("lowercase")) s.transform = RequirementTextTransform::LowerCase;
    else if (v == QLatin1String("capitalize")) s.transform = RequirementTextTransform::Capitalize;
  }
  if (map.contains(QStringLiteral("color"))) {
    const QString v = map.value(QStringLiteral("color"));
    if (muffin::mermaid::color::isParsableColor(v))
      s.color = muffin::mermaid::color::toQColor(v);
    // invalid/inherit -> stays invalid; paint uses theme textColor.
  }
  return s;
}

QString applyRequirementTextTransform(QString source, RequirementTextTransform tf) {
  if (tf == RequirementTextTransform::None || source.isEmpty()) return source;
  if (tf == RequirementTextTransform::UpperCase) return caseWithMathPreserved(source, true);
  if (tf == RequirementTextTransform::LowerCase) return caseWithMathPreserved(source, false);
  return capitalizeWithMathPreserved(source);
}

}  // namespace muffin::mermaid::requirement
