#pragma once

#include "theme/ThemeDefinition.h"

#include <QHash>

class QString;

namespace muffin {

class CssThemeSheet;  // defined in CssThemeParser.h
class CssComputedStyleEngine;  // defined in CssComputedStyleEngine.h
class NodeCssElementBuilder;  // defined in NodeCssElement.h
class MarkdownNode;

// Construct a QColor from a CSS colour literal, correcting Qt's hex-alpha
// byte order. CSS specifies alpha LAST (#RRGGBBAA / #RGBA); QColor reads 8- and
// 4-digit hex with alpha FIRST (#AARRGGBB / #ARGB). Without this a theme value
// like "#7aeaf018" (CSS: pale cyan @ 9% alpha) is read by Qt as a=7a,r=ea,g=f0,
// b=18 — a saturated yellow-green. Every other form (6/3-digit hex, rgb()/hsl(),
// named colours) is passed straight to QColor. This is the single CSS→QColor
// boundary; extractColor(), varColor() and JSON-theme parseColor() all route
// through it so the fix applies theme-wide.
QColor cssColor(const QString& literal);

// Translates a parsed CSS-theme sheet into a Muffin ThemeDefinition. This
// is the "external-CSS compatibility" contract: a fixed table maps CSS selectors +
// :root variables to Muffin semantic tokens (ThemeColors / ThemeTypography).
// Anything the table doesn't cover (gradients, ::after decorations, animations,
// CSS counters, masks, …) is deliberately ignored — Muffin paints directly, it
// does not run a CSS engine.
//
// The mapping is intentionally lenient: a stock CSS theme that only sets
// element colours + fonts produces a usable theme; Muffin-specific knobs with no
// CSS equivalent (chrome palette, serif body, explicit dark flag, label) ride
// on `--muffin-*` custom properties layered on top of the standard CSS.
class CssThemeMapper {
public:
  // Parse `cssText` (already read from cssPath by the caller) and translate it.
  // `baseDir` is used only to resolve relative @import targets. `id` is the
  // machine name (usually the file stem); it overrides any name the CSS declares.
  static ThemeDefinition fromCss(const QString& cssText, const QString& id, const QString& baseDir);
  // Translate an already-parsed sheet. Split from fromCss so the caller can
  // inspect the sheet (e.g. register its @font-face fonts) between parse and
  // translation. `id` is the machine name; it overrides any name the CSS declares.
  static ThemeDefinition fromSheet(const CssThemeSheet& sheet, const QString& id);
  // Parse a CSS `linear-gradient(...)` / `radial-gradient(...)` value into a
  // GradientSpec (rect-independent data; the painter builds a QGradient per
  // target rect). Exposed for unit testing. var()/color-mix()/rgb()/hex stops
  // resolve through the same path as every other theme colour.
  static GradientSpec parseGradient(const QString& raw, const QHash<QString, QString>& vars);
  // Resolve a CSS colour value (var/color-mix/rgb/hex/named) → QColor. Exposed so
  // the keyframe sampler can interpolate stop colours at paint time.
  static QColor resolveColor(const QString& value, const QHash<QString, QString>& vars);
  // Resolve a CSS length (px/pt/em/rem/%) → pixels (emPx defaults to 16).
  static qreal resolveLengthPx(const QString& value, const QHash<QString, QString>& vars);
  // Box-relative variant: a `%` resolves against `containingPx` (the host box's
  // dimension) instead of 1em. For paint-time resolution of pseudo width/height
  // where the % is relative to the rendered host (e.g. `h3::before { height: 61% }`).
  static qreal resolveLengthPx(const QString& value, const QHash<QString, QString>& vars,
                               qreal emPx, qreal containingPx);
  // Real-tree computed style for a node: build a CssElement view of `node` (its
  // live ancestors/siblings/position), run the cascade through `engine`, and map
  // the result to a ThemeElementStyle the same way the load-time precompute does.
  // Used by the structural-selector layout path. `bodyPx` is the em/rem basis.
  // `builder` is the caller-owned (persistent) CSS element tree, so the sibling chain is built once
  // per rebuild instead of per node (the latter was O(n²) on flat block lists).
  static ThemeElementStyle elementStyleForNode(NodeCssElementBuilder& builder, const CssComputedStyleEngine& engine,
                                               const MarkdownNode& node, const QString& key, qreal bodyPx);
};

}  // namespace muffin
