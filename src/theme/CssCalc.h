#pragma once

#include <QSizeF>
#include <QString>
#include <QStringView>
#include <QMarginsF>
#include <array>

namespace muffin {

// Convert a CSS absolute length to px using the spec's fixed 96px/in ratio.
// Recognises px, pt, pc, in, cm, mm and Q (passed lower-cased as "q").
qreal absoluteCssLengthToPx(qreal value, const QString& unit, bool* recognised = nullptr);

// Resolved physical context for CSS length units that depend on font metrics or
// the rendering viewport. emPx is the SVG root font-size (themeVariables.fontSize);
// remPx is the <html> root font-size (mermaid leaves it at the browser default 16);
// exPx/chPx must come from QFontMetricsF of the actually-configured font (xHeight
// / advance of '0') — they are font-specific and must NOT be hardcoded. viewportPx
// is the CSS layout viewport; its default is a neutral placeholder, so a real
// caller passes its own (e.g. the Mermaid requirement layer passes mmdc's default
// raster profile, kept in THAT layer — not here — so this generic helper carries
// no Mermaid dependency).
struct CssLengthContext {
  qreal emPx = 16.0;
  qreal remPx = 16.0;
  qreal exPx = 8.0;
  qreal chPx = 8.0;
  QSizeF viewportPx{1.0, 1.0};  // neutral placeholder; callers override it
};

// Tri-state result so the caller applies its OWN fallback semantics: stroke-width
// maps Invalid -> its CSS initial 1px, but font/spacing properties fall back
// differently, so the resolver never bakes in 1px.
enum class CssLengthStatus { Missing, Invalid, Valid };
struct CssLengthResult {
  CssLengthStatus status = CssLengthStatus::Missing;
  qreal px = 0.0;  // meaningful only when status == Valid
};

// Computed <length-percentage>. Percentages survive computation until the
// containing block is known. In particular, Valid(0) is distinct from Invalid.
struct CssLengthPercentage {
  bool operator==(const CssLengthPercentage&) const = default;
  CssLengthStatus status = CssLengthStatus::Missing;
  qreal px = 0.0;
  qreal fraction = 0.0;
  bool hasPercentage = false;
  qreal used(qreal containingPx) const { return px + fraction * containingPx; }
};

CssLengthPercentage parseCssLengthPercentage(QStringView value, const CssLengthContext& context, bool allowUnitless = false);

struct CssBoxLengths {
  std::array<CssLengthPercentage, 4> sides;  // top, right, bottom, left
  QMarginsF used(QMarginsF fallback, qreal containingWidth, bool nonNegative = false) const {
    qreal values[]{fallback.top(), fallback.right(), fallback.bottom(), fallback.left()};
    for (int i = 0; i < 4; ++i) {
      if (sides[i].status != CssLengthStatus::Valid) continue;
      values[i] = sides[i].used(qMax<qreal>(0, containingWidth));
      if (nonNegative) values[i] = qMax<qreal>(0, values[i]);
    }
    return {values[3], values[0], values[1], values[2]};
  }
};

// Resolve a single CSS length value (e.g. "4", "1.5px", "2em", "1in", "1e2px",
// "4vw") to pixels against `ctx`. The magnitude follows the full CSS <number>
// grammar (optional sign, integer/decimal mantissa, optional exponent), units
// are ASCII case-insensitive, and absolute units reuse absoluteCssLengthToPx.
//   Missing  -> empty/whitespace value.
//   Invalid  -> non-numeric token, unknown unit, trailing junk after the unit,
//               or a non-finite magnitude (overflow such as 1e999).
//   Valid(px)-> a recognised length. px may be NEGATIVE: the resolver is
//               property-agnostic, so each caller applies its own sign policy
//               (stroke-width treats Valid<0 as its CSS initial; letter-spacing
//               and word-spacing accept negatives). 0 included — the caller
//               decides what it means (e.g. stroke-width:0 -> NoPen).
CssLengthResult resolveCssLengthToPx(QStringView value, const CssLengthContext& ctx);

// Evaluate a CSS `calc(<expr>)` expression to pixels. Supports `+ - * /`, nested
// parentheses, and per-term units (absolute units plus em/rem/%). `emPx` resolves em, `rootPx`
// resolves rem (defaults to emPx), `containingPx` resolves `%` (defaults to emPx).
// Compatibility wrapper over parseCssLengthPercentage with the same dimensional
// validation. Returns 0.0 for invalid expressions; new callers use the typed
// result to distinguish invalid from a valid zero. Variables must be resolved.
qreal evalCalcPx(const QString& expression, qreal emPx, qreal rootPx, qreal containingPx);

}  // namespace muffin
