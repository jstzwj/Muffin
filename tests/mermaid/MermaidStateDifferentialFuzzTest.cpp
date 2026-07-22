#include "mermaid/state/StateDiagram.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid::state;
namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QJsonObject counts(const QMap<QString, int>& values) {
  QJsonObject result;
  for (auto it = values.cbegin(); it != values.cend(); ++it) result.insert(it.key(), it.value());
  return result;
}
}

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected state differential fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open state differential fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureDigest")).toString() ==
              QLatin1String("77e3c8f0bc0b6569be7c0620c40c81798c027a296ee51f395db260c2fb8d1491"),
          QStringLiteral("State differential fixture drifted"));
  const QJsonObject generator = root.value(QStringLiteral("generator")).toObject();
  const QJsonArray cases = root.value(QStringLiteral("negativeCases")).toArray();
  QMap<QString, int> operators, stages, positionRules;
  QSet<QString> codes;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    bool rejected = false;
    try {
      StateDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    } catch (const StateParseError& parseError) {
      rejected = true;
      const StateDiagnostic& diagnostic = parseError.diagnostic();
      const QJsonObject upstream = fixture.value(QStringLiteral("upstreamError")).toObject();
      const QJsonObject expected = upstream.value(QStringLiteral("normalized")).toObject();
      require(stateErrorStageName(diagnostic.stage) == upstream.value(QStringLiteral("stage")).toString(),
              QStringLiteral("%1 diagnostic stage differs").arg(fixture.value(QStringLiteral("id")).toString()));
      require(stateErrorCodeName(diagnostic.code) == fixture.value(QStringLiteral("expectedNativeCode")).toString(),
              QStringLiteral("%1 diagnostic code differs").arg(fixture.value(QStringLiteral("id")).toString()));
      require(diagnostic.span.line == expected.value(QStringLiteral("line")).toInt() &&
                  diagnostic.span.column == expected.value(QStringLiteral("column")).toInt(),
              QStringLiteral("%1 diagnostic position differs: %2:%3 vs %4:%5")
                  .arg(fixture.value(QStringLiteral("id")).toString())
                  .arg(diagnostic.span.line).arg(diagnostic.span.column)
                  .arg(expected.value(QStringLiteral("line")).toInt())
                  .arg(expected.value(QStringLiteral("column")).toInt()));
      codes.insert(stateErrorCodeName(diagnostic.code));
      ++stages[stateErrorStageName(diagnostic.stage)];
    }
    require(rejected, QStringLiteral("Native accepted state mutation %1")
                          .arg(fixture.value(QStringLiteral("id")).toString()));
    ++operators[fixture.value(QStringLiteral("operator")).toString()];
    ++positionRules[fixture.value(QStringLiteral("upstreamError")).toObject()
                        .value(QStringLiteral("positionRule")).toString()];
    require(fixture.value(QStringLiteral("targetProduction")).toInt() > 0,
            QStringLiteral("State mutation lacks target production"));
  }
  require(cases.size() == generator.value(QStringLiteral("caseCount")).toInt() &&
              counts(operators) == generator.value(QStringLiteral("operatorCounts")).toObject() &&
              counts(stages) == generator.value(QStringLiteral("stageCounts")).toObject() &&
              counts(positionRules) == generator.value(QStringLiteral("positionRuleCounts")).toObject(),
          QStringLiteral("State mutation coverage matrix regressed"));
  for (const QJsonValue& code : generator.value(QStringLiteral("requiredNativeCodes")).toArray())
    require(codes.contains(code.toString()),
            QStringLiteral("State diagnostic code is uncovered: %1").arg(code.toString()));
  qDebug() << "MermaidStateDifferentialFuzzTest:" << cases.size()
           << "production-aware mutations passed";
  return 0;
}
