#pragma once

#include <QSizeF>
#include <QString>
#include <QStringView>

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
// Lenient: CSS requires consistent units on +/- and a dimensionless operand on
// `*`/`/`, but every operand is resolved to px first and combined numerically.
// Returns 0.0 for any expression it cannot fully parse (callers treat 0 as
// "unset"). `expression` is the calc() argument with vars already resolved.
qreal evalCalcPx(const QString& expression, qreal emPx, qreal rootPx, qreal containingPx);

}  // namespace muffin
