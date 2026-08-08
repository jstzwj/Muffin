#include "mermaid/theme/FlowTheme.h"

#include "mermaid/theme/MermaidColor.h"

#include <QString>

#include <algorithm>

namespace muffin::mermaid::flowtheme {
namespace {

using color::adjust;
using color::darken;
using color::invert;
using color::lighten;
using color::mkBorder;
using color::rgba;

// `this.x = this.x || value` — keep existing non-empty value (JS truthy string).
void assignIfEmpty(QString& field, const QString& value) {
  if (field.isEmpty()) field = value;
}

// The number of cScale entries to derive. Upstream iterates
// `for (i=0; i<this.THEME_COLOR_LIMIT; i++)` over a DYNAMIC array; the native
// port stores cScale/cScaleInv/cScalePeer/cScaleLabel as FIXED 13-element arrays
// (cScale0..cScale12), so clamp the user-controlled THEME_COLOR_LIMIT to [0,13]
// for every fixed-array access. For the default TCL=12 this is a no-op; for
// TCL=13 it lets dark populate cScaleInv/Peer/Label[12] and pie12 (= cScale12);
// for TCL>13 it prevents out-of-bounds writes. Negative TCL -> 0. (pie is a
// separate 12-slot array and is capped independently in populatePieFromCScale.)
int cScaleCount(const FlowThemeVariables& t) {
  return std::clamp(t.themeColorLimit, 0, 13);
}

// --- per-theme raw constructors (the `this.X = literal` lines) ---

void applyBase(FlowThemeVariables& t) {
  t.background = QStringLiteral("#f4f4f4");
  t.primaryColor = QStringLiteral("#fff4dd");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
}

void applyDark(FlowThemeVariables& t) {
  t.background = QStringLiteral("#333");
  t.primaryColor = QStringLiteral("#1f2020");
  t.secondaryColor = lighten(t.primaryColor, 16);
  t.tertiaryColor = adjust(t.primaryColor, {.h = -160.0});
  t.primaryBorderColor = invert(t.background);
  t.primaryTextColor = invert(t.primaryColor);
  t.mainBkg = QStringLiteral("#1f2020");
  t.secondBkg = QStringLiteral("calculated");
  t.mainContrastColor = QStringLiteral("lightgrey");
  t.lineColor = QStringLiteral("calculated");
  t.border1 = QStringLiteral("#ccc");
  t.border2 = rgba(255, 255, 255, 0.25);
  t.arrowheadColor = QStringLiteral("calculated");
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
  t.labelBackground = QStringLiteral("#181818");
  t.textColor = QStringLiteral("#ccc");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.nodeBkg = QStringLiteral("calculated");
  t.nodeBorder = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("calculated");
  t.clusterBorder = QStringLiteral("calculated");
  t.defaultLinkColor = QStringLiteral("calculated");
  t.titleColor = QStringLiteral("#F9FFFE");
  t.edgeLabelBackground = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("#302F3D");  // constructor overrides the "calculated" above
}

void applyDefault(FlowThemeVariables& t) {
  t.useGradient = false;
  t.background = QStringLiteral("#f4f4f4");
  t.primaryColor = QStringLiteral("#ECECFF");
  t.secondaryColor = adjust(t.primaryColor, {.h = 120.0});
  t.secondaryColor = QStringLiteral("#ffffde");  // constructor overrides the adjust above
  t.tertiaryColor = adjust(t.primaryColor, {.h = -160.0});
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.primaryTextColor = invert(t.primaryColor);
  t.lineColor = invert(t.background);
  t.textColor = invert(t.background);
  t.background = QStringLiteral("white");
  t.mainBkg = QStringLiteral("#ECECFF");
  t.secondBkg = QStringLiteral("#ffffde");
  t.lineColor = QStringLiteral("#333333");
  t.border1 = QStringLiteral("#9370DB");
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.border2 = QStringLiteral("#aaaa33");
  t.arrowheadColor = QStringLiteral("#333333");
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
  t.labelBackground = QStringLiteral("rgba(232,232,232, 0.8)");
  t.textColor = QStringLiteral("#333");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.nodeBkg = QStringLiteral("calculated");
  t.nodeBorder = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("calculated");
  t.clusterBorder = QStringLiteral("calculated");
  t.defaultLinkColor = QStringLiteral("calculated");
  t.titleColor = QStringLiteral("calculated");
  t.edgeLabelBackground = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("#FBFBFF");
}

void applyForest(FlowThemeVariables& t) {
  t.background = QStringLiteral("#f4f4f4");
  t.primaryColor = QStringLiteral("#cde498");
  t.secondaryColor = QStringLiteral("#cdffb2");
  t.background = QStringLiteral("white");
  t.mainBkg = QStringLiteral("#cde498");
  t.secondBkg = QStringLiteral("#cdffb2");
  t.lineColor = QStringLiteral("green");
  t.border1 = QStringLiteral("#13540c");
  t.border2 = QStringLiteral("#6eaa49");
  t.arrowheadColor = QStringLiteral("green");
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
  t.tertiaryColor = lighten(QStringLiteral("#cde498"), 10);
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.primaryTextColor = invert(t.primaryColor);
  t.lineColor = invert(t.background);
  t.textColor = invert(t.background);
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.nodeBkg = QStringLiteral("calculated");
  t.nodeBorder = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("calculated");
  t.clusterBorder = QStringLiteral("calculated");
  t.defaultLinkColor = QStringLiteral("calculated");
  t.titleColor = QStringLiteral("#333");
  t.edgeLabelBackground = QStringLiteral("#e8e8e8");
}

void applyNeutral(FlowThemeVariables& t) {
  t.primaryColor = QStringLiteral("#eee");
  t.contrast = QStringLiteral("#707070");
  t.secondaryColor = lighten(t.contrast, 55);
  t.background = QStringLiteral("#ffffff");
  t.tertiaryColor = adjust(t.primaryColor, {.h = -160.0});
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.primaryTextColor = invert(t.primaryColor);
  t.lineColor = invert(t.background);
  t.textColor = invert(t.background);
  t.mainBkg = QStringLiteral("#eee");
  t.secondBkg = QStringLiteral("calculated");
  t.lineColor = QStringLiteral("#666");
  t.border1 = QStringLiteral("#999");
  t.border2 = QStringLiteral("calculated");
  t.arrowheadColor = QStringLiteral("#333333");
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.nodeBkg = QStringLiteral("calculated");
  t.nodeBorder = QStringLiteral("calculated");
  t.clusterBkg = QStringLiteral("calculated");
  t.clusterBorder = QStringLiteral("calculated");
  t.defaultLinkColor = QStringLiteral("calculated");
  t.titleColor = QStringLiteral("calculated");
  t.edgeLabelBackground = QStringLiteral("white");
  t.text = QStringLiteral("#333");
}

void applyNeo(FlowThemeVariables& t) {
  t.background = QStringLiteral("#ffffff");
  t.primaryColor = QStringLiteral("#cccccc");
  t.mainBkg = QStringLiteral("#ffffff");
  t.themeColorLimit = 12;
  t.strokeWidth = 2;
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.fontFamily = QStringLiteral("arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.nodeBorder = QStringLiteral("#000000");
  t.tertiaryColor = QStringLiteral("#ffffff");
  t.useGradient = true;
  t.gradientStart = QStringLiteral("#0042eb");
  t.gradientStop = QStringLiteral("#eb0042");
}

void applyNeoDark(FlowThemeVariables& t) {
  t.background = QStringLiteral("#333");
  t.primaryColor = QStringLiteral("#1f2020");
  t.secondaryColor = lighten(t.primaryColor, 16);
  t.tertiaryColor = adjust(t.primaryColor, {.h = -160.0});
  t.primaryBorderColor = invert(t.background);
  t.primaryTextColor = invert(t.primaryColor);
  t.mainBkg = QStringLiteral("#2a2020");
  t.secondBkg = QStringLiteral("calculated");
  t.border1 = QStringLiteral("#ccc");
  t.border2 = rgba(255, 255, 255, 0.25);
  t.arrowheadColor = invert(t.background);
  t.fontFamily = QStringLiteral("arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.labelBackground = QStringLiteral("#181818");
  t.textColor = QStringLiteral("#ccc");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.useGradient = true;
  t.gradientStart = QStringLiteral("#0042eb");
  t.gradientStop = QStringLiteral("#eb0042");
  t.shadowColor = QStringLiteral("#b9b9b9");
  t.shadowOpacity = 0.2;
  t.shadowOffsetX = 1.0;
  t.shadowOffsetY = 2.0;
}

void applyRedux(FlowThemeVariables& t) {
  t.useGradient = false;
  t.background = QStringLiteral("#ffffff");
  t.primaryColor = QStringLiteral("#cccccc");
  t.mainBkg = QStringLiteral("#ffffff");
  t.themeColorLimit = 12;
  t.strokeWidth = 2;
  t.primaryBorderColor = mkBorder(QStringLiteral("#28253D"), false);
  t.fontFamily = QStringLiteral("\"Recursive Variable\", arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.nodeBorder = QStringLiteral("#28253D");
  t.tertiaryColor = QStringLiteral("#ffffff");
  t.clusterBkg = QStringLiteral("#F9F9FB");
  t.clusterBorder = QStringLiteral("#BDBCCC");
}

void applyReduxDark(FlowThemeVariables& t) {
  t.useGradient = false;
  t.background = QStringLiteral("#333");
  t.primaryColor = QStringLiteral("#1f2020");
  t.secondaryColor = lighten(t.primaryColor, 16);
  t.tertiaryColor = adjust(t.primaryColor, {.h = -160.0});
  t.primaryBorderColor = invert(t.background);
  t.primaryTextColor = invert(t.primaryColor);
  t.mainBkg = QStringLiteral("#111113");
  t.secondBkg = QStringLiteral("calculated");
  t.border1 = QStringLiteral("#ccc");
  t.border2 = rgba(255, 255, 255, 0.25);
  t.arrowheadColor = invert(t.background);
  t.fontFamily = QStringLiteral("\"Recursive Variable\", arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.labelBackground = QStringLiteral("#111113");
  t.textColor = QStringLiteral("#ccc");
  t.themeColorLimit = 12;
  t.strokeWidth = 2;
  t.nodeBorder = QStringLiteral("#FFFFFF");
  t.clusterBkg = QStringLiteral("#1E1A2E");
  t.clusterBorder = QStringLiteral("#BDBCCC");
}

// The 12-entry Tailwind *-400 border palette shared by redux-color and
// redux-dark-color (chunk-CHAKFXHA.mjs:3936-3961). Both *-color variants define
// the identical borderColorArray; only redux-color adds the matching *-50 bkg
// palette (applyReduxColor). requirementDiagram/er/rect cycle node stroke by
// `colorIndex % borderColorArray.size()` when this is non-empty.
void applyReduxColorBorderPalette(FlowThemeVariables& t) {
  t.borderColorArray = {
      QStringLiteral("#E879F9"), QStringLiteral("#2DD4BF"), QStringLiteral("#FB923C"),
      QStringLiteral("#22D3EE"), QStringLiteral("#4ADE80"), QStringLiteral("#A78BFA"),
      QStringLiteral("#F87171"), QStringLiteral("#FACC15"), QStringLiteral("#818CF8"),
      QStringLiteral("#A3E635"),  // upstream literal is "#A3E635 " (trailing space; benign)
      QStringLiteral("#38BDF8"), QStringLiteral("#FB7185")};
}

void applyReduxColor(FlowThemeVariables& t) {
  t.useGradient = false;
  t.background = QStringLiteral("#ffffff");
  t.primaryColor = QStringLiteral("#cccccc");
  t.mainBkg = QStringLiteral("#ffffff");
  t.strokeWidth = 2;
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.fontFamily = QStringLiteral("\"Recursive Variable\", arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.nodeBorder = QStringLiteral("#28253D");
  t.tertiaryColor = QStringLiteral("#ffffff");
  // colorIndex palette (chunk-CHAKFXHA.mjs:3936-3987): 12 Tailwind *-400 border
  // + 12 Tailwind *-50 bkg. requirement nodes cycle both by insertion order.
  applyReduxColorBorderPalette(t);
  t.bkgColorArray = {
      QStringLiteral("#FDF4FF"), QStringLiteral("#F0FDFA"), QStringLiteral("#FFF7ED"),
      QStringLiteral("#ECFEFF"), QStringLiteral("#F0FDF4"), QStringLiteral("#F5F3FF"),
      QStringLiteral("#FEF2F2"), QStringLiteral("#FEFCE8"), QStringLiteral("#EEF2FF"),
      QStringLiteral("#F7FEE7"), QStringLiteral("#F0F9FF"), QStringLiteral("#FFF1F2")};
}

void applyReduxDarkColor(FlowThemeVariables& t) {
  // Mirrors redux-dark's constructor (the *-color variant adds the border palette
  // for colorIndex cycling). bkgColorArray is intentionally LEFT EMPTY upstream
  // (chunk-CHAKFXHA.mjs:4327): nodes cycle stroke only; fill falls back to mainBkg.
  applyReduxDark(t);
  applyReduxColorBorderPalette(t);
}

void applyRawConstructor(FlowThemeId id, FlowThemeVariables& t) {
  switch (id) {
    case FlowThemeId::Base: applyBase(t); break;
    case FlowThemeId::Dark: applyDark(t); break;
    case FlowThemeId::Default: applyDefault(t); break;
    case FlowThemeId::Forest: applyForest(t); break;
    case FlowThemeId::Neutral: applyNeutral(t); break;
    case FlowThemeId::Neo: applyNeo(t); break;
    case FlowThemeId::NeoDark: applyNeoDark(t); break;
    case FlowThemeId::Redux: applyRedux(t); break;
    case FlowThemeId::ReduxDark: applyReduxDark(t); break;
    case FlowThemeId::ReduxColor: applyReduxColor(t); break;
    case FlowThemeId::ReduxDarkColor: applyReduxDarkColor(t); break;
  }
}

// --- updateColors variants ---
// Forward declarations (pie/quadrant derivation).
void populatePieFamilyA(FlowThemeVariables& t, const QString& primary,
                        const QString& secondary, const QString& tertiary);
void populatePieDefault(FlowThemeVariables& t);
void populatePieForest(FlowThemeVariables& t);
void populatePieFromCScale(FlowThemeVariables& t, bool pie12FromCScale0);
void populateQuadrant(FlowThemeVariables& t, const QString& primary);

// Family A (base/neo/neo-dark/redux/redux-dark/redux-color/redux-dark-color):
// the shared `||`-guarded derivation (darkMode always false for built-in
// themes). primaryTextColorDefault is the non-darkMode fallback ("#333" etc.).
void updateColorsFamilyA(FlowThemeVariables& t, const QString& primaryTextColorDefault,
                         bool nodeBorderFromBorder1, bool lightPalette = false) {
  assignIfEmpty(t.primaryTextColor, primaryTextColorDefault);
  assignIfEmpty(t.secondaryColor, adjust(t.primaryColor, {.h = -120.0}));
  assignIfEmpty(t.tertiaryColor, adjust(t.primaryColor, {.h = 180.0, .l = 5.0}));
  const QString tertiaryBorderColor = mkBorder(t.tertiaryColor, false);
  assignIfEmpty(t.primaryBorderColor, mkBorder(t.primaryColor, false));
  const QString tertiaryTextColor = invert(t.tertiaryColor);
  assignIfEmpty(t.lineColor, invert(t.background));
  assignIfEmpty(t.arrowheadColor, invert(t.background));
  assignIfEmpty(t.textColor, t.primaryTextColor);
  assignIfEmpty(t.border2, tertiaryBorderColor);
  assignIfEmpty(t.nodeBkg, t.primaryColor);
  assignIfEmpty(t.mainBkg, t.primaryColor);
  // neo-dark/redux-dark derive nodeBorder from border1; base/neo/redux from
  // primaryBorderColor. Themes that pre-set nodeBorder in the constructor are
  // unaffected (assignIfEmpty keeps the constructor value).
  assignIfEmpty(t.nodeBorder, nodeBorderFromBorder1 ? t.border1 : t.primaryBorderColor);
  assignIfEmpty(t.clusterBkg, t.tertiaryColor);
  assignIfEmpty(t.clusterBorder, tertiaryBorderColor);
  assignIfEmpty(t.defaultLinkColor, t.lineColor);
  assignIfEmpty(t.titleColor, tertiaryTextColor);
  assignIfEmpty(t.edgeLabelBackground, t.secondaryColor);  // darkMode false
  assignIfEmpty(t.nodeTextColor, t.primaryTextColor);
  // The light non-base themes (neo/redux/redux-color) bind a LOCAL primaryColor
  // constant "#ECECFE" in upstream's updateColors — distinct from
  // themeVariables.primaryColor — and derive BOTH the pie and quadrant palettes
  // from that constant family (const primaryColor="#ECECFE", secondaryColor=
  // "#E9E9F1", tertiaryColor=adjust(primaryColor,{h:180,l:5}); chunk-CHAKFXHA.mjs
  // L2737). base + the dark variants have no such local and use themeVariables
  // primaryColor/secondaryColor/tertiaryColor directly. Quadrant text fills
  // always derive from primaryTextColor (not the local primary).
  const QString primary = lightPalette ? QStringLiteral("#ECECFE") : t.primaryColor;
  if (lightPalette) {
    populatePieFamilyA(t, primary, QStringLiteral("#E9E9F1"), adjust(primary, {.h = 180.0, .l = 5.0}));
  } else {
    populatePieFamilyA(t, primary, t.secondaryColor, t.tertiaryColor);
  }
  populateQuadrant(t, primary);
}

void finishCScale(FlowThemeVariables& t, bool darkMode,
                  bool darkColorLabels = false) {
  for (int i = 0; i < cScaleCount(t); ++i) {
    // An empty cScale[i] (e.g. cScale12 for every theme but dark) has no color to
    // invert/darken/lighten -- skip those color ops. cScaleLabel still takes its
    // non-cScale fallback (primaryTextColor) unless darkColorLabels actually
    // darkens a (non-empty) cScale[i].
    if (!t.cScale[i].isEmpty()) {
      assignIfEmpty(t.cScaleInv[i], invert(t.cScale[i]));
      assignIfEmpty(t.cScalePeer[i], darkMode ? lighten(t.cScale[i], 10)
                                              : darken(t.cScale[i], 10));
    }
    assignIfEmpty(t.cScaleLabel[i],
                  (darkColorLabels && !t.cScale[i].isEmpty()) ? darken(t.cScale[i], 75)
                                                              : t.primaryTextColor);
  }
}

// Family-A pie palette (shared by base/neo/redux/redux-color/redux-dark/
// redux-dark-color). pie1-3 = primary/secondary/tertiary; pie4-6 = adjust(base,
// {l:-10}); pie7-12 = adjust(primary, {h, l}).
void populatePieFamilyA(FlowThemeVariables& t, const QString& primary,
                        const QString& secondary, const QString& tertiary) {
  assignIfEmpty(t.pie[0], primary);
  assignIfEmpty(t.pie[1], secondary);
  assignIfEmpty(t.pie[2], tertiary);
  assignIfEmpty(t.pie[3], adjust(primary, {.l = -10.0}));
  assignIfEmpty(t.pie[4], adjust(secondary, {.l = -10.0}));
  assignIfEmpty(t.pie[5], adjust(tertiary, {.l = -10.0}));
  assignIfEmpty(t.pie[6], adjust(primary, {.h = 60.0, .l = -10.0}));
  assignIfEmpty(t.pie[7], adjust(primary, {.h = -60.0, .l = -10.0}));
  assignIfEmpty(t.pie[8], adjust(primary, {.h = 120.0}));
  assignIfEmpty(t.pie[9], adjust(primary, {.h = 60.0, .l = -20.0}));
  assignIfEmpty(t.pie[10], adjust(primary, {.h = -60.0, .l = -20.0}));
  assignIfEmpty(t.pie[11], adjust(primary, {.h = 120.0, .l = -10.0}));
}

// Default theme's OWN pie formula (different from Family-A). From the upstream
// source (chunk-WYO6CB5R.mjs ~L53671): pie3=adjust(tertiary,{l:-40}),
// pie5=adjust(secondary,{l:-30}), pie6=adjust(tertiary,{l:-20}),
// pie7=adjust(primary,{h:60,l:-20}), pie8=adjust(primary,{h:-60,l:-40}),
// pie9-12 = adjust(primary,{h,l}) with the Default-specific {h,l} pairs.
void populatePieDefault(FlowThemeVariables& t) {
  assignIfEmpty(t.pie[0], t.primaryColor);
  assignIfEmpty(t.pie[1], t.secondaryColor);
  assignIfEmpty(t.pie[2], adjust(t.tertiaryColor, {.l = -40.0}));
  assignIfEmpty(t.pie[3], adjust(t.primaryColor, {.l = -10.0}));
  assignIfEmpty(t.pie[4], adjust(t.secondaryColor, {.l = -30.0}));
  assignIfEmpty(t.pie[5], adjust(t.tertiaryColor, {.l = -20.0}));
  assignIfEmpty(t.pie[6], adjust(t.primaryColor, {.h = 60.0, .l = -20.0}));
  assignIfEmpty(t.pie[7], adjust(t.primaryColor, {.h = -60.0, .l = -40.0}));
  assignIfEmpty(t.pie[8], adjust(t.primaryColor, {.h = 120.0, .l = -40.0}));
  assignIfEmpty(t.pie[9], adjust(t.primaryColor, {.h = 60.0, .l = -40.0}));
  assignIfEmpty(t.pie[10], adjust(t.primaryColor, {.h = -90.0, .l = -40.0}));
  assignIfEmpty(t.pie[11], adjust(t.primaryColor, {.h = 120.0, .l = -30.0}));
}

// Forest theme's OWN pie formula (chunk-WYO6CB5R.mjs ~L1363, the `adjust5`
// block). pie1-3 = primary/secondary/tertiary; pie4/5 = adjust(*,{l:-30});
// pie6 = adjust(tertiary,{h:40,l:-40}); pie7-12 = adjust(primary,{h,l}).
void populatePieForest(FlowThemeVariables& t) {
  assignIfEmpty(t.pie[0], t.primaryColor);
  assignIfEmpty(t.pie[1], t.secondaryColor);
  assignIfEmpty(t.pie[2], t.tertiaryColor);
  assignIfEmpty(t.pie[3], adjust(t.primaryColor, {.l = -30.0}));
  assignIfEmpty(t.pie[4], adjust(t.secondaryColor, {.l = -30.0}));
  assignIfEmpty(t.pie[5], adjust(t.tertiaryColor, {.h = 40.0, .l = -40.0}));
  assignIfEmpty(t.pie[6], adjust(t.primaryColor, {.h = 60.0, .l = -10.0}));
  assignIfEmpty(t.pie[7], adjust(t.primaryColor, {.h = -60.0, .l = -10.0}));
  assignIfEmpty(t.pie[8], adjust(t.primaryColor, {.h = 120.0}));
  assignIfEmpty(t.pie[9], adjust(t.primaryColor, {.h = 60.0, .l = -50.0}));
  assignIfEmpty(t.pie[10], adjust(t.primaryColor, {.h = -60.0, .l = -50.0}));
  assignIfEmpty(t.pie[11], adjust(t.primaryColor, {.h = 120.0, .l = -50.0}));
}

// Dark + Neutral derive the pie palette from cScale (chunk-WYO6CB5R.mjs L613 /
// L1765): `for (i=0; i<THEME_COLOR_LIMIT; i++) this["pie"+i] = this["cScale"+i]`.
// The loop writes upstream keys pie0..pie(count-1) (0-based, count=cScaleCount),
// but the renderer and golden read pie1..pie12, so golden pieK = cScaleK for
// K in 1..min(count-1,12). pie is a 12-slot array, so cap the source index at 12
// (cScale12). Outcomes:
//   dark  TCL=12     -> pie12 empty (loop writes pie0..pie11 only);
//   dark  TCL>=13    -> pie12 = cScale12 = "#010029";
//   neutral TCL>0    -> pie12 = cScale0 (upstream `this.pie12 = this.pie0`,
//                       run after the loop, overrides any cScale12 copy);
//   any TCL<=1       -> no pie slots populated.
// The copy is unconditional (upstream uses `=`, not `||`); user overrides still
// win because resolveFlowTheme re-applies them after updateColors. Must be called
// AFTER cScale0..12 are finalized.
void populatePieFromCScale(FlowThemeVariables& t, bool pie12FromCScale0) {
  const int last = std::min(cScaleCount(t) - 1, 12);
  for (int i = 1; i <= last; ++i) t.pie[i - 1] = t.cScale[i];  // pie1..pieK = cScale1..cScaleK
  if (pie12FromCScale0 && t.themeColorLimit > 0)
    t.pie[11] = t.cScale[0];  // neutral: this.pie12 = this.pie0 (= cScale0)
}

// Quadrant fills + text fills (UNIFORM across all 11 themes): RGB adjustments
// of primaryColor / primaryTextColor in +5/+10/+15 steps.
void populateQuadrant(FlowThemeVariables& t, const QString& primary) {
  assignIfEmpty(t.quadrant[0], primary);
  assignIfEmpty(t.quadrant[1], adjust(primary, {.r = 5.0, .g = 5.0, .b = 5.0}));
  assignIfEmpty(t.quadrant[2], adjust(primary, {.r = 10.0, .g = 10.0, .b = 10.0}));
  assignIfEmpty(t.quadrant[3], adjust(primary, {.r = 15.0, .g = 15.0, .b = 15.0}));
  assignIfEmpty(t.quadrantText[0], t.primaryTextColor);
  assignIfEmpty(t.quadrantText[1], adjust(t.primaryTextColor, {.r = -5.0, .g = -5.0, .b = -5.0}));
  assignIfEmpty(t.quadrantText[2], adjust(t.primaryTextColor, {.r = -10.0, .g = -10.0, .b = -10.0}));
  assignIfEmpty(t.quadrantText[3], adjust(t.primaryTextColor, {.r = -15.0, .g = -15.0, .b = -15.0}));
}

void populateAdjustedScale(FlowThemeVariables& t, const QString& primary,
                           const QString& secondary, const QString& tertiary) {
  assignIfEmpty(t.cScale[0], primary);
  assignIfEmpty(t.cScale[1], secondary);
  assignIfEmpty(t.cScale[2], tertiary);
  const int hues[9] = {30, 60, 90, 120, 150, 210, 270, 300, 330};
  for (int i = 0; i < 9; ++i) {
    color::ChannelAdjust delta{.h = static_cast<double>(hues[i])};
    if (i == 5) delta.l = 150.0;
    assignIfEmpty(t.cScale[i + 3], adjust(primary, delta));
  }
}

void updateBaseCScale(FlowThemeVariables& t, bool darkMode) {
  populateAdjustedScale(t, t.primaryColor, t.secondaryColor, t.tertiaryColor);
  for (int i = 0; i < cScaleCount(t); ++i)
    if (!t.cScale[i].isEmpty()) t.cScale[i] = darken(t.cScale[i], darkMode ? 75 : 25);
  finishCScale(t, false);
}

void updateNeoCScale(FlowThemeVariables& t) {
  const QString primary = QStringLiteral("#ECECFE");
  const QString secondary = QStringLiteral("#E9E9F1");
  populateAdjustedScale(t, primary, secondary, adjust(primary, {.h = 180.0, .l = 5.0}));
  for (int i = 0; i < cScaleCount(t); ++i)
    if (!t.cScale[i].isEmpty()) t.cScale[i] = darken(t.cScale[i], 25);
  finishCScale(t, false);
}

void updateReduxCScale(FlowThemeVariables& t) {
  for (int i = 0; i < cScaleCount(t); ++i) {
    assignIfEmpty(t.cScale[i], t.mainBkg);
    t.cScale[i] = darken(t.cScale[i], 25);
  }
  finishCScale(t, false);
}

void updateReduxColorCScale(FlowThemeVariables& t, bool darkLabels) {
  static const char* const colors[12] = {
      "#f4a8ff", "#46ecd5", "#ffb86a", "#dab2ff", "#7bf1a8", "#c4b4ff",
      "#ffa2a2", "#ffdf20", "#a3b3ff", "#bbf451", "#74d4ff", "#ffa1ad"};
  for (int i = 0; i < 12; ++i) assignIfEmpty(t.cScale[i], QString::fromLatin1(colors[i]));
  finishCScale(t, false, darkLabels);
}

// Default/forest cScale (chunk lines 1643-1673). The darken loop is
// UNCONDITIONAL, so each call darkens cScale by 10 more — default runs this
// twice (double-darken), forest once. cScalePeer/cScaleInv are `||`-guarded
// (idempotent), so they capture the pass-1 (single-darken) cScale.
void updateDefaultForestCScale(FlowThemeVariables& t) {
  assignIfEmpty(t.cScale[0], t.primaryColor);
  assignIfEmpty(t.cScale[1], t.secondaryColor);
  assignIfEmpty(t.cScale[2], t.tertiaryColor);
  assignIfEmpty(t.cScale[3], adjust(t.primaryColor, {.h = 30.0}));
  assignIfEmpty(t.cScale[4], adjust(t.primaryColor, {.h = 60.0}));
  assignIfEmpty(t.cScale[5], adjust(t.primaryColor, {.h = 90.0}));
  assignIfEmpty(t.cScale[6], adjust(t.primaryColor, {.h = 120.0}));
  assignIfEmpty(t.cScale[7], adjust(t.primaryColor, {.h = 150.0}));
  assignIfEmpty(t.cScale[8], adjust(t.primaryColor, {.h = 210.0}));
  assignIfEmpty(t.cScale[9], adjust(t.primaryColor, {.h = 270.0}));
  assignIfEmpty(t.cScale[10], adjust(t.primaryColor, {.h = 300.0}));
  assignIfEmpty(t.cScale[11], adjust(t.primaryColor, {.h = 330.0}));
  assignIfEmpty(t.cScalePeer[1], darken(t.secondaryColor, 45));
  assignIfEmpty(t.cScalePeer[2], darken(t.tertiaryColor, 40));
  for (int i = 0; i < cScaleCount(t); ++i) {
    if (t.cScale[i].isEmpty()) continue;  // no color to darken at this index (e.g. cScale12)
    t.cScale[i] = darken(t.cScale[i], 10);  // UNCONDITIONAL — accumulates per call
    assignIfEmpty(t.cScalePeer[i], darken(t.cScale[i], 25));
    assignIfEmpty(t.cScaleInv[i], adjust(t.cScale[i], {.h = 180.0}));
  }
}

void updateColorsDefault(FlowThemeVariables& t) {
  updateDefaultForestCScale(t);
  for (int i = 0; i < cScaleCount(t); ++i)
    assignIfEmpty(t.cScaleLabel[i], (i == 0 || i == 3)
                                              ? QStringLiteral("#ffffff")
                                              : QStringLiteral("black"));
  t.nodeBkg = t.mainBkg;
  t.nodeBorder = t.border1;
  t.clusterBkg = t.secondBkg;
  t.clusterBorder = t.border2;
  t.defaultLinkColor = t.lineColor;
  t.titleColor = t.textColor;
  t.edgeLabelBackground = t.labelBackground;
  populatePieDefault(t);
  populateQuadrant(t, t.primaryColor);
}

void updateColorsForest(FlowThemeVariables& t) {
  updateDefaultForestCScale(t);
  for (int i = 0; i < cScaleCount(t); ++i)
    assignIfEmpty(t.cScaleLabel[i], QStringLiteral("black"));
  t.nodeBkg = t.mainBkg;
  t.nodeBorder = t.border1;
  t.clusterBkg = t.secondBkg;
  t.clusterBorder = t.border2;
  t.defaultLinkColor = t.lineColor;
  // forest keeps constructor titleColor (#333) and edgeLabelBackground (#e8e8e8).
  populatePieForest(t);
  populateQuadrant(t, t.primaryColor);
}

void updateColorsDark(FlowThemeVariables& t) {
  // dark's updateColors (unconditional `=`, runs ONCE).
  t.secondBkg = lighten(t.mainBkg, 16);
  t.lineColor = t.mainContrastColor;
  t.arrowheadColor = t.mainContrastColor;
  t.nodeBkg = t.mainBkg;
  t.nodeBorder = t.border1;
  t.clusterBkg = t.secondBkg;
  t.clusterBorder = t.border2;
  t.defaultLinkColor = t.lineColor;
  t.edgeLabelBackground = lighten(t.labelBackground, 25);
  // cScale: dark sets cScale1..12 to literal hex UNCONDITIONALLY (chunk lines
  // 1309-1320), then the `||` block keeps them; cScale0 = primaryColor. cScale12
  // = #010029 is set even at TCL=12 (so the golden sees it), but cScaleInv/Peer/
  // Label[12] are only computed when the TCL-gated loop below reaches i=12
  // (TCL>=13), matching upstream.
  assignIfEmpty(t.cScale[0], t.primaryColor);
  t.cScale[1] = QStringLiteral("#0b0000");
  t.cScale[2] = QStringLiteral("#4d1037");
  t.cScale[3] = QStringLiteral("#3f5258");
  t.cScale[4] = QStringLiteral("#4f2f1b");
  t.cScale[5] = QStringLiteral("#6e0a0a");
  t.cScale[6] = QStringLiteral("#3b0048");
  t.cScale[7] = QStringLiteral("#995a01");
  t.cScale[8] = QStringLiteral("#154706");
  t.cScale[9] = QStringLiteral("#161722");
  t.cScale[10] = QStringLiteral("#00296f");
  t.cScale[11] = QStringLiteral("#01629c");
  t.cScale[12] = QStringLiteral("#010029");
  for (int i = 0; i < cScaleCount(t); ++i) {
    assignIfEmpty(t.cScaleInv[i], invert(t.cScale[i]));
    assignIfEmpty(t.cScalePeer[i], lighten(t.cScale[i], 10));
    assignIfEmpty(t.cScaleLabel[i], t.mainContrastColor);
  }
  // dark's final nodeBorder override (line 1502): nodeBorder = nodeBorder || "#999".
  assignIfEmpty(t.nodeBorder, QStringLiteral("#999"));
  // dark does NOT derive nodeTextColor (getStyles falls back to textColor).
  populatePieFromCScale(t, false);  // dark: pie1..pie11 = cScale1..11; pie12 left empty
  populateQuadrant(t, t.primaryColor);
}

void updateColorsNeutral(FlowThemeVariables& t) {
  t.secondBkg = lighten(t.contrast, 55);
  t.border2 = t.contrast;
  t.nodeBkg = t.mainBkg;
  t.nodeBorder = t.border1;
  t.clusterBkg = t.secondBkg;
  t.clusterBorder = t.border2;
  t.defaultLinkColor = t.lineColor;
  t.titleColor = t.text;
  // cScale: all literals (neutral lines 2418-2429).
  const QString literals[12] = {
      QStringLiteral("#555"), QStringLiteral("#F4F4F4"), QStringLiteral("#555"),
      QStringLiteral("#BBB"), QStringLiteral("#777"), QStringLiteral("#999"),
      QStringLiteral("#DDD"), QStringLiteral("#FFF"), QStringLiteral("#DDD"),
      QStringLiteral("#BBB"), QStringLiteral("#999"), QStringLiteral("#777")};
  for (int i = 0; i < 12; ++i) {
    assignIfEmpty(t.cScale[i], literals[i]);
    assignIfEmpty(t.cScaleInv[i], invert(t.cScale[i]));
    assignIfEmpty(t.cScalePeer[i], darken(t.cScale[i], 10));  // darkMode false
    assignIfEmpty(t.cScaleLabel[i], (i == 0 || i == 2)
                                              ? QStringLiteral("#F4F4F4")
                                              : QStringLiteral("#333"));
  }
  // neutral does NOT derive nodeTextColor (getStyles falls back to textColor).
  populatePieFromCScale(t, true);  // neutral: pie1..pie11 = cScale1..11; pie12 = cScale0
  populateQuadrant(t, t.primaryColor);
}

void updateColors(FlowThemeId id, FlowThemeVariables& t) {
  switch (id) {
    case FlowThemeId::Base:
      updateColorsFamilyA(t, QStringLiteral("#333"), false); updateBaseCScale(t, false); break;
    case FlowThemeId::Neo:
      updateColorsFamilyA(t, QStringLiteral("#333"), false, true); updateNeoCScale(t); break;
    case FlowThemeId::NeoDark:
      updateColorsFamilyA(t, QStringLiteral("#333"), true); updateBaseCScale(t, false); break;
    case FlowThemeId::Redux:
      updateColorsFamilyA(t, QStringLiteral("#28253D"), false, true); updateReduxCScale(t); break;
    case FlowThemeId::ReduxDark:
      updateColorsFamilyA(t, QStringLiteral("#FFFFFF"), true); updateBaseCScale(t, false); break;
    case FlowThemeId::ReduxColor:
      updateColorsFamilyA(t, QStringLiteral("#28253D"), false, true); updateReduxColorCScale(t, false); break;
    case FlowThemeId::ReduxDarkColor:
      updateColorsFamilyA(t, QStringLiteral("#FFFFFF"), true); updateReduxColorCScale(t, true); break;
    case FlowThemeId::Default: updateColorsDefault(t); break;
    case FlowThemeId::Forest: updateColorsForest(t); break;
    case FlowThemeId::Dark: updateColorsDark(t); break;
    case FlowThemeId::Neutral: updateColorsNeutral(t); break;
  }
}

// Whether the theme's constructor calls this.updateColors() (chunk line 1641).
// Only `default` does — so default runs updateColors twice (constructor + calculate).
bool constructorCallsUpdateColors(FlowThemeId id) { return id == FlowThemeId::Default; }

// default/forest calculate() clears "calculated" sentinels to undefined before
// the first updateColors pass (chunk lines 1905-1909). base themes do not.
bool clearsCalculatedSentinels(FlowThemeId id) {
  return id == FlowThemeId::Default || id == FlowThemeId::Forest;
}

void clearCalculatedSentinels(FlowThemeVariables& t) {
  const auto clear = [](QString& f) { if (f == QStringLiteral("calculated")) f.clear(); };
  clear(t.secondBkg); clear(t.lineColor); clear(t.arrowheadColor); clear(t.nodeBkg);
  clear(t.nodeBorder); clear(t.clusterBkg); clear(t.clusterBorder);
  clear(t.defaultLinkColor); clear(t.titleColor); clear(t.edgeLabelBackground);
  clear(t.border2);
}

}  // namespace

FlowThemeId parseThemeId(const QString& name) {
  const QString n = name.toLower().trimmed();
  if (n == QStringLiteral("base")) return FlowThemeId::Base;
  if (n == QStringLiteral("dark")) return FlowThemeId::Dark;
  if (n == QStringLiteral("default")) return FlowThemeId::Default;
  if (n == QStringLiteral("forest")) return FlowThemeId::Forest;
  if (n == QStringLiteral("neutral")) return FlowThemeId::Neutral;
  if (n == QStringLiteral("neo")) return FlowThemeId::Neo;
  if (n == QStringLiteral("neo-dark")) return FlowThemeId::NeoDark;
  if (n == QStringLiteral("redux")) return FlowThemeId::Redux;
  if (n == QStringLiteral("redux-dark")) return FlowThemeId::ReduxDark;
  if (n == QStringLiteral("redux-color")) return FlowThemeId::ReduxColor;
  if (n == QStringLiteral("redux-dark-color")) return FlowThemeId::ReduxDarkColor;
  return FlowThemeId::Default;
}

FlowThemeVariables resolveFlowTheme(FlowThemeId id, const QHash<QString, QString>& overrides) {
  FlowThemeVariables t;
  applyRawConstructor(id, t);
  // default's constructor calls updateColors() at the end (1st pass).
  if (constructorCallsUpdateColors(id)) updateColors(id, t);
  // calculate(overrides): sentinel-clear (default/forest) → apply overrides →
  // updateColors → re-apply overrides (user wins over derived).
  if (clearsCalculatedSentinels(id)) clearCalculatedSentinels(t);
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) t.set(it.key(), it.value());
  updateColors(id, t);
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) t.set(it.key(), it.value());
  return t;
}

FlowThemeVariables resolveFlowTheme(FlowThemeId id) {
  return resolveFlowTheme(id, {});
}

// --- get/set by field name (override application + golden comparison) ---

QString FlowThemeVariables::get(const QString& key) const {
  if (key == QStringLiteral("background")) return background;
  if (key == QStringLiteral("primaryColor")) return primaryColor;
  if (key == QStringLiteral("secondaryColor")) return secondaryColor;
  if (key == QStringLiteral("tertiaryColor")) return tertiaryColor;
  if (key == QStringLiteral("mainBkg")) return mainBkg;
  if (key == QStringLiteral("secondBkg")) return secondBkg;
  if (key == QStringLiteral("lineColor")) return lineColor;
  if (key == QStringLiteral("border1")) return border1;
  if (key == QStringLiteral("border2")) return border2;
  if (key == QStringLiteral("arrowheadColor")) return arrowheadColor;
  if (key == QStringLiteral("fontFamily")) return fontFamily;
  if (key == QStringLiteral("fontSize")) return fontSize;
  if (key == QStringLiteral("labelBackground")) return labelBackground;
  if (key == QStringLiteral("textColor")) return textColor;
  if (key == QStringLiteral("titleColor")) return titleColor;
  if (key == QStringLiteral("edgeLabelBackground")) return edgeLabelBackground;
  if (key == QStringLiteral("clusterBkg")) return clusterBkg;
  if (key == QStringLiteral("clusterBorder")) return clusterBorder;
  if (key == QStringLiteral("primaryBorderColor")) return primaryBorderColor;
  if (key == QStringLiteral("primaryTextColor")) return primaryTextColor;
  if (key == QStringLiteral("nodeTextColor")) return nodeTextColor;
  if (key == QStringLiteral("nodeBkg")) return nodeBkg;
  if (key == QStringLiteral("nodeBorder")) return nodeBorder;
  if (key == QStringLiteral("defaultLinkColor")) return defaultLinkColor;
  if (key == QStringLiteral("strokeWidth")) return QString::number(strokeWidth);
  if (key == QStringLiteral("useGradient")) return useGradient ? QStringLiteral("true") : QStringLiteral("false");
  if (key == QStringLiteral("gradientStart")) return gradientStart;
  if (key == QStringLiteral("gradientStop")) return gradientStop;
  if (key == QStringLiteral("THEME_COLOR_LIMIT")) return QString::number(themeColorLimit);
  for (int i = 0; i < 13; ++i) {
    if (key == QStringLiteral("cScale%1").arg(i)) return cScale[i];
    if (key == QStringLiteral("cScaleInv%1").arg(i)) return cScaleInv[i];
    if (key == QStringLiteral("cScalePeer%1").arg(i)) return cScalePeer[i];
    if (key == QStringLiteral("cScaleLabel%1").arg(i)) return cScaleLabel[i];
  }
  // Pie + Quadrant themeVariables.
  for (int i = 0; i < 12; ++i)
    if (key == QStringLiteral("pie%1").arg(i + 1)) return pie[i];
  for (int i = 0; i < 4; ++i) {
    if (key == QStringLiteral("quadrant%1Fill").arg(i + 1)) return quadrant[i];
    if (key == QStringLiteral("quadrant%1TextFill").arg(i + 1)) return quadrantText[i];
  }
  if (key == QStringLiteral("pieTitleTextColor")) return pieTitleTextColor;
  if (key == QStringLiteral("pieSectionTextColor")) return pieSectionTextColor;
  if (key == QStringLiteral("pieLegendTextColor")) return pieLegendTextColor;
  if (key == QStringLiteral("pieStrokeColor")) return pieStrokeColor;
  if (key == QStringLiteral("pieStrokeWidth")) return pieStrokeWidth;
  if (key == QStringLiteral("pieOpacity")) return pieOpacity;
  if (key == QStringLiteral("pieOuterStrokeColor")) return pieOuterStrokeColor;
  if (key == QStringLiteral("pieOuterStrokeWidth")) return pieOuterStrokeWidth;
  if (key == QStringLiteral("pieTitleTextSize")) return pieTitleTextSize;
  if (key == QStringLiteral("pieSectionTextSize")) return pieSectionTextSize;
  if (key == QStringLiteral("pieLegendTextSize")) return pieLegendTextSize;
  if (key == QStringLiteral("quadrantPointFill")) return quadrantPointFill;
  if (key == QStringLiteral("quadrantPointTextFill")) return quadrantPointTextFill;
  if (key == QStringLiteral("quadrantXAxisTextFill")) return quadrantXAxisTextFill;
  if (key == QStringLiteral("quadrantYAxisTextFill")) return quadrantYAxisTextFill;
  if (key == QStringLiteral("quadrantInternalBorderStrokeFill")) return quadrantInternalBorderStrokeFill;
  if (key == QStringLiteral("quadrantExternalBorderStrokeFill")) return quadrantExternalBorderStrokeFill;
  if (key == QStringLiteral("quadrantTitleFill")) return quadrantTitleFill;
  return QString();
}

void FlowThemeVariables::set(const QString& key, const QString& value) {
  if (key == QStringLiteral("background")) background = value;
  else if (key == QStringLiteral("primaryColor")) primaryColor = value;
  else if (key == QStringLiteral("secondaryColor")) secondaryColor = value;
  else if (key == QStringLiteral("tertiaryColor")) tertiaryColor = value;
  else if (key == QStringLiteral("mainBkg")) mainBkg = value;
  else if (key == QStringLiteral("secondBkg")) secondBkg = value;
  else if (key == QStringLiteral("lineColor")) lineColor = value;
  else if (key == QStringLiteral("border1")) border1 = value;
  else if (key == QStringLiteral("border2")) border2 = value;
  else if (key == QStringLiteral("arrowheadColor")) arrowheadColor = value;
  else if (key == QStringLiteral("fontFamily")) fontFamily = value;
  else if (key == QStringLiteral("fontSize")) fontSize = value;
  else if (key == QStringLiteral("labelBackground")) labelBackground = value;
  else if (key == QStringLiteral("textColor")) textColor = value;
  else if (key == QStringLiteral("titleColor")) titleColor = value;
  else if (key == QStringLiteral("edgeLabelBackground")) edgeLabelBackground = value;
  else if (key == QStringLiteral("clusterBkg")) clusterBkg = value;
  else if (key == QStringLiteral("clusterBorder")) clusterBorder = value;
  else if (key == QStringLiteral("primaryBorderColor")) primaryBorderColor = value;
  else if (key == QStringLiteral("primaryTextColor")) primaryTextColor = value;
  else if (key == QStringLiteral("nodeTextColor")) nodeTextColor = value;
  else if (key == QStringLiteral("nodeBkg")) nodeBkg = value;
  else if (key == QStringLiteral("nodeBorder")) nodeBorder = value;
  else if (key == QStringLiteral("defaultLinkColor")) defaultLinkColor = value;
  else if (key == QStringLiteral("strokeWidth")) strokeWidth = value.toDouble();
  else if (key == QStringLiteral("useGradient")) useGradient = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
  else if (key == QStringLiteral("gradientStart")) gradientStart = value;
  else if (key == QStringLiteral("gradientStop")) gradientStop = value;
  else if (key == QStringLiteral("THEME_COLOR_LIMIT")) themeColorLimit = value.toInt();
  // Pie + Quadrant overrides (separate from the main chain; loops early-return).
  for (int i = 0; i < 12; ++i)
    if (key == QStringLiteral("pie%1").arg(i + 1)) { pie[i] = value; return; }
  for (int i = 0; i < 4; ++i) {
    if (key == QStringLiteral("quadrant%1Fill").arg(i + 1)) { quadrant[i] = value; return; }
    if (key == QStringLiteral("quadrant%1TextFill").arg(i + 1)) { quadrantText[i] = value; return; }
  }
  if (key == QStringLiteral("pieTitleTextColor")) pieTitleTextColor = value;
  else if (key == QStringLiteral("pieSectionTextColor")) pieSectionTextColor = value;
  else if (key == QStringLiteral("pieLegendTextColor")) pieLegendTextColor = value;
  else if (key == QStringLiteral("pieStrokeColor")) pieStrokeColor = value;
  else if (key == QStringLiteral("pieStrokeWidth")) pieStrokeWidth = value;
  else if (key == QStringLiteral("pieOpacity")) pieOpacity = value;
  else if (key == QStringLiteral("pieOuterStrokeColor")) pieOuterStrokeColor = value;
  else if (key == QStringLiteral("pieOuterStrokeWidth")) pieOuterStrokeWidth = value;
  else if (key == QStringLiteral("pieTitleTextSize")) pieTitleTextSize = value;
  else if (key == QStringLiteral("pieSectionTextSize")) pieSectionTextSize = value;
  else if (key == QStringLiteral("pieLegendTextSize")) pieLegendTextSize = value;
  else if (key == QStringLiteral("quadrantPointFill")) quadrantPointFill = value;
  else if (key == QStringLiteral("quadrantPointTextFill")) quadrantPointTextFill = value;
  else if (key == QStringLiteral("quadrantXAxisTextFill")) quadrantXAxisTextFill = value;
  else if (key == QStringLiteral("quadrantYAxisTextFill")) quadrantYAxisTextFill = value;
  else if (key == QStringLiteral("quadrantInternalBorderStrokeFill")) quadrantInternalBorderStrokeFill = value;
  else if (key == QStringLiteral("quadrantExternalBorderStrokeFill")) quadrantExternalBorderStrokeFill = value;
  else if (key == QStringLiteral("quadrantTitleFill")) quadrantTitleFill = value;
}

}  // namespace muffin::mermaid::flowtheme
