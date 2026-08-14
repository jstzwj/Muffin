#include "mermaid/theme/FlowTheme.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/MermaidPreprocessor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cstdlib>
#include <limits>

using namespace muffin::mermaid::flowtheme;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QString flowThemeIdName(FlowThemeId id) {
  switch (id) {
    case FlowThemeId::Base: return QStringLiteral("base");
    case FlowThemeId::Dark: return QStringLiteral("dark");
    case FlowThemeId::Default: return QStringLiteral("default");
    case FlowThemeId::Forest: return QStringLiteral("forest");
    case FlowThemeId::Neutral: return QStringLiteral("neutral");
    case FlowThemeId::Neo: return QStringLiteral("neo");
    case FlowThemeId::NeoDark: return QStringLiteral("neo-dark");
    case FlowThemeId::Redux: return QStringLiteral("redux");
    case FlowThemeId::ReduxDark: return QStringLiteral("redux-dark");
    case FlowThemeId::ReduxColor: return QStringLiteral("redux-color");
    case FlowThemeId::ReduxDarkColor: return QStringLiteral("redux-dark-color");
  }
  return QString();
}

// The flowchart-critical themeVariables fields, compared for every theme.
const QStringList criticalFields() {
  return {
      QStringLiteral("background"),   QStringLiteral("primaryColor"),
      QStringLiteral("secondaryColor"), QStringLiteral("tertiaryColor"),
      QStringLiteral("mainBkg"),      QStringLiteral("secondBkg"),
      QStringLiteral("lineColor"),    QStringLiteral("border1"),
      QStringLiteral("border2"),      QStringLiteral("arrowheadColor"),
      QStringLiteral("fontFamily"),   QStringLiteral("fontSize"),
      QStringLiteral("fontWeight"),
      QStringLiteral("labelBackground"), QStringLiteral("textColor"),
      QStringLiteral("titleColor"),   QStringLiteral("edgeLabelBackground"),
      QStringLiteral("requirementEdgeLabelBackground"),
      // Requirement getStyles() variables (all 11 themes derive them).
      QStringLiteral("requirementBackground"),
      QStringLiteral("requirementBorderColor"),
      QStringLiteral("requirementBorderSize"),
      QStringLiteral("requirementTextColor"),
      QStringLiteral("relationColor"),
      QStringLiteral("relationLabelBackground"),
      QStringLiteral("relationLabelColor"),
      QStringLiteral("actorTextColor"),
      QStringLiteral("clusterBkg"),   QStringLiteral("clusterBorder"),
      QStringLiteral("compositeBackground"), QStringLiteral("altBackground"),
      QStringLiteral("compositeTitleBackground"),
      QStringLiteral("primaryBorderColor"), QStringLiteral("primaryTextColor"),
      QStringLiteral("secondaryBorderColor"), QStringLiteral("secondaryTextColor"),
      QStringLiteral("tertiaryBorderColor"), QStringLiteral("tertiaryTextColor"),
      QStringLiteral("nodeTextColor"), QStringLiteral("nodeBkg"),
      QStringLiteral("nodeBorder"),   QStringLiteral("defaultLinkColor"),
      // State special shapes (stateStart/stateEnd rendering-util shapes):
      // specialStateColor is present in every theme's golden; stateBorder only
      // neutral/neo/redux* define (absent keys compare as empty).
      QStringLiteral("specialStateColor"), QStringLiteral("stateBorder"),
      QStringLiteral("stateBkg"),
      QStringLiteral("git0"),         QStringLiteral("gitBranchLabel0"),
      QStringLiteral("dropShadow"),
      QStringLiteral("strokeWidth"),  QStringLiteral("THEME_COLOR_LIMIT"),
  };
}

// Pie + Quadrant themeVariables are derived per-theme in updateColors for all
// 11 themes: Family-A (base/neo/neo-dark/redux/redux-dark/redux-color/
// redux-dark-color — populatePieFamilyA), Default (populatePieDefault), Forest
// (populatePieForest), and Dark/Neutral (populatePieFromCScale — pie mirrors
// cScale). Quadrant is uniform across all themes (populateQuadrant).
bool pieQuadrantImplemented(FlowThemeId id) {
  switch (id) {
    case FlowThemeId::Base:
    case FlowThemeId::Neo:
    case FlowThemeId::NeoDark:
    case FlowThemeId::Redux:
    case FlowThemeId::ReduxDark:
    case FlowThemeId::ReduxColor:
    case FlowThemeId::ReduxDarkColor:
    case FlowThemeId::Default:
    case FlowThemeId::Forest:
    case FlowThemeId::Neutral:
    case FlowThemeId::Dark:
      return true;
    default:
      return false;
  }
}

// The 20 pie/quadrant fields that populatePie*/populateQuadrant derive: the
// 12 section fills + 4 quadrant fills + 4 quadrant text fills.
QStringList pieQuadrantFields() {
  QStringList f;
  for (int i = 1; i <= 12; ++i) f.append(QStringLiteral("pie%1").arg(i));
  for (int i = 1; i <= 4; ++i) {
    f.append(QStringLiteral("quadrant%1Fill").arg(i));
    f.append(QStringLiteral("quadrant%1TextFill").arg(i));
  }
  return f;
}

QStringList journeyFields() {
  QStringList f;
  for (int i = 0; i < 8; ++i) f.append(QStringLiteral("fillType%1").arg(i));
  return f;
}

QStringList xyChartFields() {
  return {QStringLiteral("backgroundColor"),
          QStringLiteral("titleColor"),
          QStringLiteral("dataLabelColor"),
          QStringLiteral("xAxisTitleColor"),
          QStringLiteral("xAxisLabelColor"),
          QStringLiteral("xAxisTickColor"),
          QStringLiteral("xAxisLineColor"),
          QStringLiteral("yAxisTitleColor"),
          QStringLiteral("yAxisLabelColor"),
          QStringLiteral("yAxisTickColor"),
          QStringLiteral("yAxisLineColor"),
          QStringLiteral("plotColorPalette")};
}

QStringList packetFields() {
  return {QStringLiteral("byteFontSize"),
          QStringLiteral("startByteColor"),
          QStringLiteral("endByteColor"),
          QStringLiteral("labelColor"),
          QStringLiteral("labelFontSize"),
          QStringLiteral("titleColor"),
          QStringLiteral("titleFontSize"),
          QStringLiteral("blockStrokeColor"),
          QStringLiteral("blockStrokeWidth"),
          QStringLiteral("blockFillColor")};
}

QHash<QString, QString> sourceThemeOverrides(const QString& source);

QStringList eventModelingFields() {
  return {QStringLiteral("emUiFill"),
          QStringLiteral("emUiStroke"),
          QStringLiteral("emProcessorFill"),
          QStringLiteral("emProcessorStroke"),
          QStringLiteral("emReadModelFill"),
          QStringLiteral("emReadModelStroke"),
          QStringLiteral("emCommandFill"),
          QStringLiteral("emCommandStroke"),
          QStringLiteral("emEventFill"),
          QStringLiteral("emEventStroke"),
          QStringLiteral("emSwimlaneBackgroundOdd"),
          QStringLiteral("emSwimlaneBackgroundStroke"),
          QStringLiteral("emArrowhead"),
          QStringLiteral("emRelationStroke")};
}

void checkEventModelingThemeOverrides() {
  QHash<QString, QString> direct;
  int index = 1;
  for (const QString& field : eventModelingFields())
    direct.insert(field, QStringLiteral("#%1%1%1").arg(index++, 2, 16,
                                                        QLatin1Char('0')));
  const FlowThemeVariables directTheme =
      resolveFlowTheme(FlowThemeId::Default, direct);
  for (auto it = direct.cbegin(); it != direct.cend(); ++it)
    require(directTheme.get(it.key()) == it.value(),
            QStringLiteral("Event Modeling direct override lost %1").arg(it.key()));

  const QString initSource = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"emUiFill\": \"#123456\", "
      "\"emRelationStroke\": \"#654321\", "
      "\"emArrowhead\": \"#abcdef\"}}}%%\n"
      "eventmodeling\ntf 1 evt Created");
  const FlowThemeVariables initTheme = resolveFlowTheme(
      FlowThemeId::Default, sourceThemeOverrides(initSource));
  require(initTheme.emUiFill == QLatin1String("#123456") &&
              initTheme.emRelationStroke == QLatin1String("#654321") &&
              initTheme.emArrowhead == QLatin1String("#abcdef"),
          QStringLiteral("Event Modeling init theme overrides drifted"));

  const QString frontmatterSource = QStringLiteral(
      "---\nconfig:\n  themeVariables:\n    emCommandFill: '#102030'\n"
      "    emCommandStroke: '#405060'\n---\n"
      "eventmodeling\ntf 1 cmd Submit");
  const FlowThemeVariables frontmatterTheme = resolveFlowTheme(
      FlowThemeId::Default, sourceThemeOverrides(frontmatterSource));
  require(frontmatterTheme.emCommandFill == QLatin1String("#102030") &&
              frontmatterTheme.emCommandStroke == QLatin1String("#405060"),
          QStringLiteral("Event Modeling frontmatter theme overrides drifted"));
}

void checkRequirementEdgeLabelBackgroundOptional() {
  const FlowThemeVariables redux = resolveFlowTheme(FlowThemeId::Redux);
  require(redux.requirementEdgeLabelBackground.has_value() &&
              *redux.requirementEdgeLabelBackground == QLatin1String("#FFFFFF"),
          QStringLiteral("Redux requirementEdgeLabelBackground"));
  const FlowThemeVariables reduxDark = resolveFlowTheme(FlowThemeId::ReduxDark);
  require(reduxDark.requirementEdgeLabelBackground.has_value() &&
              *reduxDark.requirementEdgeLabelBackground == QLatin1String("#16141F"),
          QStringLiteral("ReduxDark requirementEdgeLabelBackground"));
  for (FlowThemeId id : {FlowThemeId::Base, FlowThemeId::Dark,
                         FlowThemeId::Default, FlowThemeId::Forest,
                         FlowThemeId::Neutral, FlowThemeId::Neo,
                         FlowThemeId::NeoDark, FlowThemeId::ReduxColor,
                         FlowThemeId::ReduxDarkColor}) {
    require(!resolveFlowTheme(id).requirementEdgeLabelBackground.has_value(),
            QStringLiteral("Unexpected requirementEdgeLabelBackground: %1")
                .arg(flowThemeIdName(id)));
  }
  QHash<QString, QString> explicitEmpty;
  explicitEmpty.insert(QStringLiteral("requirementEdgeLabelBackground"),
                       QString());
  const FlowThemeVariables empty = resolveFlowTheme(
      FlowThemeId::Default, explicitEmpty);
  require(empty.requirementEdgeLabelBackground.has_value() &&
              empty.requirementEdgeLabelBackground->isEmpty(),
          QStringLiteral("Explicit empty requirementEdgeLabelBackground"));
}

void checkWardleyThemeOverrides() {
  const QStringList fields = {
      QStringLiteral("backgroundColor"), QStringLiteral("axisColor"),
      QStringLiteral("axisTextColor"), QStringLiteral("gridColor"),
      QStringLiteral("componentFill"), QStringLiteral("componentStroke"),
      QStringLiteral("componentLabelColor"), QStringLiteral("linkStroke"),
      QStringLiteral("evolutionStroke"), QStringLiteral("annotationStroke"),
      QStringLiteral("annotationTextColor"), QStringLiteral("annotationFill")};
  QHash<QString, QString> direct;
  for (qsizetype i = 0; i < fields.size(); ++i)
    direct.insert(QStringLiteral("wardley.") + fields.at(i),
                  QStringLiteral("#%1a0b").arg(i + 1, 2, 16,
                                                   QLatin1Char('0')));
  const FlowThemeVariables directTheme =
      resolveFlowTheme(FlowThemeId::Default, direct);
  for (auto it = direct.cbegin(); it != direct.cend(); ++it)
    require(directTheme.get(it.key()) == it.value(),
            QStringLiteral("Wardley direct override lost %1").arg(it.key()));

  const QString initSource = QStringLiteral(
      "%%{init:{\"themeVariables\":{\"wardley\":{"
      "\"backgroundColor\":\"#123456\","
      "\"evolutionStroke\":\"#654321\","
      "\"annotationStroke\":\"#abcdef\","
      "\"annotationTextColor\":\"#fedcba\","
      "\"annotationFill\":\"#102030\"}}}}%%\n"
      "wardley-beta\ncomponent A [0.5,0.5]");
  const FlowThemeVariables sourceTheme = resolveFlowTheme(
      FlowThemeId::Default, sourceThemeOverrides(initSource));
  require(sourceTheme.wardley.backgroundColor == QLatin1String("#123456") &&
              sourceTheme.wardley.evolutionStroke == QLatin1String("#654321") &&
              sourceTheme.wardley.annotationStroke == QLatin1String("#333333") &&
              sourceTheme.wardley.annotationTextColor == QLatin1String("#131300") &&
              sourceTheme.wardley.annotationFill == QLatin1String("white"),
          QStringLiteral("Wardley source-entry theme sanitizer drifted"));
}

QStringList ganttFields() {
  return {QStringLiteral("sectionBkgColor"),
          QStringLiteral("altSectionBkgColor"),
          QStringLiteral("sectionBkgColor2"),
          QStringLiteral("excludeBkgColor"),
          QStringLiteral("taskBorderColor"),
          QStringLiteral("taskBkgColor"),
          QStringLiteral("taskTextColor"),
          QStringLiteral("taskTextDarkColor"),
          QStringLiteral("taskTextOutsideColor"),
          QStringLiteral("taskTextLightColor"),
          QStringLiteral("taskTextClickableColor"),
          QStringLiteral("activeTaskBorderColor"),
          QStringLiteral("activeTaskBkgColor"),
          QStringLiteral("gridColor"),
          QStringLiteral("doneTaskBkgColor"),
          QStringLiteral("doneTaskBorderColor"),
          QStringLiteral("critBorderColor"),
          QStringLiteral("critBkgColor"),
          QStringLiteral("todayLineColor"),
          QStringLiteral("vertLineColor")};
}

QJsonObject packetStyleDefaults() {
  return {{QStringLiteral("byteFontSize"), QStringLiteral("10px")},
          {QStringLiteral("startByteColor"), QStringLiteral("black")},
          {QStringLiteral("endByteColor"), QStringLiteral("black")},
          {QStringLiteral("labelColor"), QStringLiteral("black")},
          {QStringLiteral("labelFontSize"), QStringLiteral("12px")},
          {QStringLiteral("titleColor"), QStringLiteral("black")},
          {QStringLiteral("titleFontSize"), QStringLiteral("14px")},
          {QStringLiteral("blockStrokeColor"), QStringLiteral("black")},
          {QStringLiteral("blockStrokeWidth"), QStringLiteral("1")},
          {QStringLiteral("blockFillColor"), QStringLiteral("#efefef")}};
}

// Pie + Quadrant SCALAR themeVariables (uniform formulas across themes): the
// dependency fields (taskTextDarkColor, mainContrastColor) that the pie text
// colors derive from, the pie stroke/opacity/sizes, and the quadrant point/axis/
// title/border scalars. Compared for every implemented theme.
QStringList pieQuadrantScalarFields() {
  return {QStringLiteral("taskTextDarkColor"), QStringLiteral("mainContrastColor"),
          QStringLiteral("pieTitleTextColor"), QStringLiteral("pieSectionTextColor"),
          QStringLiteral("pieLegendTextColor"), QStringLiteral("pieStrokeColor"),
          QStringLiteral("pieStrokeWidth"), QStringLiteral("pieOuterStrokeColor"),
          QStringLiteral("pieOuterStrokeWidth"), QStringLiteral("pieOpacity"),
          QStringLiteral("pieTitleTextSize"), QStringLiteral("pieSectionTextSize"),
          QStringLiteral("pieLegendTextSize"), QStringLiteral("quadrantPointFill"),
          QStringLiteral("quadrantPointTextFill"), QStringLiteral("quadrantXAxisTextFill"),
          QStringLiteral("quadrantYAxisTextFill"), QStringLiteral("quadrantTitleFill"),
          QStringLiteral("quadrantInternalBorderStrokeFill"),
          QStringLiteral("quadrantExternalBorderStrokeFill")};
}

QStringList fieldsForTheme(FlowThemeId id) {
  QStringList f = criticalFields();
  for (int i = 0; i <= 11; ++i) {
    f.append(QStringLiteral("cScale%1").arg(i));
    f.append(QStringLiteral("cScalePeer%1").arg(i));
    f.append(QStringLiteral("cScaleInv%1").arg(i));
    f.append(QStringLiteral("cScaleLabel%1").arg(i));
  }
  // cScale12: dark defines "#010029" UNCONDITIONALLY (present even at TCL=12);
  // every other theme leaves it empty at the default TCL. (At abnormal TCL redux
  // etc. may also derive cScale12; cScaleInv/Peer/Label[12] are TCL-gated and
  // unset at TCL=12 for everyone, so they are not part of the golden.)
  f.append(QStringLiteral("cScale12"));
  f.append(journeyFields());
  f.append(ganttFields());
  for (int i = 1; i <= 8; ++i)
    f.append(QStringLiteral("venn%1").arg(i));
  f.append(QStringLiteral("vennTitleTextColor"));
  f.append(QStringLiteral("vennSetTextColor"));
  if (pieQuadrantImplemented(id)) {
    f.append(pieQuadrantFields());
    f.append(pieQuadrantScalarFields());
  }
  return f;
}

// Golden values may be strings or numbers; normalise to a comparable string.
QString goldenToString(const QJsonValue& v) {
  if (v.isDouble()) {
    const double d = v.toDouble();
    if (d == std::floor(d)) return QString::number(static_cast<qint64>(d));
    return QString::number(d);
  }
  return v.toString();
}

void compareTheme(FlowThemeId id, const QJsonObject& goldenVars, const QString& label) {
  const FlowThemeVariables t = resolveFlowTheme(id);
  for (const QString& key : fieldsForTheme(id)) {
    const QString native = t.get(key);
    const QString golden = goldenToString(goldenVars.value(key));
    if (native != golden) {
      fail(QStringLiteral("Theme %1/%2 %3 mismatch: native=%4 golden=%5")
               .arg(label, flowThemeIdName(id), key, native, golden));
    }
  }
  QJsonObject expectedPacket = packetStyleDefaults();
  const QJsonObject upstreamPacket =
      goldenVars.value(QStringLiteral("packet")).toObject();
  for (auto it = upstreamPacket.constBegin(); it != upstreamPacket.constEnd(); ++it)
    expectedPacket.insert(it.key(), it.value());
  for (const QString& field : packetFields()) {
    const QString native = t.get(QStringLiteral("packet.") + field);
    const QString golden = goldenToString(expectedPacket.value(field));
    require(native == golden,
            QStringLiteral("Packet theme %1/%2 mismatch: native=%3 golden=%4")
                .arg(label, field, native, golden));
  }
}

void compareOverride(FlowThemeId id, const QHash<QString, QString>& overrides,
                      const QJsonObject& goldenVars, const QString& label) {
  const FlowThemeVariables t = resolveFlowTheme(id, overrides);
  QStringList fields = criticalFields();
  fields.append(journeyFields());
  for (const QString& key : fields) {
    const QString native = t.get(key);
    const QString golden = goldenToString(goldenVars.value(key));
    if (native != golden) {
      fail(QStringLiteral("Override %1/%2 %3 mismatch: native=%4 golden=%5")
               .arg(label, flowThemeIdName(id), key, native, golden));
    }
  }
}

void compareXYChart(const FlowThemeVariables& theme,
                    const QJsonObject& expected, const QString& label) {
  for (const QString& field : xyChartFields()) {
    const QString actual = theme.get(QStringLiteral("xyChart.") + field);
    const QString wanted = goldenToString(expected.value(field));
    require(actual == wanted,
            QStringLiteral("XYChart theme %1/%2 mismatch: native=%3 golden=%4")
                .arg(label, field, actual, wanted));
  }
}

QHash<QString, QString> sourceThemeOverrides(const QString& source) {
  return muffin::mermaid::editor::themeOverrides(
      muffin::mermaid::preprocessDiagram(source).config);
}

void checkPacketThemeOverrides() {
  const QString source = QStringLiteral(
      "%%{init:{\"theme\":\"dark\",\"themeVariables\":{\"packet\":{"
      "\"labelColor\":\"#ff0000\",\"labelFontSize\":\"22px\","
      "\"titleColor\":\"#00ff00\",\"titleFontSize\":\"24px\","
      "\"byteFontSize\":\"30px\",\"startByteColor\":\"#111111\","
      "\"endByteColor\":\"#222222\",\"blockStrokeColor\":\"#333333\","
      "\"blockStrokeWidth\":\"7\",\"blockFillColor\":\"#444444\"}}}}%%\n"
      "packet-beta\n0: x");
  const FlowThemeVariables sourceTheme =
      resolveFlowTheme(FlowThemeId::Dark, sourceThemeOverrides(source));
  require(sourceTheme.packet.labelColor == QLatin1String("#ff0000") &&
              sourceTheme.packet.labelFontSize == QLatin1String("22px") &&
              sourceTheme.packet.titleColor == QLatin1String("#00ff00") &&
              sourceTheme.packet.titleFontSize == QLatin1String("24px"),
          QStringLiteral("Packet's four source-reachable style fields drifted"));
  require(sourceTheme.packet.byteFontSize == QLatin1String("10px") &&
              sourceTheme.packet.startByteColor == QLatin1String("black") &&
              sourceTheme.packet.endByteColor == QLatin1String("black") &&
              sourceTheme.packet.blockStrokeColor == QLatin1String("black") &&
              sourceTheme.packet.blockStrokeWidth == QLatin1String("1") &&
              sourceTheme.packet.blockFillColor == QLatin1String("#efefef"),
          QStringLiteral("Packet source sanitizer/API-only fields drifted"));

  const FlowThemeVariables emptyReplacement = resolveFlowTheme(
      FlowThemeId::Forest,
      sourceThemeOverrides(QStringLiteral(
          "---\nconfig:\n  theme: forest\n  themeVariables:\n    packet: {}\n---\n"
          "packet-beta\n0: x")));
  require(emptyReplacement.packet.startByteColor == QLatin1String("black") &&
              emptyReplacement.packet.blockFillColor == QLatin1String("#efefef"),
          QStringLiteral("Packet empty-object replacement retained Forest colors"));

  QHash<QString, QString> direct;
  direct.insert(QStringLiteral("packet.byteFontSize"), QStringLiteral("18px"));
  direct.insert(QStringLiteral("packet.blockFillColor"), QStringLiteral("#abcdef"));
  const FlowThemeVariables directTheme =
      resolveFlowTheme(FlowThemeId::Default, direct);
  require(directTheme.get(QStringLiteral("packet.byteFontSize")) ==
                  QLatin1String("18px") &&
              directTheme.get(QStringLiteral("packet.blockFillColor")) ==
                  QLatin1String("#abcdef"),
          QStringLiteral("Packet typed get/set round-trip drifted"));
}

void checkFontWeightOverrides() {
  QHash<QString, QString> direct;
  direct.insert(QStringLiteral("fontWeight"), QStringLiteral("800"));
  require(resolveFlowTheme(FlowThemeId::Redux, direct).fontWeight ==
              QLatin1String("800"),
          QStringLiteral("Direct fontWeight override did not win"));

  const QString initSource = QStringLiteral(
      "%%{init: {\"theme\":\"redux\",\"themeVariables\":{\"fontWeight\":800}}}%%\n"
      "timeline\nTask");
  require(resolveFlowTheme(FlowThemeId::Redux,
                           sourceThemeOverrides(initSource)).fontWeight ==
              QLatin1String("800"),
          QStringLiteral("Source-entry fontWeight override did not win"));

  const QString frontmatterSource = QStringLiteral(
      "---\nconfig:\n  theme: redux\n  themeVariables:\n"
      "    fontWeight: 800\n---\ntimeline\nTask");
  require(resolveFlowTheme(FlowThemeId::Redux,
                           sourceThemeOverrides(frontmatterSource)).fontWeight ==
              QLatin1String("800"),
          QStringLiteral("Frontmatter fontWeight override did not win"));
}

void checkXYChartThemes(const QJsonObject& fixture) {
  require(fixture.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("XYChart theme fixture version drifted"));
  const QJsonArray declaredFields = fixture.value(QStringLiteral("fields")).toArray();
  require(declaredFields.size() == xyChartFields().size(),
          QStringLiteral("XYChart theme field count drifted"));
  for (int i = 0; i < declaredFields.size(); ++i)
    require(declaredFields.at(i).toString() == xyChartFields().at(i),
            QStringLiteral("XYChart theme field order drifted at %1").arg(i));

  const QJsonObject themes = fixture.value(QStringLiteral("themes")).toObject();
  require(themes.size() == 11,
          QStringLiteral("XYChart fixture must cover all 11 themes"));
  for (auto it = themes.constBegin(); it != themes.constEnd(); ++it) {
    const FlowThemeId id = parseThemeId(it.key());
    require(flowThemeIdName(id) == it.key(),
            QStringLiteral("Unknown XYChart fixture theme: %1").arg(it.key()));
    compareXYChart(resolveFlowTheme(id), it.value().toObject(), it.key());
  }

  const QJsonObject cases = fixture.value(QStringLiteral("overrides")).toObject();
  QHash<QString, QString> direct;
  direct.insert(QStringLiteral("xyChart.titleColor"),
                QStringLiteral("#123456"));
  compareXYChart(resolveFlowTheme(FlowThemeId::Default, direct),
                 cases.value(QStringLiteral("initializeSparse")).toObject(),
                 QStringLiteral("initializeSparse"));

  const QString initSource = QStringLiteral(
      "%%{init: {\"themeVariables\":{\"xyChart\":{\"titleColor\":\"#123456\"}}}}%%\n"
      "xychart-beta\nbar [1]");
  compareXYChart(resolveFlowTheme(FlowThemeId::Default,
                                  sourceThemeOverrides(initSource)),
                 cases.value(QStringLiteral("sourceSparse")).toObject(),
                 QStringLiteral("sourceSparse"));

  const QString frontmatterSource = QStringLiteral(
      "---\nconfig:\n  themeVariables:\n    xyChart:\n"
      "      titleColor: \"#123456\"\n---\n"
      "xychart-beta\nbar [1]");
  compareXYChart(resolveFlowTheme(FlowThemeId::Default,
                                  sourceThemeOverrides(frontmatterSource)),
                 cases.value(QStringLiteral("frontmatterSparse")).toObject(),
                 QStringLiteral("frontmatterSparse"));

  const QString dependencySource = QStringLiteral(
      "%%{init: {\"themeVariables\":{\"primaryTextColor\":\"#654321\"}}}%%\n"
      "xychart-beta\nbar [1]");
  compareXYChart(resolveFlowTheme(FlowThemeId::Default,
                                  sourceThemeOverrides(dependencySource)),
                 cases.value(QStringLiteral("dependency")).toObject(),
                 QStringLiteral("dependency"));

  const QString emptySource = QStringLiteral(
      "%%{init: {\"themeVariables\":{\"xyChart\":{\"titleColor\":\"\"}}}}%%\n"
      "xychart-beta\nbar [1]");
  compareXYChart(resolveFlowTheme(FlowThemeId::Default,
                                  sourceThemeOverrides(emptySource)),
                 cases.value(QStringLiteral("sourceEmpty")).toObject(),
                 QStringLiteral("sourceEmpty"));
}

void checkJourneyFillTypeOverrides() {
  const QString dynamicExpected[8] = {
      QStringLiteral("#ff0000"), QStringLiteral("#00ff00"),
      QStringLiteral("hsl(64, 100%, 50%)"), QStringLiteral("hsl(184, 100%, 50%)"),
      QStringLiteral("hsl(-64, 100%, 50%)"), QStringLiteral("hsl(56, 100%, 50%)"),
      QStringLiteral("hsl(128, 100%, 50%)"), QStringLiteral("hsl(248, 100%, 50%)"),
  };
  const QString lightExpected[8] = {
      QStringLiteral("#ECECFE"), QStringLiteral("#E9E9F1"),
      QStringLiteral("hsl(304, 90%, 96.0784313725%)"),
      QStringLiteral("hsl(304, 22.2222222222%, 92.9411764706%)"),
      QStringLiteral("hsl(176, 90%, 96.0784313725%)"),
      QStringLiteral("hsl(176, 22.2222222222%, 92.9411764706%)"),
      QStringLiteral("hsl(8, 90%, 96.0784313725%)"),
      QStringLiteral("hsl(8, 22.2222222222%, 92.9411764706%)"),
  };
  QHash<QString, QString> paletteOverrides;
  paletteOverrides.insert(QStringLiteral("primaryColor"), QStringLiteral("#ff0000"));
  paletteOverrides.insert(QStringLiteral("secondaryColor"), QStringLiteral("#00ff00"));

  const FlowThemeId dynamicThemes[] = {
      FlowThemeId::Base, FlowThemeId::Dark, FlowThemeId::Default, FlowThemeId::Forest,
      FlowThemeId::Neutral, FlowThemeId::NeoDark, FlowThemeId::ReduxDark,
      FlowThemeId::ReduxDarkColor,
  };
  for (const FlowThemeId id : dynamicThemes) {
    const FlowThemeVariables t = resolveFlowTheme(id, paletteOverrides);
    for (int i = 0; i < 8; ++i) {
      require(t.get(QStringLiteral("fillType%1").arg(i)) == dynamicExpected[i],
              QStringLiteral("journey dynamic palette %1 fillType%2")
                  .arg(flowThemeIdName(id))
                  .arg(i));
    }
  }

  const FlowThemeId localLightThemes[] = {
      FlowThemeId::Neo, FlowThemeId::Redux, FlowThemeId::ReduxColor,
  };
  for (const FlowThemeId id : localLightThemes) {
    const FlowThemeVariables t = resolveFlowTheme(id, paletteOverrides);
    for (int i = 0; i < 8; ++i) {
      require(t.get(QStringLiteral("fillType%1").arg(i)) == lightExpected[i],
              QStringLiteral("journey local-light palette %1 fillType%2")
                  .arg(flowThemeIdName(id))
                  .arg(i));
    }
  }

  for (const FlowThemeId id : {FlowThemeId::Base, FlowThemeId::Default}) {
    QHash<QString, QString> direct;
    direct.insert(QStringLiteral("fillType0"), QStringLiteral("#123456"));
    direct.insert(QStringLiteral("fillType7"), QStringLiteral("#abcdef"));
    FlowThemeVariables t = resolveFlowTheme(id, direct);
    require(t.get(QStringLiteral("fillType0")) == QLatin1String("#123456"),
            QStringLiteral("journey direct fillType0 override %1").arg(flowThemeIdName(id)));
    require(t.get(QStringLiteral("fillType7")) == QLatin1String("#abcdef"),
            QStringLiteral("journey direct fillType7 override %1").arg(flowThemeIdName(id)));

    direct.insert(QStringLiteral("fillType0"), QString());
    t = resolveFlowTheme(id, direct);
    require(t.get(QStringLiteral("fillType0")).isEmpty(),
            QStringLiteral("journey empty fillType0 override %1").arg(flowThemeIdName(id)));
  }
}

// Theme-MODEL override round-trip for the pie text-color themeVariables
// (resolveFlowTheme(overrides) -> get()). Upstream keys are *TextColor (NOT
// *TextFill); each must round-trip through get()/set() so that a user override
// wins over the derived value. This guards against the prior regression where
// the keys were wired to nonexistent *TextFill names and every override was
// silently dropped. NOTE: this verifies the theme MODEL only -- it does NOT
// assert the source-entry PRODUCTION path. The production Pie adapter
// (PieDiagramAdapter) still consumes its own style keys (titleColor /
// primaryTextColor) and is wired to these *TextColor themeVariables in a later
// commit; until then the keys are correct in the model but not yet read by the
// renderer.
void checkPieTextColorOverrides() {
  const struct { QString key; QString val; } cases[3] = {
    {QStringLiteral("pieTitleTextColor"), QStringLiteral("#a1b2c3")},
    {QStringLiteral("pieSectionTextColor"), QStringLiteral("#4d5e6f")},
    {QStringLiteral("pieLegendTextColor"), QStringLiteral("#778899")},
  };
  for (const auto& c : cases) {
    QHash<QString, QString> ov;
    ov.insert(c.key, c.val);
    const FlowThemeVariables t = resolveFlowTheme(FlowThemeId::Default, ov);
    const QString got = t.get(c.key);
    require(got == c.val,
            QStringLiteral("pie text-color override %1 = %2 (expected %3)").arg(c.key, got, c.val));
  }
}

// Theme-MODEL override round-trip for the dynamic-array pie/quadrant keys
// (pie%N / quadrant%NFill): exercises the looped set()/get() branches, which
// are distinct from the scalar *TextColor keys above. Like the scalar check,
// this verifies the model (resolveFlowTheme) override path, not the production
// adapter. resolveFlowTheme re-applies overrides after the final updateColors,
// so the user value must win over the derived palette entry.
void checkPieQuadrantDynamicOverrides() {
  const struct { QString key; QString val; } cases[2] = {
    {QStringLiteral("pie1"), QStringLiteral("#abcdef")},
    {QStringLiteral("quadrant1Fill"), QStringLiteral("#112233")},
  };
  for (const auto& c : cases) {
    QHash<QString, QString> ov;
    ov.insert(c.key, c.val);
    const FlowThemeVariables t = resolveFlowTheme(FlowThemeId::Default, ov);
    const QString got = t.get(c.key);
    require(got == c.val,
            QStringLiteral("dynamic pie/quadrant override %1 = %2 (expected %3)").arg(c.key, got, c.val));
  }
}

// The indexed cScale families share looped get()/set() branches. Exercise both
// ends of the base palette and one entry from every companion palette so a
// missing setter cannot be hidden by the theme's derived defaults.
void checkCScaleDynamicOverrides() {
  const struct { QString key; QString value; } cases[] = {
      {QStringLiteral("cScale0"), QStringLiteral("#010203")},
      {QStringLiteral("cScale12"), QStringLiteral("#121314")},
      {QStringLiteral("cScaleInv0"), QStringLiteral("#212223")},
      {QStringLiteral("cScalePeer0"), QStringLiteral("#313233")},
      {QStringLiteral("cScaleLabel0"), QStringLiteral("#414243")},
  };
  for (const auto& item : cases) {
    QHash<QString, QString> overrides;
    overrides.insert(item.key, item.value);
    const FlowThemeVariables theme =
        resolveFlowTheme(FlowThemeId::Default, overrides);
    require(theme.get(item.key) == item.value,
            QStringLiteral("dynamic cScale override failed: %1")
                .arg(item.key));
  }
}

void checkMindmapRootOverrides() {
  QHash<QString, QString> overrides;
  overrides.insert(QStringLiteral("git0"), QStringLiteral("#102030"));
  overrides.insert(QStringLiteral("gitBranchLabel0"),
                   QStringLiteral("#f0e0d0"));
  const FlowThemeVariables theme =
      resolveFlowTheme(FlowThemeId::Default, overrides);
  require(theme.git0 == QLatin1String("#102030") &&
              theme.gitBranchLabel0 == QLatin1String("#f0e0d0"),
          QStringLiteral("Mindmap root theme overrides did not win"));
}

void checkKanbanNeoShadows() {
  struct ExpectedShadow {
    FlowThemeId id;
    QString color;
    qreal opacity;
    qreal x;
    qreal y;
  };
  const ExpectedShadow cases[] = {
      {FlowThemeId::Base, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0},
      {FlowThemeId::Dark, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0},
      {FlowThemeId::Default, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0},
      {FlowThemeId::Forest, QStringLiteral("#b9b9b9"), 0.5, 1.0, 2.0},
      {FlowThemeId::Neutral, QStringLiteral("#b9b9b9"), 1.0, 1.0, 2.0},
      {FlowThemeId::Neo, QStringLiteral("#000000"), 0.25, 0.0, 1.0},
      {FlowThemeId::NeoDark, QStringLiteral("#b9b9b9"), 0.2, 1.0, 2.0},
      {FlowThemeId::Redux, QStringLiteral("#000000"), 0.06, 4.0, 4.0},
      {FlowThemeId::ReduxDark, QStringLiteral("#ffffff"), 0.06, 4.0, 4.0},
      {FlowThemeId::ReduxColor, QStringLiteral("#000000"), 0.06, 4.0, 4.0},
      {FlowThemeId::ReduxDarkColor, QStringLiteral("#ffffff"), 0.06, 4.0, 4.0},
  };
  for (const ExpectedShadow& expected : cases) {
    const FlowThemeVariables theme = resolveFlowTheme(expected.id);
    require(theme.shadowColor == expected.color &&
                theme.shadowOpacity == expected.opacity &&
                theme.shadowOffsetX == expected.x &&
                theme.shadowOffsetY == expected.y,
            QStringLiteral("Kanban neo drop-shadow drifted for %1")
                .arg(flowThemeIdName(expected.id)));
  }
}

// THEME_COLOR_LIMIT controls how many pie slices the cScale-derived themes
// (dark, neutral) populate. Upstream: `for i<TCL: this["pie"+i]=this["cScale"+i]`
// (0-based keys), renderer reads pie1..pie12, so pieK = cScaleK for K=1..TCL-1.
// Probed vs mermaid 11.16.0 (scripts/probe_mermaid_pie_tcl.mjs): dark is robust
// across TCL=0/1/2/12/13; neutral is defined at TCL=2/12 (crashes upstream at
// 0/1/13, exercised only for no-crash in checkTclNoOverflow). dark pie12 is
// EMPTY at TCL<=12 (the loop writes pie0..pie11 only) but is cScale12="#010029"
// at TCL>=13 (dark defines cScale12 unconditionally, and the loop reaches i=12).
void checkPieTclDistribution() {
  const auto resolveAt = [](FlowThemeId id, int tcl) {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("THEME_COLOR_LIMIT"), QString::number(tcl));
    return resolveFlowTheme(id, ov);
  };
  const auto allPieEmpty = [](const FlowThemeVariables& t) {
    for (int i = 0; i < 12; ++i)
      if (!t.pie[i].isEmpty()) return false;
    return true;
  };

  // Dark (probed upstream-defined at TCL=0/1/2/12).
  require(allPieEmpty(resolveAt(FlowThemeId::Dark, 0)), "dark TCL=0 pie not all empty");
  require(allPieEmpty(resolveAt(FlowThemeId::Dark, 1)), "dark TCL=1 pie not all empty");
  {
    const FlowThemeVariables t = resolveAt(FlowThemeId::Dark, 2);
    require(t.pie[0] == QLatin1String("#0b0000"), "dark TCL=2 pie1");
    for (int i = 1; i < 12; ++i) require(t.pie[i].isEmpty(), "dark TCL=2 tail not empty");
  }
  {
    const FlowThemeVariables t = resolveAt(FlowThemeId::Dark, 12);
    const QString expected[11] = {
        QStringLiteral("#0b0000"), QStringLiteral("#4d1037"), QStringLiteral("#3f5258"),
        QStringLiteral("#4f2f1b"), QStringLiteral("#6e0a0a"), QStringLiteral("#3b0048"),
        QStringLiteral("#995a01"), QStringLiteral("#154706"), QStringLiteral("#161722"),
        QStringLiteral("#00296f"), QStringLiteral("#01629c")};
    for (int i = 0; i < 11; ++i)
      require(t.pie[i] == expected[i], QStringLiteral("dark TCL=12 pie%1").arg(i + 1));
    require(t.pie[11].isEmpty(), "dark TCL=12 pie12 not empty");
  }
  {
    // TCL=13: upstream writes pie0..pie12; renderer pie1..pie12 = cScale1..12.
    // cScale12 = #010029 (dark's unconditional literal), so pie12 is SET here.
    const FlowThemeVariables t = resolveAt(FlowThemeId::Dark, 13);
    const QString expected[12] = {
        QStringLiteral("#0b0000"), QStringLiteral("#4d1037"), QStringLiteral("#3f5258"),
        QStringLiteral("#4f2f1b"), QStringLiteral("#6e0a0a"), QStringLiteral("#3b0048"),
        QStringLiteral("#995a01"), QStringLiteral("#154706"), QStringLiteral("#161722"),
        QStringLiteral("#00296f"), QStringLiteral("#01629c"), QStringLiteral("#010029")};
    for (int i = 0; i < 12; ++i)
      require(t.pie[i] == expected[i], QStringLiteral("dark TCL=13 pie%1").arg(i + 1));
  }

  // Neutral (probed upstream-defined at TCL=2/12). pie12 = cScale0 (= "#555")
  // whenever TCL > 0 (upstream `this.pie12 = this.pie0`).
  {
    const FlowThemeVariables t = resolveAt(FlowThemeId::Neutral, 2);
    require(t.pie[0] == QLatin1String("#F4F4F4"), "neutral TCL=2 pie1");
    for (int i = 1; i < 11; ++i) require(t.pie[i].isEmpty(), "neutral TCL=2 mid not empty");
    require(t.pie[11] == QLatin1String("#555"), "neutral TCL=2 pie12 (cScale0)");
  }
  {
    const FlowThemeVariables t = resolveAt(FlowThemeId::Neutral, 12);
    const QString expected[12] = {
        QStringLiteral("#F4F4F4"), QStringLiteral("#555"), QStringLiteral("#BBB"),
        QStringLiteral("#777"), QStringLiteral("#999"), QStringLiteral("#DDD"),
        QStringLiteral("#FFF"), QStringLiteral("#DDD"), QStringLiteral("#BBB"),
        QStringLiteral("#999"), QStringLiteral("#777"), QStringLiteral("#555")};
    for (int i = 0; i < 12; ++i)
      require(t.pie[i] == expected[i], QStringLiteral("neutral TCL=12 pie%1").arg(i + 1));
  }
}

// All 11 themes must resolve without crashing / out-of-bounds for extreme
// THEME_COLOR_LIMIT values. The fixed 13-element cScale arrays are indexed via
// cScaleCount = clamp(TCL, 0, 13), so TCL=13 reaches cScale[12] (dark: pie12 =
// cScale12 = #010029; others: empty, color ops skipped), TCL>13 / INT_MAX behave
// as 13, and negative as 0. (Upstream neutral crashes at TCL=0/1/13; the native
// port must be safer -- no crash, no OOB write.) The raw TCL is preserved on the
// model (get("THEME_COLOR_LIMIT")); only the array-access loops are clamped.
void checkTclNoOverflow() {
  const FlowThemeId all[] = {FlowThemeId::Base,  FlowThemeId::Dark,    FlowThemeId::Default,
                             FlowThemeId::Forest, FlowThemeId::Neutral, FlowThemeId::Neo,
                             FlowThemeId::NeoDark, FlowThemeId::Redux,   FlowThemeId::ReduxDark,
                             FlowThemeId::ReduxColor, FlowThemeId::ReduxDarkColor};
  const int tcls[] = {0, 1, 2, 12, 13, std::numeric_limits<int>::max(), -5};
  for (FlowThemeId id : all) {
    for (int tcl : tcls) {
      QHash<QString, QString> ov;
      ov.insert(QStringLiteral("THEME_COLOR_LIMIT"), QString::number(tcl));
      const FlowThemeVariables t = resolveFlowTheme(id, ov);  // must not crash / OOB
      require(t.get(QStringLiteral("THEME_COLOR_LIMIT")) == QString::number(tcl),
              QStringLiteral("TCL raw value not preserved for %1 / TCL=%2")
                  .arg(flowThemeIdName(id))
                  .arg(tcl));
      const int pc = std::clamp(t.themeColorLimit, 0, 13);
      require(pc >= 0 && pc <= 13, "cScaleCount out of [0,13]");
    }
  }
}

// themeOverrides routes THEME_COLOR_LIMIT through jsThemeColorLimit (full JS
// Number() + ceil semantics, shared with the requirement path) rather than the
// generic configString -> FlowThemeVariables::set toInt() path, which would
// truncate "2.5" to 0. null/absent -> not present (the theme keeps its default).
void checkThemeOverridesTclJs() {
  const auto overridesFor = [](const QString& tclValueJson) {
    QJsonObject config;
    if (!tclValueJson.isEmpty()) {
      const QJsonObject tv =
          QJsonDocument::fromJson(("{\"THEME_COLOR_LIMIT\":" + tclValueJson + "}").toUtf8()).object();
      config.insert(QStringLiteral("themeVariables"), tv);
    }
    return muffin::mermaid::editor::themeOverrides(config);
  };
  const auto equals = [](const QHash<QString, QString>& h, const QString& v) {
    return h.contains(QStringLiteral("THEME_COLOR_LIMIT")) &&
           h.value(QStringLiteral("THEME_COLOR_LIMIT")) == v;
  };
  require(equals(overridesFor(QStringLiteral("2.5")), QStringLiteral("3")), "TCL 2.5 -> 3");
  require(equals(overridesFor(QStringLiteral("\"2.5\"")), QStringLiteral("3")), "TCL \"2.5\" -> 3");
  require(equals(overridesFor(QStringLiteral("\"0x2\"")), QStringLiteral("2")), "TCL 0x2 -> 2");
  require(equals(overridesFor(QStringLiteral("2")), QStringLiteral("2")), "TCL 2 -> 2");
  require(equals(overridesFor(QStringLiteral("0")), QStringLiteral("0")), "TCL 0 -> 0");
  require(!overridesFor(QStringLiteral("null")).contains(QStringLiteral("THEME_COLOR_LIMIT")),
          "TCL null -> absent (keep default)");
  require(!overridesFor(QString()).contains(QStringLiteral("THEME_COLOR_LIMIT")),
          "TCL absent -> absent (keep default)");
}

// Pie/Quadrant scalar dependency overrides. Probed vs mermaid 11.16.0
// (scripts/probe_mermaid_pie_dep.mjs): a taskTextDarkColor override propagates to
// pie title/legend for Forest + the Family-A themes (their updateColors uses
// `||`, so the override survives), but NOT for Default (constructor updateColors
// pre-derives pie title) or Neutral (updateColors sets taskTextDarkColor
// unconditionally = this.text, clobbering the override). mainContrastColor
// propagates for Dark. A direct pieTitleTextColor override always wins.
// Quadrant point/axis/title text derive from primaryTextColor, borders from
// primaryBorderColor.
void checkScalarDependencyOverrides() {
  const auto resolved = [](FlowThemeId id, const QHash<QString, QString>& ov) {
    return resolveFlowTheme(id, ov);
  };

  // 1a. taskTextDarkColor override PROPAGATES (Forest + Family-A).
  for (FlowThemeId id : {FlowThemeId::Forest, FlowThemeId::Base, FlowThemeId::Neo,
                         FlowThemeId::ReduxColor}) {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("taskTextDarkColor"), QStringLiteral("#abcdef"));
    const FlowThemeVariables t = resolved(id, ov);
    require(t.get(QStringLiteral("pieTitleTextColor")) == QLatin1String("#abcdef"),
            QStringLiteral("taskTextDarkColor propagates to pieTitle (%1)").arg(flowThemeIdName(id)));
    require(t.get(QStringLiteral("pieLegendTextColor")) == QLatin1String("#abcdef"),
            QStringLiteral("taskTextDarkColor propagates to pieLegend (%1)").arg(flowThemeIdName(id)));
  }
  // 1b. taskTextDarkColor override does NOT propagate for Default / Neutral (the
  // pie title keeps the theme's own taskTextDarkColor); the override still wins
  // on get("taskTextDarkColor") itself.
  {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("taskTextDarkColor"), QStringLiteral("#abcdef"));
    const FlowThemeVariables td = resolved(FlowThemeId::Default, ov);
    require(td.get(QStringLiteral("taskTextDarkColor")) == QLatin1String("#abcdef"),
            "default taskTextDarkColor override applied");
    require(td.get(QStringLiteral("pieTitleTextColor")) == QLatin1String("black"),
            "default pieTitleTextColor does NOT follow taskTextDarkColor override");
    const FlowThemeVariables tn = resolved(FlowThemeId::Neutral, ov);
    require(tn.get(QStringLiteral("taskTextDarkColor")) == QLatin1String("#abcdef"),
            "neutral taskTextDarkColor override applied");
    require(tn.get(QStringLiteral("pieTitleTextColor")) == QLatin1String("#333"),
            "neutral pieTitleTextColor does NOT follow taskTextDarkColor override");
  }
  // pieSectionTextColor derives from textColor, NOT taskTextDarkColor.
  {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("taskTextDarkColor"), QStringLiteral("#abcdef"));
    const FlowThemeVariables t = resolved(FlowThemeId::Forest, ov);
    require(t.get(QStringLiteral("pieSectionTextColor")) != QLatin1String("#abcdef"),
            "pieSectionTextColor is textColor, independent of taskTextDarkColor");
  }

  // 2. mainContrastColor override -> Dark pie title/legend.
  {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("mainContrastColor"), QStringLiteral("#fedcba"));
    const FlowThemeVariables t = resolved(FlowThemeId::Dark, ov);
    require(t.get(QStringLiteral("pieTitleTextColor")) == QLatin1String("#fedcba"),
            "mainContrastColor -> dark pieTitleTextColor");
    require(t.get(QStringLiteral("pieLegendTextColor")) == QLatin1String("#fedcba"),
            "mainContrastColor -> dark pieLegendTextColor");
  }

  // 3. A direct pieTitleTextColor override wins over taskTextDarkColor.
  {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("taskTextDarkColor"), QStringLiteral("#abcdef"));
    ov.insert(QStringLiteral("pieTitleTextColor"), QStringLiteral("#111111"));
    const FlowThemeVariables t = resolved(FlowThemeId::Default, ov);
    require(t.get(QStringLiteral("pieTitleTextColor")) == QLatin1String("#111111"),
            "direct pieTitleTextColor has highest priority");
  }

  // 4. Quadrant scalar derivation from primaryTextColor / primaryBorderColor.
  {
    QHash<QString, QString> ov;
    ov.insert(QStringLiteral("primaryTextColor"), QStringLiteral("#123456"));
    ov.insert(QStringLiteral("primaryBorderColor"), QStringLiteral("#654321"));
    const FlowThemeVariables t = resolved(FlowThemeId::Base, ov);
    require(t.get(QStringLiteral("quadrantPointTextFill")) == QLatin1String("#123456"),
            "quadrantPointTextFill = primaryTextColor");
    require(t.get(QStringLiteral("quadrantXAxisTextFill")) == QLatin1String("#123456"),
            "quadrantXAxisTextFill = primaryTextColor");
    require(t.get(QStringLiteral("quadrantTitleFill")) == QLatin1String("#123456"),
            "quadrantTitleFill = primaryTextColor");
    require(t.get(QStringLiteral("quadrantInternalBorderStrokeFill")) == QLatin1String("#654321"),
            "quadrantInternalBorderStrokeFill = primaryBorderColor");
    require(t.get(QStringLiteral("quadrantExternalBorderStrokeFill")) == QLatin1String("#654321"),
            "quadrantExternalBorderStrokeFill = primaryBorderColor");
  }

  // 5. quadrantPointFill is re-derived UNCONDITIONALLY each updateColors
  // (upstream `(pointFill || isDark(q1)) ? lighten(q1) : darken(q1)`, NOT a
  // ||-guarded assign-if-empty). Probed vs mermaid 11.16.0
  // (scripts/probe_mermaid_quadrant_pointfill.mjs): a quadrant1Fill override
  // re-derives pointFill from the NEW q1 -- essential for Default's double pass,
  // where pointFill is already set on the second updateColors. Given the SAME
  // quadrant1Fill, single-pass (Forest/Base) and double-pass (Default) yield the
  // SAME pointFill (the one-arg lighten/darken both produce hsl(h, s, NaN%)), and
  // Default's overridden pointFill differs from its own base. A direct
  // quadrantPointFill override still wins.
  {
    const FlowThemeVariables defBase = resolved(FlowThemeId::Default, {});
    QHash<QString, QString> q1ov;
    q1ov.insert(QStringLiteral("quadrant1Fill"), QStringLiteral("#112233"));
    const FlowThemeVariables defOv = resolved(FlowThemeId::Default, q1ov);
    const FlowThemeVariables forOv = resolved(FlowThemeId::Forest, q1ov);
    const FlowThemeVariables basOv = resolved(FlowThemeId::Base, q1ov);
    require(defOv.get(QStringLiteral("quadrantPointFill")) !=
                defBase.get(QStringLiteral("quadrantPointFill")),
            "default quadrantPointFill re-derived from overridden quadrant1Fill");
    // Exact probed value (scripts/probe_mermaid_quadrant_pointfill.mjs):
    // quadrant1Fill #112233 -> hsl(210, 50%, NaN%) (one-arg lighten, NaN lightness).
    require(defOv.get(QStringLiteral("quadrantPointFill")) ==
                QLatin1String("hsl(210, 50%, NaN%)"),
            "default quadrantPointFill exact value for quadrant1Fill #112233");
    require(defOv.get(QStringLiteral("quadrantPointFill")) ==
                forOv.get(QStringLiteral("quadrantPointFill")),
            "default/forest quadrantPointFill agree for same quadrant1Fill");
    require(defOv.get(QStringLiteral("quadrantPointFill")) ==
                basOv.get(QStringLiteral("quadrantPointFill")),
            "default/base quadrantPointFill agree for same quadrant1Fill");
    QHash<QString, QString> pov;
    pov.insert(QStringLiteral("quadrantPointFill"), QStringLiteral("#aabbcc"));
    const FlowThemeVariables t = resolved(FlowThemeId::Default, pov);
    require(t.get(QStringLiteral("quadrantPointFill")) == QLatin1String("#aabbcc"),
            "direct quadrantPointFill override wins");
  }
}

// Gate D: walk EVERY resolved themeVariables key from the 285-key inventory
// (theme-variables-inventory.json — union of all 11 built-in themes' golden
// values plus upstream dist consumer classification) through
// FlowThemeVariables::get(). Keys whose golden value the native model does not
// yet reproduce are enumerated in `remaining` — the precise partial-closure
// list the config matrix themeVariables.* row points at. Grouped rationale:
//  - upstream-derived but never consumed by any 11.16 renderer (loop-written
//    palette slots / ctor leftovers): pie0, surface0-4, surfacePeer0-4,
//    darkTextColor, filterColor, rootLabelColor, contrast, text, note,
//    critical, done, scaleLabelColor, labelColor ("calculated" ctor leftover),
//    innerEndBackground (`.node circle.state-end` never matches the
//    dagre-wrapper DOM);
//  - sequence-local keys resolved inside SequenceDiagramAdapter (values pass
//    the sequence pixel/oracle gates): actorBkg, actorBorder, actorLineColor,
//    loopTextColor, labelTextColor, labelBoxBkgColor, labelBoxBorderColor,
//    activationBkgColor, activationBorderColor, sequenceNumberColor,
//    signalColor, signalTextColor;
//  - state/gantt/class/er/git/wardley-local keys resolved by the family
//    adapter or unconsumed: noteBkgColor, noteBorderColor, noteTextColor,
//    noteFontWeight, stateLabelColor, stateEdgeLabelBackground,
//    transitionColor, transitionLabelColor, compositeBorder, classText,
//    attributeBackgroundColorEven/Odd, branchLabelColor, erEdgeLabelBackground,
//    wardleyEvolutionColor, rowEven, rowOdd, rectBkgColor,
//    labelBackgroundColor, errorBkgColor, errorTextColor, personBkg,
//    personBorder, radius.
// Every removal from this list is a closed themeVariables key; keep sorted.
const QStringList& themeVariablesRemainingKeys() {
  static const QStringList remaining = {
      QStringLiteral("activationBkgColor"),
      QStringLiteral("activationBorderColor"),
      QStringLiteral("actorBkg"),
      QStringLiteral("actorBorder"),
      QStringLiteral("actorLineColor"),
      QStringLiteral("attributeBackgroundColorEven"),
      QStringLiteral("attributeBackgroundColorOdd"),
      QStringLiteral("branchLabelColor"),
      QStringLiteral("classText"),
      QStringLiteral("compositeBorder"),
      QStringLiteral("contrast"),
      QStringLiteral("critical"),
      QStringLiteral("darkTextColor"),
      QStringLiteral("done"),
      QStringLiteral("erEdgeLabelBackground"),
      QStringLiteral("errorBkgColor"),
      QStringLiteral("errorTextColor"),
      QStringLiteral("filterColor"),
      QStringLiteral("innerEndBackground"),
      QStringLiteral("labelBackgroundColor"),
      QStringLiteral("labelBoxBkgColor"),
      QStringLiteral("labelBoxBorderColor"),
      QStringLiteral("labelColor"),
      QStringLiteral("labelTextColor"),
      QStringLiteral("loopTextColor"),
      QStringLiteral("note"),
      QStringLiteral("noteBkgColor"),
      QStringLiteral("noteBorderColor"),
      QStringLiteral("noteFontWeight"),
      QStringLiteral("noteTextColor"),
      QStringLiteral("personBkg"),
      QStringLiteral("personBorder"),
      QStringLiteral("pie0"),
      QStringLiteral("radius"),
      QStringLiteral("rectBkgColor"),
      QStringLiteral("rootLabelColor"),
      QStringLiteral("rowEven"),
      QStringLiteral("rowOdd"),
      QStringLiteral("scaleLabelColor"),
      QStringLiteral("sequenceNumberColor"),
      QStringLiteral("signalColor"),
      QStringLiteral("signalTextColor"),
      QStringLiteral("stateEdgeLabelBackground"),
      QStringLiteral("stateLabelColor"),
      QStringLiteral("surface0"),
      QStringLiteral("surface1"),
      QStringLiteral("surface2"),
      QStringLiteral("surface3"),
      QStringLiteral("surface4"),
      QStringLiteral("surfacePeer0"),
      QStringLiteral("surfacePeer1"),
      QStringLiteral("surfacePeer2"),
      QStringLiteral("surfacePeer3"),
      QStringLiteral("surfacePeer4"),
      QStringLiteral("text"),
      QStringLiteral("transitionColor"),
      QStringLiteral("transitionLabelColor"),
      QStringLiteral("wardleyEvolutionColor"),
  };
  return remaining;
}

void checkThemeVariablesInventory(const QJsonObject& fixture) {
  require(fixture.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Theme inventory version drifted"));
  const QStringList remaining = themeVariablesRemainingKeys();
  const QJsonObject values = fixture.value(QStringLiteral("values")).toObject();
  require(values.size() == 11,
          QStringLiteral("Theme inventory must cover 11 themes"));
  QStringList mismatches;
  QStringList details;
  for (auto themeIt = values.constBegin(); themeIt != values.constEnd();
       ++themeIt) {
    const FlowThemeId id = parseThemeId(themeIt.key());
    require(flowThemeIdName(id) == themeIt.key(),
            QStringLiteral("Unknown inventory theme: %1").arg(themeIt.key()));
    const FlowThemeVariables theme = resolveFlowTheme(id);
    const QJsonObject vars = themeIt.value().toObject();
    for (auto varIt = vars.constBegin(); varIt != vars.constEnd(); ++varIt) {
      const QString key = varIt.key();
      if (remaining.contains(key)) continue;
      const QString native = theme.get(key);
      const QString golden = goldenToString(varIt.value());
      if (native != golden && !mismatches.contains(key)) {
        mismatches.append(key);
        details.append(QStringLiteral("%1[%2: %3 != %4]")
                           .arg(key, themeIt.key(), native, golden));
      }
    }
  }
  if (!mismatches.isEmpty())
    fail(QStringLiteral("themeVariables inventory keys not covered natively "
                        "(add to remaining only with a per-key justification): "
                        "%1 -- %2")
             .arg(mismatches.join(QLatin1String(", ")),
                  details.join(QLatin1String(", "))));
  // Every remaining key must still exist in the inventory (no stale entries).
  for (const QString& key : remaining)
    require(fixture.value(QStringLiteral("keys")).toObject().contains(key),
            QStringLiteral("Stale remaining key: %1").arg(key));
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected theme fixture path"));
  const QString fixturePath = QString::fromLocal8Bit(argv[1]);
  QFile file(fixturePath);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open theme fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Theme fixture version drifted"));

  for (const QJsonValue& v : root.value(QStringLiteral("themes")).toArray()) {
    const QJsonObject entry = v.toObject();
    const QString name = entry.value(QStringLiteral("name")).toString();
    const FlowThemeId id = parseThemeId(name);
    require(flowThemeIdName(id) == name,
            QStringLiteral("Theme name round-trip failed: %1").arg(name));
    compareTheme(id, entry.value(QStringLiteral("variables")).toObject(), name);
  }

  const QJsonObject overrideCase = root.value(QStringLiteral("overrideCase")).toObject();
  QHash<QString, QString> overrides;
  overrides.insert(QStringLiteral("primaryColor"), QStringLiteral("#ff0000"));
  overrides.insert(QStringLiteral("lineColor"), QStringLiteral("#00ff00"));
  compareOverride(FlowThemeId::Default, overrides,
                  overrideCase.value(QStringLiteral("variables")).toObject(),
                  overrideCase.value(QStringLiteral("name")).toString());

  checkPieTextColorOverrides();
  checkPieQuadrantDynamicOverrides();
  checkCScaleDynamicOverrides();
  checkMindmapRootOverrides();
  checkKanbanNeoShadows();
  checkJourneyFillTypeOverrides();
  checkPieTclDistribution();
  checkTclNoOverflow();
  checkThemeOverridesTclJs();
  checkScalarDependencyOverrides();
  checkFontWeightOverrides();
  checkPacketThemeOverrides();
  checkEventModelingThemeOverrides();
  checkRequirementEdgeLabelBackgroundOptional();
  checkWardleyThemeOverrides();

  QFile xyChartFile(
      QFileInfo(fixturePath).dir().filePath(QStringLiteral("xychart-theme.json")));
  require(xyChartFile.open(QIODevice::ReadOnly),
          QStringLiteral("Could not open XYChart theme fixture"));
  checkXYChartThemes(QJsonDocument::fromJson(xyChartFile.readAll()).object());

  QFile inventoryFile(
      QFileInfo(fixturePath).dir().filePath(
          QStringLiteral("theme-variables-inventory.json")));
  require(inventoryFile.open(QIODevice::ReadOnly),
          QStringLiteral("Could not open theme variables inventory"));
  checkThemeVariablesInventory(
      QJsonDocument::fromJson(inventoryFile.readAll()).object());

  qDebug().noquote()
      << "MermaidThemeTest: all themes + overrides match goldens";
  return 0;
}
