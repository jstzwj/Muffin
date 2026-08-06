#include "mermaid/theme/FlowTheme.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdlib>

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

// Pie + Quadrant themeVariables are derived per-theme in updateColors.
// Currently implemented for the Family-A themes (base/neo/neo-dark/redux/
// redux-dark/redux-color/redux-dark-color — populatePieFamilyA) and Default
// (populatePieDefault). Dark/Forest/Neutral get their own pie/quadrant formulas
// in a later commit; until then they are excluded from the golden comparison
// (compare only what is derived, never a knowingly-unimplemented field).
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

  qDebug().noquote() << "MermaidThemeTest: all themes + override match golden";
  return 0;
}
