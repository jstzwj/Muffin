#include "mermaid/theme/FlowTheme.h"

#include "mermaid/theme/MermaidColor.h"

#include <QString>

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
}

void applyRedux(FlowThemeVariables& t) {
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

void applyReduxColor(FlowThemeVariables& t) {
  t.background = QStringLiteral("#ffffff");
  t.primaryColor = QStringLiteral("#cccccc");
  t.mainBkg = QStringLiteral("#ffffff");
  t.strokeWidth = 2;
  t.primaryBorderColor = mkBorder(t.primaryColor, false);
  t.fontFamily = QStringLiteral("\"Recursive Variable\", arial, sans-serif");
  t.fontSize = QStringLiteral("14px");
  t.nodeBorder = QStringLiteral("#28253D");
  t.tertiaryColor = QStringLiteral("#ffffff");
}

void applyReduxDarkColor(FlowThemeVariables& t) {
  // Mirrors redux-dark's constructor (the *-color variants add a borderColorArray
  // for ER diagrams; not flowchart-relevant, so the base fields match redux-dark).
  applyReduxDark(t);
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

// Family A (base/neo/neo-dark/redux/redux-dark/redux-color/redux-dark-color):
// the shared `||`-guarded derivation (darkMode always false for built-in
// themes). primaryTextColorDefault is the non-darkMode fallback ("#333" etc.).
void updateColorsFamilyA(FlowThemeVariables& t, const QString& primaryTextColorDefault,
                         bool nodeBorderFromBorder1) {
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
}

void finishCScale(FlowThemeVariables& t, bool darkMode,
                  bool darkColorLabels = false) {
  for (int i = 0; i < t.themeColorLimit; ++i) {
    assignIfEmpty(t.cScaleInv[i], invert(t.cScale[i]));
    assignIfEmpty(t.cScalePeer[i], darkMode ? lighten(t.cScale[i], 10)
                                            : darken(t.cScale[i], 10));
    assignIfEmpty(t.cScaleLabel[i], darkColorLabels ? darken(t.cScale[i], 75)
                                                     : t.primaryTextColor);
  }
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
  for (int i = 0; i < t.themeColorLimit; ++i)
    t.cScale[i] = darken(t.cScale[i], darkMode ? 75 : 25);
  finishCScale(t, false);
}

void updateNeoCScale(FlowThemeVariables& t) {
  const QString primary = QStringLiteral("#ECECFE");
  const QString secondary = QStringLiteral("#E9E9F1");
  populateAdjustedScale(t, primary, secondary, adjust(primary, {.h = 180.0, .l = 5.0}));
  for (int i = 0; i < t.themeColorLimit; ++i) t.cScale[i] = darken(t.cScale[i], 25);
  finishCScale(t, false);
}

void updateReduxCScale(FlowThemeVariables& t) {
  for (int i = 0; i < t.themeColorLimit; ++i) {
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
  for (int i = 0; i < t.themeColorLimit; ++i) {
    t.cScale[i] = darken(t.cScale[i], 10);  // UNCONDITIONAL — accumulates per call
    assignIfEmpty(t.cScalePeer[i], darken(t.cScale[i], 25));
    assignIfEmpty(t.cScaleInv[i], adjust(t.cScale[i], {.h = 180.0}));
  }
}

void updateColorsDefault(FlowThemeVariables& t) {
  updateDefaultForestCScale(t);
  for (int i = 0; i < t.themeColorLimit; ++i)
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
}

void updateColorsForest(FlowThemeVariables& t) {
  updateDefaultForestCScale(t);
  for (int i = 0; i < t.themeColorLimit; ++i)
    assignIfEmpty(t.cScaleLabel[i], QStringLiteral("black"));
  t.nodeBkg = t.mainBkg;
  t.nodeBorder = t.border1;
  t.clusterBkg = t.secondBkg;
  t.clusterBorder = t.border2;
  t.defaultLinkColor = t.lineColor;
  // forest keeps constructor titleColor (#333) and edgeLabelBackground (#e8e8e8).
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
  // cScale: dark sets cScale1..11 to literal hex (chunk lines 1309-1320
  // unconditionally), then the `||` block keeps them; cScale0 = primaryColor.
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
  for (int i = 0; i < t.themeColorLimit; ++i) {
    assignIfEmpty(t.cScaleInv[i], invert(t.cScale[i]));
    assignIfEmpty(t.cScalePeer[i], lighten(t.cScale[i], 10));
    assignIfEmpty(t.cScaleLabel[i], t.mainContrastColor);
  }
  // dark's final nodeBorder override (line 1502): nodeBorder = nodeBorder || "#999".
  assignIfEmpty(t.nodeBorder, QStringLiteral("#999"));
  // dark does NOT derive nodeTextColor (getStyles falls back to textColor).
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
}

void updateColors(FlowThemeId id, FlowThemeVariables& t) {
  switch (id) {
    case FlowThemeId::Base:
      updateColorsFamilyA(t, QStringLiteral("#333"), false); updateBaseCScale(t, false); break;
    case FlowThemeId::Neo:
      updateColorsFamilyA(t, QStringLiteral("#333"), false); updateNeoCScale(t); break;
    case FlowThemeId::NeoDark:
      updateColorsFamilyA(t, QStringLiteral("#333"), true); updateBaseCScale(t, false); break;
    case FlowThemeId::Redux:
      updateColorsFamilyA(t, QStringLiteral("#28253D"), false); updateReduxCScale(t); break;
    case FlowThemeId::ReduxDark:
      updateColorsFamilyA(t, QStringLiteral("#FFFFFF"), true); updateBaseCScale(t, false); break;
    case FlowThemeId::ReduxColor:
      updateColorsFamilyA(t, QStringLiteral("#28253D"), false); updateReduxColorCScale(t, false); break;
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
  if (key == QStringLiteral("THEME_COLOR_LIMIT")) return QString::number(themeColorLimit);
  for (int i = 0; i < 12; ++i) {
    if (key == QStringLiteral("cScale%1").arg(i)) return cScale[i];
    if (key == QStringLiteral("cScaleInv%1").arg(i)) return cScaleInv[i];
    if (key == QStringLiteral("cScalePeer%1").arg(i)) return cScalePeer[i];
    if (key == QStringLiteral("cScaleLabel%1").arg(i)) return cScaleLabel[i];
  }
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
  else if (key == QStringLiteral("THEME_COLOR_LIMIT")) themeColorLimit = value.toInt();
}

}  // namespace muffin::mermaid::flowtheme
