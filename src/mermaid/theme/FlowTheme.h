#pragma once

// Native port of mermaid 11.16.0's theme system (the 11 registered themes in
// dist/chunks/mermaid.esm/chunk-CHAKFXHA.mjs). Each theme is a `Theme` class
// with a constructor (raw field literals, some derived via khroma) + an
// `updateColors()` derivation + a `calculate(overrides)` two-pass driver.
//
// Scope: the flowchart renderer (chunk-YI7H2ERT.mjs getStyles) reads ~15
// themeVariables fields and all cScale/cScaleInv/cScalePeer/cScaleLabel palettes
// are ported for all 11 registered themes.
//
// Critical fidelity note: the `default` theme's constructor calls
// `this.updateColors()` (chunk line 1641), and `getThemeVariables` then calls
// `calculate()` which calls updateColors AGAIN. default's cScale darken loop
// is UNCONDITIONAL (`cScale[i] = darken(cScale[i], 10)`, not `||=`), so the
// double-run double-darkens cScale. Only `default` runs updateColors twice;
// the other 10 themes run it once (calculate only). resolveFlowTheme replicates
// this per-theme single-vs-double distinction.

#include <QString>
#include <QStringList>

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
  bool useGradient = true;
  QString gradientStart;
  QString gradientStop;
  QString shadowColor = QStringLiteral("#000000");
  qreal shadowOpacity = 0.25;
  qreal shadowOffsetX = 0.0;
  qreal shadowOffsetY = 1.0;
  int themeColorLimit = 12;

  // Complete 12-color theme palette.
  QString cScale[12];
  QString cScaleInv[12];
  QString cScalePeer[12];
  QString cScaleLabel[12];

  // requirementDiagram / er / rect `colorIndex` palette (chunk-CHAKFXHA.mjs:
  // only redux-color defines both; redux-dark-color defines borderColorArray
  // only with bkgColorArray = []). When borderColorArray is non-empty, the
  // requirement scene cycles node stroke/fill by `colorIndex % size`. Theme-
  // internal: populated by the constructors only, NOT exposed via get()/set()
  // (string-keyed override of an array is out of scope; the theme golden only
  // checks a fixed scalar/cScale field list).
  QStringList borderColorArray;
  QStringList bkgColorArray;

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
