#include "mermaid/flowchart/Flowchart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>

using muffin::mermaid::flowchart::Flowchart;
using muffin::mermaid::flowchart::FlowchartParseError;
using muffin::mermaid::flowchart::FlowchartParseOptions;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString json(const QJsonObject& value) {
  return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected flowchart DB fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open %1").arg(file.fileName()));
  QJsonParseError error;
  const QJsonDocument fixture = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && fixture.isObject(), QStringLiteral("Invalid flowchart fixture"));
  const QJsonObject root = fixture.object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Flowchart fixture version drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() >= 8, QStringLiteral("Flowchart DB fixture is unexpectedly small"));
  for (const QJsonValue& value : cases) {
    const QJsonObject item = value.toObject();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QJsonObject expected = item.value(QStringLiteral("expected")).toObject();
    const QJsonObject actual = Flowchart::parse(item.value(QStringLiteral("source")).toString()).toJson();
    require(actual == expected,
            QStringLiteral("Flowchart DB mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id, json(actual), json(expected)));
  }

  for (const QString& invalid : {QStringLiteral("not a flowchart"),
                                 QStringLiteral("flowchart LR\nsubgraph open\nA --> B"),
                                 QStringLiteral("flowchart LR\nA --> B\nlinkStyle 3 stroke:red")}) {
    bool threw = false;
    try {
      Flowchart::parse(invalid);
    } catch (const FlowchartParseError&) {
      threw = true;
    }
    require(threw, QStringLiteral("Invalid flowchart should throw: %1").arg(invalid));
  }

  bool edgeLimitThrew = false;
  try {
    FlowchartParseOptions options;
    options.maxEdges = 1;
    Flowchart::parse(QStringLiteral("flowchart LR\nA --> B\nB --> C"), options);
  } catch (const FlowchartParseError&) {
    edgeLimitThrew = true;
  }
  require(edgeLimitThrew, QStringLiteral("Flowchart parser must enforce maxEdges"));

  bool textLimitThrew = false;
  try {
    FlowchartParseOptions options;
    options.maxTextSize = 8;
    Flowchart::parse(QStringLiteral("flowchart LR\nA --> B"), options);
  } catch (const FlowchartParseError&) {
    textLimitThrew = true;
  }
  require(textLimitThrew, QStringLiteral("Flowchart parser must enforce maxTextSize"));
  return 0;
}
