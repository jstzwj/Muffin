#include "mermaid/theme/FlowTheme.h"

#include "mermaid/theme/MermaidColor.h"

#include <QString>

#include <algorithm>
#include <limits>

namespace muffin::mermaid::flowtheme {
namespace {

using color::adjust;
using color::darken;
using color::invert;
using color::isDark;
using color::lighten;
using color::mkBorder;
using color::rgba;

// JS NaN — upstream's quadrantPointFill calls lighten()/darken() with ONE arg, so
// amount=undefined -> lightness = clampL(L + NaN) = NaN, stringifying as
// hsl(h, s, NaN%). Used to reproduce that exactly.
constexpr double kJsNaN = std::numeric_limits<double>::quiet_NaN();

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

void setShadow(FlowThemeVariables& t, const QString& color, qreal opacity,
               qreal offsetX, qreal offsetY, const QString& css) {
  t.shadowColor = color;
  t.shadowOpacity = opacity;
  t.shadowOffsetX = offsetX;
  t.shadowOffsetY = offsetY;
  t.dropShadow = css;
}

// --- per-theme raw constructors (the `this.X = literal` lines) ---

void applyBase(FlowThemeVariables& t) {
  t.background = QStringLiteral("#f4f4f4");
  t.primaryColor = QStringLiteral("#fff4dd");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.fontFamily = QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif");
  t.fontSize = QStringLiteral("16px");
  t.fontWeight = QStringLiteral("normal");
  setShadow(t, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0,
            QStringLiteral("drop-shadow( 1px 2px 2px rgba(185,185,185,1))"));
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
  t.fontWeight = QStringLiteral("normal");
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
  setShadow(t, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0,
            QStringLiteral("drop-shadow( 1px 2px 2px rgba(185,185,185,1))"));
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
  t.fontWeight = QStringLiteral("normal");
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
  setShadow(t, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0,
            QStringLiteral("drop-shadow(1px 2px 2px rgba(185, 185, 185, 1))"));
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
  t.fontWeight = QStringLiteral("normal");
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
  setShadow(t, QStringLiteral("#b9b9b9"), 0.5, 1.0, 2.0,
            QStringLiteral("drop-shadow( 1px 2px 2px rgba(185,185,185,0.5))"));
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
  t.fontWeight = QStringLiteral("normal");
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
  setShadow(t, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0,
            QStringLiteral("drop-shadow( 1px 2px 2px rgba(185,185,185,1))"));
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
  t.fontWeight = QStringLiteral("normal");
  t.nodeBorder = QStringLiteral("#000000");
  t.tertiaryColor = QStringLiteral("#ffffff");
  t.useGradient = true;
  t.gradientStart = QStringLiteral("#0042eb");
  t.gradientStop = QStringLiteral("#eb0042");
  setShadow(t, QStringLiteral("#000000"), 0.25, 0.0, 1.0,
            QStringLiteral("drop-shadow( 0px 1px 2px rgba(0, 0, 0, 0.25));"));
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
  t.fontWeight = QStringLiteral("normal");
  t.labelBackground = QStringLiteral("#181818");
  t.textColor = QStringLiteral("#ccc");
  t.themeColorLimit = 12;
  t.strokeWidth = 1;
  t.useGradient = true;
  t.gradientStart = QStringLiteral("#0042eb");
  t.gradientStop = QStringLiteral("#eb0042");
  setShadow(t, QStringLiteral("#b9b9b9"), 0.2, 1.0, 2.0,
            QStringLiteral("drop-shadow( 1px 2px 2px rgba(185,185,185,0.2))"));
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
  t.fontWeight = QStringLiteral("600");
  t.nodeBorder = QStringLiteral("#28253D");
  t.tertiaryColor = QStringLiteral("#ffffff");
  t.clusterBkg = QStringLiteral("#F9F9FB");
  t.clusterBorder = QStringLiteral("#BDBCCC");
  setShadow(t, QStringLiteral("#000000"), 0.06, 4.0, 4.0,
            QStringLiteral("url(#drop-shadow)"));
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
  t.fontWeight = QStringLiteral("600");
  t.labelBackground = QStringLiteral("#111113");
  t.textColor = QStringLiteral("#ccc");
  t.themeColorLimit = 12;
  t.strokeWidth = 2;
  t.nodeBorder = QStringLiteral("#FFFFFF");
  t.clusterBkg = QStringLiteral("#1E1A2E");
  t.clusterBorder = QStringLiteral("#BDBCCC");
  setShadow(t, QStringLiteral("#ffffff"), 0.06, 4.0, 4.0,
            QStringLiteral("url(#drop-shadow)"));
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
  t.fontWeight = QStringLiteral("600");
  t.nodeBorder = QStringLiteral("#28253D");
  t.tertiaryColor = QStringLiteral("#ffffff");
  setShadow(t, QStringLiteral("#000000"), 0.06, 4.0, 4.0,
            QStringLiteral("url(#drop-shadow)"));
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
void populatePieScalars(FlowThemeVariables& t, const QString& titleLegendColor);
void populateQuadrant(FlowThemeVariables& t, const QString& primary);
void populateJourneyFillTypes(FlowThemeVariables& t, const QString& primary,
                              const QString& secondary, bool unconditional);
void populateXYChart(FlowThemeId id, FlowThemeVariables& t);

// Mindmap classic root colors are borrowed from the first git palette slot.
// Keep the per-theme update semantics here: Default calls this twice, so its
// existing git0 is darkened twice; every other built-in theme calls it once.
void populateMindmapRoot(FlowThemeId id, FlowThemeVariables& t) {
  switch (id) {
    case FlowThemeId::Base:
      assignIfEmpty(t.git0, t.primaryColor);
      t.git0 = darken(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, t.primaryTextColor);
      break;
    case FlowThemeId::Dark:
      t.git0 = lighten(t.secondaryColor, 20);
      assignIfEmpty(t.gitBranchLabel0, t.taskTextDarkColor);
      break;
    case FlowThemeId::Default:
      assignIfEmpty(t.git0, t.primaryColor);
      t.git0 = darken(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, QStringLiteral("#ffffff"));
      break;
    case FlowThemeId::Forest:
      assignIfEmpty(t.git0, t.primaryColor);
      t.git0 = darken(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, QStringLiteral("#ffffff"));
      break;
    case FlowThemeId::Neutral:
      t.git0 = darken(t.pie[0], 25);
      t.gitBranchLabel0 = t.text;
      break;
    case FlowThemeId::Neo:
    case FlowThemeId::Redux:
    case FlowThemeId::ReduxColor:
      assignIfEmpty(t.git0, QStringLiteral("#ECECFE"));
      t.git0 = darken(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, t.primaryTextColor);
      break;
    case FlowThemeId::NeoDark:
      assignIfEmpty(t.git0, QStringLiteral("#0b0000"));
      t.git0 = lighten(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, t.primaryTextColor);
      break;
    case FlowThemeId::ReduxDark:
    case FlowThemeId::ReduxDarkColor:
      assignIfEmpty(t.git0, t.primaryColor);
      t.git0 = darken(t.git0, 25);
      assignIfEmpty(t.gitBranchLabel0, t.primaryTextColor);
      break;
  }
}

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
  const QString secondary = lightPalette ? QStringLiteral("#E9E9F1") : t.secondaryColor;
  populateJourneyFillTypes(t, primary, secondary, false);
  // taskTextDarkColor (pie title/legend source for every non-dark theme) =
  // textColor. The dark-variant FamilyA (neo-dark/redux-dark/redux-dark-color,
  // flagged by nodeBorderFromBorder1) also sets mainContrastColor="lightgrey"
  // (upstream constructor literal) for golden parity; their pie title/legend
  // still use taskTextDarkColor, NOT mainContrastColor.
  assignIfEmpty(t.taskTextDarkColor, t.textColor);
  if (nodeBorderFromBorder1) assignIfEmpty(t.mainContrastColor, QStringLiteral("lightgrey"));
  populatePieScalars(t, t.taskTextDarkColor);
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

// Journey's eight task/section fill types share one hue-rotation formula in
// all themes. Family-A themes use `||` guards; Dark/Default/Forest/Neutral use
// unconditional assignments. The distinction matters during calculate(): a
// direct fillType override still wins after the final override re-application,
// while primary/secondary overrides must feed the derived palette.
void populateJourneyFillTypes(FlowThemeVariables& t, const QString& primary,
                              const QString& secondary, bool unconditional) {
  const QString values[8] = {
      primary,
      secondary,
      adjust(primary, {.h = 64.0}),
      adjust(secondary, {.h = 64.0}),
      adjust(primary, {.h = -64.0}),
      adjust(secondary, {.h = -64.0}),
      adjust(primary, {.h = 128.0}),
      adjust(secondary, {.h = 128.0}),
  };
  for (int i = 0; i < 8; ++i) {
    if (unconditional)
      t.fillType[i] = values[i];
    else
      assignIfEmpty(t.fillType[i], values[i]);
  }
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

// Pie scalar themeVariables (UNIFORM formulas across all 11 themes): the stroke
// / opacity / sizes are constant defaults; pieSectionTextColor = textColor; and
// pieTitleTextColor / pieLegendTextColor = titleLegendColor, which is
// taskTextDarkColor for every theme but the standalone dark (mainContrastColor).
// `||`-guarded so a direct pie*TextColor override wins, and (because
// resolveFlowTheme re-applies overrides after updateColors) a taskTextDarkColor /
// mainContrastColor override propagates here.
void populatePieScalars(FlowThemeVariables& t, const QString& titleLegendColor) {
  assignIfEmpty(t.pieTitleTextColor, titleLegendColor);
  assignIfEmpty(t.pieSectionTextColor, t.textColor);
  assignIfEmpty(t.pieLegendTextColor, titleLegendColor);
  assignIfEmpty(t.pieStrokeColor, QStringLiteral("black"));
  assignIfEmpty(t.pieStrokeWidth, QStringLiteral("2px"));
  assignIfEmpty(t.pieOuterStrokeColor, QStringLiteral("black"));
  assignIfEmpty(t.pieOuterStrokeWidth, QStringLiteral("2px"));
  assignIfEmpty(t.pieOpacity, QStringLiteral("0.7"));
  assignIfEmpty(t.pieTitleTextSize, QStringLiteral("25px"));
  assignIfEmpty(t.pieSectionTextSize, QStringLiteral("17px"));
  assignIfEmpty(t.pieLegendTextSize, QStringLiteral("17px"));
}

// Quadrant fills + text fills + scalar fields (UNIFORM across all 11 themes):
// RGB adjustments of primaryColor / primaryTextColor in +5/+10/+15 steps, plus
// the scalar fields (pointText/xAxis/yAxis/title = primaryTextColor; internal/
// external border = primaryBorderColor). quadrantPointFill is upstream's
//   this.quadrantPointFill = this.quadrantPointFill || isDark(this.quadrant1Fill)
//       ? lighten(this.quadrant1Fill) : darken(this.quadrant1Fill);
// which by JS precedence parses as
//   (this.quadrantPointFill || isDark(q1)) ? lighten(q1) : darken(q1)
// -- an UNCONDITIONAL reassignment every updateColors() (NOT a ||-guarded
// assign-if-empty). The one-arg lighten/darken pass amount=undefined -> lightness
// NaN -> hsl(h, s, NaN%), so both branches yield the same string; the condition
// still matters because Default's double pass means pointFill is already set on
// the second updateColors, so a user quadrant1Fill override must re-derive
// pointFill from the NEW q1 (the non-empty pointFill forces the lighten branch).
// A direct quadrantPointFill override still wins -- resolveFlowTheme re-applies
// overrides after the final updateColors.
void populateQuadrant(FlowThemeVariables& t, const QString& primary) {
  assignIfEmpty(t.quadrant[0], primary);
  assignIfEmpty(t.quadrant[1], adjust(primary, {.r = 5.0, .g = 5.0, .b = 5.0}));
  assignIfEmpty(t.quadrant[2], adjust(primary, {.r = 10.0, .g = 10.0, .b = 10.0}));
  assignIfEmpty(t.quadrant[3], adjust(primary, {.r = 15.0, .g = 15.0, .b = 15.0}));
  assignIfEmpty(t.quadrantText[0], t.primaryTextColor);
  assignIfEmpty(t.quadrantText[1], adjust(t.primaryTextColor, {.r = -5.0, .g = -5.0, .b = -5.0}));
  assignIfEmpty(t.quadrantText[2], adjust(t.primaryTextColor, {.r = -10.0, .g = -10.0, .b = -10.0}));
  assignIfEmpty(t.quadrantText[3], adjust(t.primaryTextColor, {.r = -15.0, .g = -15.0, .b = -15.0}));
  assignIfEmpty(t.quadrantPointTextFill, t.primaryTextColor);
  assignIfEmpty(t.quadrantXAxisTextFill, t.primaryTextColor);
  assignIfEmpty(t.quadrantYAxisTextFill, t.primaryTextColor);
  assignIfEmpty(t.quadrantTitleFill, t.primaryTextColor);
  assignIfEmpty(t.quadrantInternalBorderStrokeFill, t.primaryBorderColor);
  assignIfEmpty(t.quadrantExternalBorderStrokeFill, t.primaryBorderColor);
  // Unconditional reassignment mirroring upstream's
  //   (this.quadrantPointFill || isDark(q1)) ? lighten(q1) : darken(q1)
  // (see the function header); the one-arg calls produce hsl(h, s, NaN%).
  const bool lightenBranch = !t.quadrantPointFill.isEmpty() || isDark(t.quadrant[0]);
  t.quadrantPointFill =
      lightenBranch ? lighten(t.quadrant[0], kJsNaN) : darken(t.quadrant[0], kJsNaN);
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
  assignIfEmpty(t.taskTextDarkColor, QStringLiteral("black"));
  populateJourneyFillTypes(t, t.primaryColor, t.secondaryColor, true);
  populatePieScalars(t, t.taskTextDarkColor);
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
  assignIfEmpty(t.taskTextDarkColor, QStringLiteral("black"));
  populateJourneyFillTypes(t, t.primaryColor, t.secondaryColor, true);
  populatePieScalars(t, t.taskTextDarkColor);
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
  assignIfEmpty(t.taskTextDarkColor, invert(t.mainContrastColor));  // dark: invert(lightgrey) = #2c2c2c
  populateJourneyFillTypes(t, t.primaryColor, t.secondaryColor, true);
  populatePieScalars(t, t.mainContrastColor);  // dark pie title/legend = mainContrastColor
  populatePieFromCScale(t, false);  // dark: pie1..pie(K) = cScale1..K; pie12 empty at TCL<=12, =cScale12 at TCL>=13
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
  // neutral sets taskTextDarkColor UNCONDITIONALLY (= this.text; chunk line 1729),
  // so a taskTextDarkColor override is clobbered during updateColors and does NOT
  // propagate to pie title/legend (probed vs upstream). Re-applied after, the
  // override still wins on get("taskTextDarkColor") itself.
  t.taskTextDarkColor = t.text;  // neutral.text = #333
  populateJourneyFillTypes(t, t.primaryColor, t.secondaryColor, true);
  populatePieScalars(t, t.taskTextDarkColor);
  populatePieFromCScale(t, true);  // neutral: pie1..pie(K) = cScale1..K; pie12 = cScale0
  populateQuadrant(t, t.primaryColor);
}

void populateXYChart(FlowThemeId id, FlowThemeVariables& t) {
  QString palette;
  switch (id) {
    case FlowThemeId::Dark:
      palette = QStringLiteral("#3498db,#2ecc71,#e74c3c,#f1c40f,#bdc3c7,#ffffff,#34495e,#9b59b6,#1abc9c,#e67e22");
      break;
    case FlowThemeId::Default:
      palette = QStringLiteral("#ECECFF,#8493A6,#FFC3A0,#DCDDE1,#B8E994,#D1A36F,#C3CDE6,#FFB6C1,#496078,#F8F3E3");
      break;
    case FlowThemeId::Forest:
      palette = QStringLiteral("#CDE498,#FF6B6B,#A0D2DB,#D7BDE2,#F0F0F0,#FFC3A0,#7FD8BE,#FF9A8B,#FAF3E0,#FFF176");
      break;
    case FlowThemeId::Neutral:
      palette = QStringLiteral("#EEE,#6BB8E4,#8ACB88,#C7ACD6,#E8DCC2,#FFB2A8,#FFF380,#7E8D91,#FFD8B1,#FAF3E0");
      break;
    default:
      palette = QStringLiteral("#FFF4DD,#FFD8B1,#FFA07A,#ECEFF1,#D6DBDF,#C3E0A8,#FFB6A4,#FFD74D,#738FA7,#FFFFF0");
      break;
  }

  assignIfEmpty(t.xyChart.backgroundColor, t.background);
  assignIfEmpty(t.xyChart.titleColor, t.primaryTextColor);

  // Theme5..Theme10 (Neo and Redux variants) extend the base theme class.
  // Their own XYChart object literals omit dataLabelColor, so the value created
  // by the base constructor survives as #131300 instead of following those
  // themes' primaryTextColor. The other five themes set it directly.
  const bool inheritedDataLabel =
      id == FlowThemeId::Neo || id == FlowThemeId::NeoDark ||
      id == FlowThemeId::Redux || id == FlowThemeId::ReduxDark ||
      id == FlowThemeId::ReduxColor || id == FlowThemeId::ReduxDarkColor;
  assignIfEmpty(t.xyChart.dataLabelColor,
                inheritedDataLabel ? QStringLiteral("#131300")
                                   : t.primaryTextColor);

  assignIfEmpty(t.xyChart.xAxisTitleColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.xAxisLabelColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.xAxisTickColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.xAxisLineColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.yAxisTitleColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.yAxisLabelColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.yAxisTickColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.yAxisLineColor, t.primaryTextColor);
  assignIfEmpty(t.xyChart.plotColorPalette, palette);
}

void populatePacket(FlowThemeId id, FlowThemeVariables& t) {
  if (id != FlowThemeId::Dark && id != FlowThemeId::Forest) return;
  // Both upstream updateColors implementations assign a fresh object, not a
  // field-wise merge. PacketStyleOptions supplies the four typography/stroke
  // defaults that are absent from that object.
  t.packet = PacketThemeVariables{};
  t.packet.startByteColor = t.primaryTextColor;
  t.packet.endByteColor = t.primaryTextColor;
  t.packet.labelColor = t.primaryTextColor;
  t.packet.titleColor = t.primaryTextColor;
  t.packet.blockStrokeColor = t.primaryTextColor;
  t.packet.blockFillColor = id == FlowThemeId::Dark ? t.background : t.mainBkg;
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
  populateMindmapRoot(id, t);
  populatePacket(id, t);
  populateXYChart(id, t);
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
  const auto applyOverrides = [&overrides](FlowThemeVariables& theme) {
    // calculate() replays the top-level `packet` key as one object. Replacing
    // it discards Dark/Forest's derived sibling colors; styles.ts then fills
    // omitted fields from PacketStyleOptions. Keep the marker out of get/set
    // so QHash iteration order cannot affect replacement semantics.
    if (overrides.contains(QStringLiteral("packet.__replace")))
      theme.packet = PacketThemeVariables{};
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
      if (it.key() != QLatin1String("packet.__replace"))
        theme.set(it.key(), it.value());
    }
  };
  applyOverrides(t);
  updateColors(id, t);
  applyOverrides(t);
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
  if (key == QStringLiteral("fontWeight")) return fontWeight;
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
  if (key == QStringLiteral("git0")) return git0;
  if (key == QStringLiteral("gitBranchLabel0")) return gitBranchLabel0;
  if (key == QStringLiteral("strokeWidth")) return QString::number(strokeWidth);
  if (key == QStringLiteral("useGradient")) return useGradient ? QStringLiteral("true") : QStringLiteral("false");
  if (key == QStringLiteral("gradientStart")) return gradientStart;
  if (key == QStringLiteral("gradientStop")) return gradientStop;
  if (key == QStringLiteral("dropShadow")) return dropShadow;
  if (key == QStringLiteral("THEME_COLOR_LIMIT")) return QString::number(themeColorLimit);
  for (int i = 0; i < 13; ++i) {
    if (key == QStringLiteral("cScale%1").arg(i)) return cScale[i];
    if (key == QStringLiteral("cScaleInv%1").arg(i)) return cScaleInv[i];
    if (key == QStringLiteral("cScalePeer%1").arg(i)) return cScalePeer[i];
    if (key == QStringLiteral("cScaleLabel%1").arg(i)) return cScaleLabel[i];
  }
  // Journey + Pie + Quadrant themeVariables.
  for (int i = 0; i < 8; ++i)
    if (key == QStringLiteral("fillType%1").arg(i)) return fillType[i];
  for (int i = 0; i < 12; ++i)
    if (key == QStringLiteral("pie%1").arg(i + 1)) return pie[i];
  for (int i = 0; i < 4; ++i) {
    if (key == QStringLiteral("quadrant%1Fill").arg(i + 1)) return quadrant[i];
    if (key == QStringLiteral("quadrant%1TextFill").arg(i + 1)) return quadrantText[i];
  }
  if (key == QStringLiteral("mainContrastColor")) return mainContrastColor;
  if (key == QStringLiteral("taskTextDarkColor")) return taskTextDarkColor;
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
  if (key == QStringLiteral("xyChart.backgroundColor")) return xyChart.backgroundColor;
  if (key == QStringLiteral("xyChart.titleColor")) return xyChart.titleColor;
  if (key == QStringLiteral("xyChart.dataLabelColor")) return xyChart.dataLabelColor;
  if (key == QStringLiteral("xyChart.xAxisTitleColor")) return xyChart.xAxisTitleColor;
  if (key == QStringLiteral("xyChart.xAxisLabelColor")) return xyChart.xAxisLabelColor;
  if (key == QStringLiteral("xyChart.xAxisTickColor")) return xyChart.xAxisTickColor;
  if (key == QStringLiteral("xyChart.xAxisLineColor")) return xyChart.xAxisLineColor;
  if (key == QStringLiteral("xyChart.yAxisTitleColor")) return xyChart.yAxisTitleColor;
  if (key == QStringLiteral("xyChart.yAxisLabelColor")) return xyChart.yAxisLabelColor;
  if (key == QStringLiteral("xyChart.yAxisTickColor")) return xyChart.yAxisTickColor;
  if (key == QStringLiteral("xyChart.yAxisLineColor")) return xyChart.yAxisLineColor;
  if (key == QStringLiteral("xyChart.plotColorPalette")) return xyChart.plotColorPalette;
  if (key == QStringLiteral("packet.byteFontSize")) return packet.byteFontSize;
  if (key == QStringLiteral("packet.startByteColor")) return packet.startByteColor;
  if (key == QStringLiteral("packet.endByteColor")) return packet.endByteColor;
  if (key == QStringLiteral("packet.labelColor")) return packet.labelColor;
  if (key == QStringLiteral("packet.labelFontSize")) return packet.labelFontSize;
  if (key == QStringLiteral("packet.titleColor")) return packet.titleColor;
  if (key == QStringLiteral("packet.titleFontSize")) return packet.titleFontSize;
  if (key == QStringLiteral("packet.blockStrokeColor")) return packet.blockStrokeColor;
  if (key == QStringLiteral("packet.blockStrokeWidth")) return packet.blockStrokeWidth;
  if (key == QStringLiteral("packet.blockFillColor")) return packet.blockFillColor;
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
  else if (key == QStringLiteral("fontWeight")) fontWeight = value;
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
  else if (key == QStringLiteral("git0")) git0 = value;
  else if (key == QStringLiteral("gitBranchLabel0")) gitBranchLabel0 = value;
  else if (key == QStringLiteral("strokeWidth")) strokeWidth = value.toDouble();
  else if (key == QStringLiteral("useGradient")) useGradient = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
  else if (key == QStringLiteral("gradientStart")) gradientStart = value;
  else if (key == QStringLiteral("gradientStop")) gradientStop = value;
  else if (key == QStringLiteral("dropShadow")) dropShadow = value;
  else if (key == QStringLiteral("THEME_COLOR_LIMIT")) themeColorLimit = value.toInt();
  // Indexed palette and family overrides are separate from the scalar chain;
  // return as soon as the matching slot is found.
  for (int i = 0; i < 13; ++i) {
    if (key == QStringLiteral("cScale%1").arg(i)) { cScale[i] = value; return; }
    if (key == QStringLiteral("cScaleInv%1").arg(i)) { cScaleInv[i] = value; return; }
    if (key == QStringLiteral("cScalePeer%1").arg(i)) { cScalePeer[i] = value; return; }
    if (key == QStringLiteral("cScaleLabel%1").arg(i)) { cScaleLabel[i] = value; return; }
  }
  // Journey + Pie + Quadrant overrides.
  for (int i = 0; i < 8; ++i)
    if (key == QStringLiteral("fillType%1").arg(i)) { fillType[i] = value; return; }
  for (int i = 0; i < 12; ++i)
    if (key == QStringLiteral("pie%1").arg(i + 1)) { pie[i] = value; return; }
  for (int i = 0; i < 4; ++i) {
    if (key == QStringLiteral("quadrant%1Fill").arg(i + 1)) { quadrant[i] = value; return; }
    if (key == QStringLiteral("quadrant%1TextFill").arg(i + 1)) { quadrantText[i] = value; return; }
  }
  if (key == QStringLiteral("mainContrastColor")) mainContrastColor = value;
  else if (key == QStringLiteral("taskTextDarkColor")) taskTextDarkColor = value;
  else if (key == QStringLiteral("pieTitleTextColor")) pieTitleTextColor = value;
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
  else if (key == QStringLiteral("xyChart.backgroundColor")) xyChart.backgroundColor = value;
  else if (key == QStringLiteral("xyChart.titleColor")) xyChart.titleColor = value;
  else if (key == QStringLiteral("xyChart.dataLabelColor")) xyChart.dataLabelColor = value;
  else if (key == QStringLiteral("xyChart.xAxisTitleColor")) xyChart.xAxisTitleColor = value;
  else if (key == QStringLiteral("xyChart.xAxisLabelColor")) xyChart.xAxisLabelColor = value;
  else if (key == QStringLiteral("xyChart.xAxisTickColor")) xyChart.xAxisTickColor = value;
  else if (key == QStringLiteral("xyChart.xAxisLineColor")) xyChart.xAxisLineColor = value;
  else if (key == QStringLiteral("xyChart.yAxisTitleColor")) xyChart.yAxisTitleColor = value;
  else if (key == QStringLiteral("xyChart.yAxisLabelColor")) xyChart.yAxisLabelColor = value;
  else if (key == QStringLiteral("xyChart.yAxisTickColor")) xyChart.yAxisTickColor = value;
  else if (key == QStringLiteral("xyChart.yAxisLineColor")) xyChart.yAxisLineColor = value;
  else if (key == QStringLiteral("xyChart.plotColorPalette")) xyChart.plotColorPalette = value;
  else if (key == QStringLiteral("packet.byteFontSize")) packet.byteFontSize = value;
  else if (key == QStringLiteral("packet.startByteColor")) packet.startByteColor = value;
  else if (key == QStringLiteral("packet.endByteColor")) packet.endByteColor = value;
  else if (key == QStringLiteral("packet.labelColor")) packet.labelColor = value;
  else if (key == QStringLiteral("packet.labelFontSize")) packet.labelFontSize = value;
  else if (key == QStringLiteral("packet.titleColor")) packet.titleColor = value;
  else if (key == QStringLiteral("packet.titleFontSize")) packet.titleFontSize = value;
  else if (key == QStringLiteral("packet.blockStrokeColor")) packet.blockStrokeColor = value;
  else if (key == QStringLiteral("packet.blockStrokeWidth")) packet.blockStrokeWidth = value;
  else if (key == QStringLiteral("packet.blockFillColor")) packet.blockFillColor = value;
}

}  // namespace muffin::mermaid::flowtheme
