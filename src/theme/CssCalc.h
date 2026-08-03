#pragma once

#include <QSizeF>
#include <QString>
#include <QStringView>

namespace muffin {

// Convert a CSS absolute length to px using the spec's fixed 96px/in ratio.
// Recognises px, pt, pc, in, cm, mm and Q (passed lower-cased as "q").
qreal absoluteCssLengthToPx(qreal value, const QString& unit, bool* recognised = nullptr);

// mermaid-cli's default Puppeteer viewport (src/index.js: --width 800, --height
// 600, --scale unset -> deviceScaleFactor 1). Muffin resolves viewport-relative
// CSS units (vw/vh/vmin/vmax) against this so its raster output matches the
// DEFAULT mmdc raster profile; it is NOT a claim of dynamic-SVG or custom
// --width/--height parity.
inline const QSizeF kMmdcDefaultCssViewport{800.0, 600.0};

// Resolved physical context for CSS length units that depend on font metrics or
// the rendering viewport. emPx is the SVG root font-size (themeVariables.fontSize);
// remPx is the <html> root font-size (mermaid leaves it at the browser default 16);
// exPx/chPx must come from QFontMetricsF of the actually-configured font (xHeight
// / advance of '0') — they are font-specific and must NOT be hardcoded. viewportPx
// is the CSS layout viewport (default = mmdc default raster profile).
struct CssLengthContext {
  qreal emPx = 16.0;
  qreal remPx = 16.0;
  qreal exPx = 8.0;
  qreal chPx = 8.0;
  QSizeF viewportPx = kMmdcDefaultCssViewport;
};

// Tri-state result so the caller applies its OWN fallback semantics: stroke-width
// maps Invalid -> its CSS initial 1px, but font/spacing properties fall back
// differently, so the resolver never bakes in 1px.
enum class CssLengthStatus { Missing, Invalid, Valid };
struct CssLengthResult {
  CssLengthStatus status = CssLengthStatus::Missing;
  qreal px = 0.0;  // meaningful only when status == Valid
};

// Resolve a single CSS length value (e.g. "4", "1.5px", "2em", "1in", "4vw") to
// pixels against `ctx`. Units are ASCII case-insensitive. Absolute units reuse
// absoluteCssLengthToPx.
//   Missing  -> empty/whitespace value.
//   Invalid  -> non-numeric token, unknown unit, trailing junk after the unit,
//               or a negative magnitude (CSS rejects negative lengths -> the
//               declaration is dropped, caller maps to the property's initial).
//   Valid(px)-> a recognised non-negative length (0 included; the caller decides
//               what 0 means, e.g. stroke-width:0 -> NoPen).
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
