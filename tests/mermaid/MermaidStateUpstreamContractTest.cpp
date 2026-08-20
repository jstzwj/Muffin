#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>
#include <cstdio>

using namespace muffin::mermaid::state;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
}

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected state DB fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open state DB fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  const QJsonObject grammar = upstream.value(QStringLiteral("grammar")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              grammar.value(QStringLiteral("productionCount")).toInt() == 49 &&
              grammar.value(QStringLiteral("productions")).toArray().size() == 49,
          QStringLiteral("State upstream grammar contract drifted"));

  QSet<int> productionIds;
  for (const QJsonValue& value : grammar.value(QStringLiteral("productions")).toArray()) {
    const QJsonObject production = value.toObject();
    productionIds.insert(production.value(QStringLiteral("id")).toInt());
    require(production.value(QStringLiteral("symbol")).toInt() > 0 &&
                production.value(QStringLiteral("rhsLength")).toInt() >= 0,
            QStringLiteral("State production metadata is incomplete"));
  }
  require(productionIds.size() == 49 && productionIds.contains(1) &&
              productionIds.contains(49),
          QStringLiteral("State production IDs are incomplete"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  QSet<QString> ids;
  int compositeCases = 0, noteCases = 0, styleCases = 0, linkCases = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id),
            QStringLiteral("Duplicate state fixture ID: %1").arg(id));
    ids.insert(id);
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    require(!expected.value(QStringLiteral("root")).toArray().isEmpty() &&
                expected.contains(QStringLiteral("states")) &&
                expected.contains(QStringLiteral("relations")),
            QStringLiteral("State fixture lacks parser/DB output: %1").arg(id));
    QJsonObject actual;
    try {
      actual = StateDiagram::parse(
          fixture.value(QStringLiteral("source")).toString()).toJson();
    } catch (const StateParseError& parseError) {
      fail(QStringLiteral("State native parser rejected %1: %2")
               .arg(id, QString::fromUtf8(parseError.what())));
    }
    require(actual == expected,
            QStringLiteral("State parser/DB mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id,
                     QString::fromUtf8(QJsonDocument(actual).toJson(QJsonDocument::Indented)),
                     QString::fromUtf8(QJsonDocument(expected).toJson(QJsonDocument::Indented))));
    const QJsonObject actualLayout = stateLayoutInputToJson(
        buildStateLayoutInput(StateDiagram::parse(
            fixture.value(QStringLiteral("source")).toString()).data()));
    const QJsonObject expectedLayout = fixture.value(QStringLiteral("layoutInput")).toObject();
    require(actualLayout == expectedLayout,
            QStringLiteral("State Dagre input mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id,
                     QString::fromUtf8(QJsonDocument(actualLayout).toJson(QJsonDocument::Indented)),
                     QString::fromUtf8(QJsonDocument(expectedLayout).toJson(QJsonDocument::Indented))));
    compositeCases += id.contains(QLatin1String("composite"));
    noteCases += id.contains(QLatin1String("note"));
    styleCases += !expected.value(QStringLiteral("classes")).toArray().isEmpty();
    linkCases += !expected.value(QStringLiteral("links")).toArray().isEmpty();
  }
  require(cases.size() == 13 && compositeCases == 4 && noteCases == 2 &&
              styleCases == 1 && linkCases == 1,
          QStringLiteral("State upstream semantic matrix regressed"));
  const QVector<StateProductionMapping> mappings = stateProductionMappings();
  require(mappings.size() == 49,
          QStringLiteral("State native production mapping is incomplete"));
  for (qsizetype index = 0; index < mappings.size(); ++index)
    require(mappings.at(index).id == index + 1 &&
                !mappings.at(index).parserFunction.isEmpty() &&
                !mappings.at(index).oracleCase.isEmpty(),
            QStringLiteral("State production mapping %1 is incomplete").arg(index + 1));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("1f87e0533d45f0df27a80efa97222e92f91593ef9ef7c60da9d2daca16ee3f00"),
          QStringLiteral("State upstream fixture changed; audit and update its digest"));
  qDebug() << "MermaidStateUpstreamContractTest:" << cases.size()
           << "cases and" << productionIds.size() << "productions passed";
  return 0;
}
