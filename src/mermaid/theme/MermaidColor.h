#pragma once

// Native port of khroma (v2.1.0), the color library mermaid 11.16.0 bundles
// (vendored in dist/chunks/mermaid.esm/chunk-CHAKFXHA.mjs ~L47-785). Khroma is
// float-HSL with a lazy RGB<->HSL `Channels` model and signed-hue clamping
// (`h % 360`, JS signed modulo) — QColor's 8-bit quantization and [0,360) hue
// normalization cannot reproduce it, so this is a dedicated type, NOT a QColor
// wrapper. QColor is used only at the render boundary (toQColor, lossy 8-bit).
//
// The theme golden (tests/fixtures/mermaid/flowchart-theme.json) captures
// mermaid's exact resolved themeVariables strings; this port must reproduce
// them byte-for-byte. The format-selection stringify (hsl vs rgba vs hex vs
// original-string round-trip) is load-bearing — see stringify().

#include <QColor>
#include <QString>

#include <optional>

namespace muffin::mermaid::color {

// Mirrors khroma's `Channels` class: r/g/b and h/s/l are independently
// "populated" (std::optional = khroma's `undefined`); the Type flag records
// which family a setter last touched (drives stringify format selection).
// `a` is always present (defaults 1.0). `color` holds the original input
// string for the unchanged-round-trip stringify branch; `changed` is set by
// any setter.
//
// The channel getters (R/G/B/H/S/L) are NON-const: like khroma's getters they
// lazily populate missing channels via ensureHSL/ensureRGB (e.g. reading H() on
// an RGB-parsed color computes & caches h/s/l from r/g/b). This mutation is
// local to a parse->operate->stringify cycle.
struct MermaidColor {
  std::optional<double> r, g, b;   // 0-255, float
  std::optional<double> h, s, l;   // h: degrees (signed, NOT normalized); s,l: 0-100
  double a = 1.0;                   // 0-1
  enum class Type { All, RGB, HSL };
  Type type = Type::All;
  QString color;
  bool changed = false;

  // Lazy channel getters (mirror Channels getters). ensureHSL/ensureRGB
  // populate missing channels from the other family before reading.
  double R();
  double G();
  double B();
  double H();
  double S();
  double L();
  double A() const { return a; }

  // Setters (mirror Channels setters): set type + changed + the channel.
  void setR(double v) { type = Type::RGB; changed = true; r = v; }
  void setG(double v) { type = Type::RGB; changed = true; g = v; }
  void setB(double v) { type = Type::RGB; changed = true; b = v; }
  void setH(double v) { type = Type::HSL; changed = true; h = v; }
  void setS(double v) { type = Type::HSL; changed = true; s = v; }
  void setL(double v) { type = Type::HSL; changed = true; l = v; }
  void setA(double v) { changed = true; a = v; }

  void ensureHSL();
  void ensureRGB();
};

// Relative channel adjustments (mirrors khroma's `adjust` argument shape):
// only fields with a value are applied; a value of 0 is skipped (khroma's
// falsy-skip, `if (!channels[c]) continue;`).
struct ChannelAdjust {
  std::optional<double> h, s, l, r, g, b;
};

// --- parse / stringify (khroma Color.parse / Color.stringify) ---
MermaidColor parse(const QString& color);
QString stringify(const MermaidColor& c);

// --- khroma methods (all operate on color strings, returning the stringified result) ---
// `change`: absolute set of channels (clamped). `adjust`: relative add to current.
QString change(const QString& color, const ChannelAdjust& ch);
QString adjust(const QString& color, const ChannelAdjust& ch);
QString adjustChannel(const QString& color, QChar channel, double amount);
QString lighten(const QString& color, double amount);        // adjustChannel "l" +amount
QString darken(const QString& color, double amount);         // adjustChannel "l" -amount
QString transparentize(const QString& color, double amount); // adjustChannel "a" -amount
QString mix(const QString& color1, const QString& color2, double weight = 50.0);
QString invert(const QString& color, double weight = 100.0);
bool isDark(const QString& color);
QString mkBorder(const QString& col, bool darkMode);  // darkMode always false for built-in themes
// `channel(color, 'r'|'g'|'b'|'h'|'s'|'l')`: langRound(parse(c).<channel>()).
double channel(const QString& color, QChar which);

// rgba(r,g,b,a) constructs a fresh color; rgba(color, alpha) = change({a}).
QString rgba(double r, double g, double b, double a);
QString rgba(const QString& color, double alpha);

// Render boundary: lossy 8-bit QColor for the painter. NEVER used for golden
// comparison (which is exact-string against stringify()).
QColor toQColor(const QString& color);

// True iff `color` is a recognized CSS color (hex / rgb() / hsl() / named
// keyword). Mirrors what parse() accepts before falling back to the opaque
// original-string round-trip. Used by diagram style resolution to detect
// invalid inline values that mermaid's SVG would drop (so Muffin can apply the
// upstream fallback: invalid fill → inherited foreground, invalid stroke → none).
bool isParsableColor(const QString& color);

// SVG <paint> resolution at the render boundary. Categorizes a themeVariable /
// source-entry value (none / currentColor / inherit / initial / a real color /
// garbage) and resolves it PER PROPERTY the way mermaid's SVG CSS engine does,
// probed vs 11.16.0 (scripts/probe_mermaid_paint_resolution.mjs):
//   empty/none  -> no paint (NoBrush / NoPen / hide text)
//   currentColor-> black (mermaid's currentColor resolution)
//   inherit     -> Fill/Text: the inherited color; Stroke: none
//   initial     -> Fill/Text: black (SVG fill initial); Stroke: none (SVG stroke initial)
//   color       -> the color
//   garbage     -> Fill/Text: the inherited color; Stroke: none
// `inherited` is the DOM-inherited color for the element (the theme textColor).
// The caller applies the result: Fill none->NoBrush, Stroke none->NoPen (or when
// the width is <=0), Text none->skip the drawText.
enum class SvgPaintKind { Fill, Stroke, Text };
struct SvgPaint { bool none = false; QColor color; };
SvgPaint resolveSvgPaint(const QString& value, SvgPaintKind kind, const QColor& inherited);

}  // namespace muffin::mermaid::color
