#pragma once

// Native port of mermaid 11.16.0's theme system (the 11 registered themes in
// dist/chunks/mermaid.esm/chunk-CHAKFXHA.mjs). Each theme is a `Theme` class
// with a constructor (raw field literals, some derived via khroma) + an
// `updateColors()` derivation + a `calculate(overrides)` two-pass driver.
//
// Scope: the shared fields consumed by the native Flowchart, Requirement, Pie,
// Quadrant, Journey, Radar, XYChart, Timeline, and Packet renderers, including all
// cScale/cScaleInv/cScalePeer/cScaleLabel palettes for all 11 themes.
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

// Mermaid stores XYChart colors in the nested themeVariables.xyChart object,
// rather than as top-level theme-variable scalars. updateColors() fills missing
// per-field values from the current theme; resolveFlowTheme's final override
// replay preserves explicit user values, including an empty string.
struct XYChartThemeVariables {
  QString backgroundColor;
  QString titleColor;
  QString dataLabelColor;
  QString xAxisTitleColor;
  QString xAxisLabelColor;
  QString xAxisTickColor;
  QString xAxisLineColor;
  QString yAxisTitleColor;
  QString yAxisLabelColor;
  QString yAxisTickColor;
  QString yAxisLineColor;
  QString plotColorPalette;
};

// Packet styles live in the nested themeVariables.packet object. Mermaid's
// packet stylesheet supplies these defaults whenever the selected theme does
// not define the object (all themes except dark and forest), or when a source
// override replaces the object and omits a field.
struct PacketThemeVariables {
  QString byteFontSize = QStringLiteral("10px");
  QString startByteColor = QStringLiteral("black");
  QString endByteColor = QStringLiteral("black");
  QString labelColor = QStringLiteral("black");
  QString labelFontSize = QStringLiteral("12px");
  QString titleColor = QStringLiteral("black");
  QString titleFontSize = QStringLiteral("14px");
  QString blockStrokeColor = QStringLiteral("black");
  QString blockStrokeWidth = QStringLiteral("1");
  QString blockFillColor = QStringLiteral("#efefef");
};

// Cynefin's stylesheet is driven by one nested theme object. Numeric-looking
// values remain strings here because source overrides are interpolated into
// CSS by JavaScript before browser used-value resolution.
struct CynefinThemeVariables {
  QString domainFontSize;
  QString itemFontSize;
  QString boundaryColor;
  QString boundaryWidth;
  QString cliffColor;
  QString cliffWidth;
  QString arrowColor;
  QString arrowWidth;
  QString complexBg;
  QString complicatedBg;
  QString chaoticBg;
  QString clearBg;
  QString confusionBg;
  QString textColor;
  QString labelColor;
};

// Wardley maps use a nested twelve-field theme object. Dark is the only
// built-in theme whose component/annotation fill and evolution accent differ
// from the common derivation.
struct WardleyThemeVariables {
  QString backgroundColor;
  QString axisColor;
  QString axisTextColor;
  QString gridColor;
  QString componentFill;
  QString componentStroke;
  QString componentLabelColor;
  QString linkStroke;
  QString evolutionStroke;
  QString annotationStroke;
  QString annotationTextColor;
  QString annotationFill;
};

// Parse a mermaid theme name ("default", "neo-dark", ...) → FlowThemeId.
// Unknown names map to Default (mermaid's default).
FlowThemeId parseThemeId(const QString& name);

// The native-renderer themeVariables subset. Colors are stored as the exact
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
  // Timeline's Redux stylesheet consumes this for node labels. Classic/Neo
  // themes resolve to "normal"; the four Redux variants resolve to "600".
  QString fontWeight;
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
  // GitGraph owns an eight-color branch palette, its inverse palette and the
  // branch-label palette. Mindmap's classic root consumes slot zero, so the
  // legacy scalar aliases remain synchronized with the first array entries.
  QString git0;
  QString gitBranchLabel0;
  QString git[8];
  QString gitInv[8];
  QString gitBranchLabel[8];
  QString commitLineColor;
  QString commitLabelColor;
  QString commitLabelBackground;
  QString commitLabelFontSize;
  QString tagLabelColor;
  QString tagLabelBackground;
  QString tagLabelBorder;
  QString tagLabelFontSize;
  QString mainContrastColor;  // dark + dark-variant FamilyA themes
  QString contrast;           // neutral theme only
  QString text;               // neutral theme only
  // taskTextDarkColor: pie title/legend text source for every theme but the
  // standalone dark (which uses mainContrastColor). Derived per-theme in
  // updateColors (base/FamilyA = textColor; default/forest = "black"; neutral =
  // text; dark = invert(mainContrastColor)). Exposed via get()/set() so a
  // source-entry override propagates to the pie title/legend.
  QString taskTextDarkColor;
  // Gantt themeVariables. These are top-level scalars in Mermaid's theme
  // model and are replayed after updateColors, so explicit source overrides
  // win over every derived default.
  QString sectionBkgColor;
  QString altSectionBkgColor;
  QString sectionBkgColor2;
  QString excludeBkgColor;
  QString taskBorderColor;
  QString taskBkgColor;
  QString taskTextColor;
  QString taskTextOutsideColor;
  QString taskTextLightColor;
  QString taskTextClickableColor;
  QString activeTaskBorderColor;
  QString activeTaskBkgColor;
  QString gridColor;
  QString doneTaskBkgColor;
  QString doneTaskBorderColor;
  QString critBorderColor;
  QString critBkgColor;
  QString todayLineColor;
  QString vertLineColor;
  qreal strokeWidth = 1.0;
  bool useGradient = true;
  QString gradientStart;
  QString gradientStop;
  QString shadowColor = QStringLiteral("#000000");
  qreal shadowOpacity = 0.25;
  qreal shadowOffsetX = 0.0;
  qreal shadowOffsetY = 1.0;
  QString dropShadow;
  int themeColorLimit = 12;

  // Architecture stylesheet variables. Widths stay as CSS strings until the
  // scene resolves their used values.
  QString archEdgeColor;
  QString archEdgeArrowColor;
  QString archEdgeWidth;
  QString archGroupBorderColor;
  QString archGroupBorderWidth;

  // Event Modeling uses fourteen top-level theme variables. All themes except
  // the standalone dark theme inherit the light literals; dark derives its
  // four colored boxes and swimlanes from its own background palette.
  QString emUiFill;
  QString emUiStroke;
  QString emProcessorFill;
  QString emProcessorStroke;
  QString emReadModelFill;
  QString emReadModelStroke;
  QString emCommandFill;
  QString emCommandStroke;
  QString emEventFill;
  QString emEventStroke;
  QString emSwimlaneBackgroundOdd;
  QString emSwimlaneBackgroundStroke;
  QString emArrowhead;
  QString emRelationStroke;

  // cScale palette: 13 slots = upstream cScale0..cScale12. dark defines
  // cScale12 = "#010029" unconditionally (so TCL=13 yields pie12 = cScale12);
  // every other theme leaves cScale12 empty. pie stays a 12-slot array
  // (pie1..pie12) and is capped separately when copied from cScale.
  QString cScale[13];
  QString cScaleInv[13];
  QString cScalePeer[13];
  QString cScaleLabel[13];

  // Journey section/task CSS palette (fillType0..fillType7). Most themes
  // derive this from primaryColor/secondaryColor; the light Neo/Redux family
  // instead uses its local #ECECFE/#E9E9F1 constants, matching upstream.
  QString fillType[8];

  // Pie family themeVariables (pieDiagram). Derived per-theme in updateColors
  // via MermaidColor::adjust from primaryColor/secondaryColor/tertiaryColor.
  QString pie[12];  // pie1..pie12 section fills (scaleOrdinal range)
  // Upstream themeVariable keys are *TextColor (NOT *TextFill); the repo
  // whitelist (MermaidConfigKeys.inc) and the golden confirm this.
  QString pieTitleTextColor;
  QString pieSectionTextColor;
  QString pieLegendTextColor;
  QString pieStrokeColor;
  QString pieStrokeWidth = QStringLiteral("2px");
  QString pieOpacity = QStringLiteral("0.7");
  QString pieOuterStrokeColor;
  QString pieOuterStrokeWidth = QStringLiteral("2px");
  QString pieTitleTextSize = QStringLiteral("25px");
  QString pieSectionTextSize = QStringLiteral("17px");
  QString pieLegendTextSize = QStringLiteral("17px");

  // Venn's eight-set palette is defined only by Base, Dark, Default, Forest,
  // and Neutral in Mermaid 11.16. The Neo/Redux family intentionally leaves
  // these slots unset; the renderer preserves the resulting CSS fallback.
  QString venn[8];
  QString vennTitleTextColor;
  QString vennSetTextColor;

  // Quadrant family themeVariables (quadrantChart). Derived per-theme in
  // updateColors via MermaidColor::adjust from primaryColor (RGB steps).
  QString quadrant[4];  // quadrant1..4Fill
  QString quadrantText[4];  // quadrant1..4TextFill
  QString quadrantPointFill;
  QString quadrantPointTextFill;
  QString quadrantXAxisTextFill;
  QString quadrantYAxisTextFill;
  QString quadrantInternalBorderStrokeFill;
  QString quadrantExternalBorderStrokeFill;
  QString quadrantTitleFill;

  // XYChart's 12-field nested theme object. The six Neo/Redux variants inherit
  // dataLabelColor from their base constructor even though their own
  // updateColors blocks omit it; populateXYChart preserves that 11.16 quirk.
  XYChartThemeVariables xyChart;

  // Packet's nested stylesheet object. Dark and Forest replace six colors in
  // updateColors; the other themes retain PacketStyleOptions defaults.
  PacketThemeVariables packet;

  // The fixed-layout Cynefin renderer consumes all fifteen nested fields.
  CynefinThemeVariables cynefin;

  // The fixed-coordinate Wardley renderer consumes all twelve nested fields.
  WardleyThemeVariables wardley;

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
