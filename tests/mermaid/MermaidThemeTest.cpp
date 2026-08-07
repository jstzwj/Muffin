#include "mermaid/theme/FlowTheme.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QDebug>
#include <QFile>
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
      QStringLiteral("labelBackground"), QStringLiteral("textColor"),
      QStringLiteral("titleColor"),   QStringLiteral("edgeLabelBackground"),
      QStringLiteral("clusterBkg"),   QStringLiteral("clusterBorder"),
      QStringLiteral("primaryBorderColor"), QStringLiteral("primaryTextColor"),
      QStringLiteral("nodeTextColor"), QStringLiteral("nodeBkg"),
      QStringLiteral("nodeBorder"),   QStringLiteral("defaultLinkColor"),
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

QStringList fieldsForTheme(FlowThemeId id) {
  QStringList f = criticalFields();
  for (int i = 0; i <= 11; ++i) {
    f.append(QStringLiteral("cScale%1").arg(i));
    f.append(QStringLiteral("cScalePeer%1").arg(i));
    f.append(QStringLiteral("cScaleInv%1").arg(i));
    f.append(QStringLiteral("cScaleLabel%1").arg(i));
  }
  if (pieQuadrantImplemented(id)) f.append(pieQuadrantFields());
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
}

void compareOverride(FlowThemeId id, const QHash<QString, QString>& overrides,
                     const QJsonObject& goldenVars, const QString& label) {
  const FlowThemeVariables t = resolveFlowTheme(id, overrides);
  for (const QString& key : criticalFields()) {
    const QString native = t.get(key);
    const QString golden = goldenToString(goldenVars.value(key));
    if (native != golden) {
      fail(QStringLiteral("Override %1/%2 %3 mismatch: native=%4 golden=%5")
               .arg(label, flowThemeIdName(id), key, native, golden));
    }
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

// THEME_COLOR_LIMIT controls how many pie slices the cScale-derived themes
// (dark, neutral) populate. Upstream: `for i<TCL: this["pie"+i]=this["cScale"+i]`
// (0-based keys), renderer reads pie1..pie12, so pieK = cScaleK for K=1..TCL-1.
// Probed vs mermaid 11.16.0 (scripts/probe_mermaid_pie_tcl.mjs): dark is robust
// across TCL=0/1/2/12; neutral is defined at TCL=2/12 (crashes upstream at 0/1/
// 13, exercised only for no-crash in checkTclNoOverflow). dark pie12 is ALWAYS
// unset: the native paletteCount clamp caps TCL>12 at 12, and the upstream loop
// never writes the 0-based "pie12" key for TCL<=12.
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
// THEME_COLOR_LIMIT values. The fixed 12-element cScale/pie arrays are indexed
// via paletteCount = clamp(TCL, 0, 12), so TCL=13 / INT_MAX behave as 12 and
// negative as 0. (Upstream neutral crashes at TCL=0/1/13; the native port must
// be safer -- no crash, no OOB write.) The raw TCL is preserved on the model
// (get("THEME_COLOR_LIMIT")); only the array-access loops are clamped.
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
      const int pc = std::clamp(t.themeColorLimit, 0, 12);
      require(pc >= 0 && pc <= 12, "paletteCount out of [0,12]");
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
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected theme fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
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
  checkPieTclDistribution();
  checkTclNoOverflow();
  checkThemeOverridesTclJs();

  qDebug().noquote() << "MermaidThemeTest: all themes + override match golden";
  return 0;
}
