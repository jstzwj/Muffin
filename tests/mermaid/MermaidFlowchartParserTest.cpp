#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartTokenizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
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
  const QJsonArray productions = root.value(QStringLiteral("productions")).toArray();
  require(productions.size() == 189, QStringLiteral("Expected all 189 flow.jison productions"));
  for (qsizetype index = 0; index < productions.size(); ++index) {
    const QJsonObject production = productions.at(index).toObject();
    require(production.value(QStringLiteral("id")).toInt() == index + 1,
            QStringLiteral("Production ids must be contiguous"));
    require(!production.value(QStringLiteral("lhs")).toString().isEmpty() &&
                !production.value(QStringLiteral("native")).toString().isEmpty(),
            QStringLiteral("Production %1 is missing its native mapping").arg(index + 1));
    const QString status = production.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("covered")) {
      require(!production.value(QStringLiteral("fixtures")).toArray().isEmpty(),
              QStringLiteral("Production %1 has no golden fixture").arg(index + 1));
    } else {
      require((status == QLatin1String("unreachable") || status == QLatin1String("upstream-error")) &&
                  !production.value(QStringLiteral("reason")).toString().isEmpty(),
              QStringLiteral("Production %1 remains unclassified").arg(index + 1));
    }
  }
  require(cases.size() >= 8, QStringLiteral("Flowchart DB fixture is unexpectedly small"));
  for (const QJsonValue& value : cases) {
    const QJsonObject item = value.toObject();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QJsonObject expected = item.value(QStringLiteral("expected")).toObject();
    const QString source = item.value(QStringLiteral("source")).toString();
    QVector<muffin::mermaid::flowchart::FlowToken> tokens;
    try {
      tokens = muffin::mermaid::flowchart::FlowchartTokenizer(source).tokenize();
    } catch (const std::exception& error) {
      fail(QStringLiteral("Tokenizer failed for %1: %2").arg(id, QString::fromUtf8(error.what())));
    }
    const auto firstSignificant = std::find_if(tokens.cbegin(), tokens.cend(), [](const auto& token) {
      return token.kind != muffin::mermaid::flowchart::FlowTokenKind::Space &&
             token.kind != muffin::mermaid::flowchart::FlowTokenKind::Newline;
    });
    require(firstSignificant != tokens.cend() &&
                firstSignificant->kind == muffin::mermaid::flowchart::FlowTokenKind::Graph,
            QStringLiteral("Tokenizer did not start %1 with GRAPH").arg(id));
    for (const auto& token : tokens) {
      require(token.kind != muffin::mermaid::flowchart::FlowTokenKind::Unknown,
              QStringLiteral("Unknown token in %1 at %2:%3: '%4'")
                  .arg(id).arg(token.line).arg(token.column).arg(token.text));
    }
    QJsonObject actual;
    try {
      actual = Flowchart::parse(source).toJson();
    } catch (const std::exception& error) {
      fail(QStringLiteral("Parser failed for %1: %2").arg(id, QString::fromUtf8(error.what())));
    }
    require(actual == expected,
            QStringLiteral("Flowchart DB mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id, json(actual), json(expected)));
  }

  for (const QJsonValue& value : root.value(QStringLiteral("invalidCases")).toArray()) {
    const QJsonObject item = value.toObject();
    require(item.value(QStringLiteral("rejected")).toBool(),
            QStringLiteral("Upstream invalid fixture was unexpectedly accepted: %1")
                .arg(item.value(QStringLiteral("id")).toString()));
    bool threw = false;
    try {
      Flowchart::parse(item.value(QStringLiteral("source")).toString());
    } catch (const FlowchartParseError&) {
      threw = true;
    }
    require(threw, QStringLiteral("Native parser accepted upstream-invalid fixture: %1")
                       .arg(item.value(QStringLiteral("id")).toString()));
  }

  for (const QString& invalid : {QStringLiteral("not a flowchart"),
                                 QStringLiteral("flowchart SIDEWAYS\nA --> B"),
                                 QStringLiteral("graph; A --> B"),
                                 QStringLiteral("flowchart TB\nsubgraph\nA --> B\nend"),
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
