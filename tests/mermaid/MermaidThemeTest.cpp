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

QStringList fieldsForTheme(FlowThemeId) {
  QStringList f = criticalFields();
  for (int i = 0; i <= 11; ++i) {
    f.append(QStringLiteral("cScale%1").arg(i));
    f.append(QStringLiteral("cScalePeer%1").arg(i));
    f.append(QStringLiteral("cScaleInv%1").arg(i));
    f.append(QStringLiteral("cScaleLabel%1").arg(i));
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

  qDebug().noquote() << "MermaidThemeTest: all themes + override match golden";
  return 0;
}
