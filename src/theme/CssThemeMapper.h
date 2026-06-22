#pragma once

#include "theme/ThemeDefinition.h"

#include <QHash>

class QString;

namespace muffin {

class CssThemeSheet;  // defined in CssThemeParser.h

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
};

}  // namespace muffin
