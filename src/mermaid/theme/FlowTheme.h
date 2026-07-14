#pragma once

// Native port of mermaid 11.16.0's theme system (the 11 registered themes in
// dist/chunks/mermaid.esm/chunk-CHAKFXHA.mjs). Each theme is a `Theme` class
// with a constructor (raw field literals, some derived via khroma) + an
// `updateColors()` derivation + a `calculate(overrides)` two-pass driver.
//
// Scope: the flowchart renderer (chunk-YI7H2ERT.mjs getStyles) reads ~15
// themeVariables fields. Those are ported for ALL 11 themes. The cScale color
// palette (used by `classDef foo color:N`) is ported fully for `default` and
// `dark` (the F3 pixel-golden targets); the other 9 themes' cScale variants
// (neo's l:150, redux's mainBkg, redux-color's literals) are deferred — cScale
// only affects rare `color:N` classDefs, not normal flowchart rendering.
//
// Critical fidelity note: the `default` theme's constructor calls
// `this.updateColors()` (chunk line 1641), and `getThemeVariables` then calls
// `calculate()` which calls updateColors AGAIN. default's cScale darken loop
// is UNCONDITIONAL (`cScale[i] = darken(cScale[i], 10)`, not `||=`), so the
// double-run double-darkens cScale. Only `default` runs updateColors twice;
// the other 10 themes run it once (calculate only). resolveFlowTheme replicates
// this per-theme single-vs-double distinction.

#include <QString>

#include <QHash>
#include <optional>

namespace muffin::mermaid::flowtheme {

enum class FlowThemeId {
  Base, Dark, Default, Forest, Neutral, Neo, NeoDark,
  Redux, ReduxDark, ReduxColor, ReduxDarkColor
};

// Parse a mermaid theme name ("default", "neo-dark", ...) → FlowThemeId.
// Unknown names map to Default (mermaid's default).
FlowThemeId parseThemeId(const QString& name);

// The flowchart-relevant themeVariables subset. Colors are stored as the exact
// khroma-format strings (hsl(...)/#hex/rgba(...)) so the golden compares
// byte-for-byte; the painter converts via color::toQColor at render time.
struct FlowThemeVariables {
  // constructor-set raw inputs
  QString background;
  QString primaryColor;
  QString secondaryColor;
  QString tertiaryColor;
  QString mainBkg;
  QString secondBkg;
  QString lineColor;
  QString border1;
  QString border2;
  QString arrowheadColor;
  QString fontFamily;
  QString fontSize;
  QString labelBackground;
  QString textColor;
  QString titleColor;
  QString edgeLabelBackground;
  QString clusterBkg;
  QString clusterBorder;
  QString primaryBorderColor;
  QString primaryTextColor;
  QString nodeTextColor;
  QString nodeBkg;
  QString nodeBorder;
  QString defaultLinkColor;
  QString mainContrastColor;  // dark theme only
  QString contrast;           // neutral theme only
  QString text;               // neutral theme only
  qreal strokeWidth = 1.0;
  int themeColorLimit = 12;

  // cScale palette (default + dark ported fully; others left empty).
  QString cScale[12];
  QString cScaleInv[12];
  QString cScalePeer[12];
  QString cScaleLabel[12];

  // Generic field access by themeVariable name (for override application +
  // golden comparison). Returns empty for unknown/unset fields.
  QString get(const QString& key) const;
  void set(const QString& key, const QString& value);
};

// Resolve a theme: applyRawConstructor → (default only: updateColors) →
// calculate(overrides) [sentinel-clear for default/forest → overrides →
// updateColors → overrides]. Matches mermaid's getThemeVariables(name, tv).
FlowThemeVariables resolveFlowTheme(FlowThemeId id);
FlowThemeVariables resolveFlowTheme(FlowThemeId id, const QHash<QString, QString>& overrides);

}  // namespace muffin::mermaid::flowtheme
