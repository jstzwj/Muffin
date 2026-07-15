#include "mermaid/sequence/SequenceDiagram.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid::sequence;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QString errorClass(SequenceErrorStage stage) {
  if (stage == SequenceErrorStage::Detector) return QStringLiteral("detection");
  if (stage == SequenceErrorStage::Semantic) return QStringLiteral("semantic");
  return QStringLiteral("syntax");
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected sequence differential fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open sequence differential fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Sequence differential version drifted"));
  require(root.value(QStringLiteral("fixtureDigest")).toString() ==
              QLatin1String("11be1182acaf72d4b6771420051304f36116f560350f096f14a9af7a45970b96"),
          QStringLiteral("Sequence differential fixture changed; audit and update its digest"));

  const QJsonObject generator = root.value(QStringLiteral("generator")).toObject();
  const QJsonArray cases = root.value(QStringLiteral("negativeCases")).toArray();
  require(cases.size() == generator.value(QStringLiteral("caseCount")).toInt() && cases.size() == 15,
          QStringLiteral("Sequence mutation case count drifted"));
  QMap<QString, int> operatorCounts, stageCounts, matrixCounts, positionRuleCounts;
  QSet<QString> codes, ids;
  int comparedLines = 0, comparedColumns = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!ids.contains(id), QStringLiteral("Duplicate sequence mutation id: %1").arg(id));
    ids.insert(id);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString operation = fixture.value(QStringLiteral("operator")).toString();
    const QJsonObject upstream = fixture.value(QStringLiteral("upstreamError")).toObject();
    const QJsonObject expectedPosition = upstream.value(QStringLiteral("normalized")).toObject();
    const QString positionRule = upstream.value(QStringLiteral("positionRule")).toString();
    require(!positionRule.isEmpty(), QStringLiteral("%1 has no upstream position normalization rule").arg(id));
    require(upstream.value(QStringLiteral("raw")).isObject(),
            QStringLiteral("%1 lost its raw Jison location").arg(id));
    bool rejected = false;
    try {
      SequenceDiagram::parse(source);
    } catch (const SequenceParseError& error) {
      rejected = true;
      const SequenceDiagnostic& diagnostic = error.diagnostic();
      const QString nativeStage = sequenceErrorStageName(diagnostic.stage);
      const QString nativeCode = sequenceErrorCodeName(diagnostic.code);
      require(nativeStage == upstream.value(QStringLiteral("stage")).toString(),
              QStringLiteral("%1 stage mismatch: native=%2 upstream=%3")
                  .arg(id, nativeStage, upstream.value(QStringLiteral("stage")).toString()));
      require(errorClass(diagnostic.stage) == upstream.value(QStringLiteral("class")).toString(),
              QStringLiteral("%1 error class mismatch").arg(id));
      require(nativeCode == fixture.value(QStringLiteral("expectedNativeCode")).toString(),
              QStringLiteral("%1 diagnostic mismatch: native=%2 expected=%3")
                  .arg(id, nativeCode, fixture.value(QStringLiteral("expectedNativeCode")).toString()));
      ++comparedLines;
      require(diagnostic.span.line == expectedPosition.value(QStringLiteral("line")).toInt(),
              QStringLiteral("%1 line mismatch: native=%2 expected=%3")
                  .arg(id).arg(diagnostic.span.line)
                  .arg(expectedPosition.value(QStringLiteral("line")).toInt()));
      ++comparedColumns;
      require(diagnostic.span.column == expectedPosition.value(QStringLiteral("column")).toInt(),
              QStringLiteral("%1 column mismatch: native=%2 expected=%3")
                  .arg(id).arg(diagnostic.span.column)
                  .arg(expectedPosition.value(QStringLiteral("column")).toInt()));
      codes.insert(nativeCode);
      ++stageCounts[nativeStage];
      const QString matrix = QStringLiteral("%1|%2|%3|%4")
          .arg(QStringLiteral("%1:%2")
                   .arg(fixture.value(QStringLiteral("targetProduction")).toInt())
                   .arg(fixture.value(QStringLiteral("targetRhsLength")).toInt()))
          .arg(operation, nativeStage, nativeCode);
      ++matrixCounts[matrix];
      ++positionRuleCounts[positionRule];
    }
    require(rejected, QStringLiteral("Native accepted sequence mutation: %1\n%2").arg(id, source));
    ++operatorCounts[operation];
  }

  auto asJson = [](const QMap<QString, int>& values) {
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) result.insert(it.key(), it.value());
    return result;
  };
  require(asJson(operatorCounts) == generator.value(QStringLiteral("operatorCounts")).toObject(),
          QStringLiteral("Sequence mutation operator coverage drifted"));
  require(asJson(stageCounts) == generator.value(QStringLiteral("stageCounts")).toObject(),
          QStringLiteral("Sequence diagnostic stage coverage drifted"));
  require(asJson(matrixCounts) == generator.value(QStringLiteral("matrixCounts")).toObject(),
          QStringLiteral("Sequence production/operator/stage/code matrix drifted"));
  require(asJson(positionRuleCounts) == generator.value(QStringLiteral("positionRuleCounts")).toObject(),
          QStringLiteral("Sequence upstream position normalization coverage drifted"));
  const QJsonObject rawLocationCounts = generator.value(QStringLiteral("rawLocationCounts")).toObject();
  require(rawLocationCounts.value(QStringLiteral("lines")).toInt() >= 11 &&
              rawLocationCounts.value(QStringLiteral("columns")).toInt() >= 9,
          QStringLiteral("Sequence raw Jison line/column coverage regressed"));
  require(generator.value(QStringLiteral("unreachableStages")).toObject()
                  .value(QStringLiteral("lexer")).toString().contains(QLatin1String("INVALID")),
          QStringLiteral("Sequence lexer-stage unreachability is no longer classified"));
  for (const QJsonValue& code : generator.value(QStringLiteral("requiredNativeCodes")).toArray())
    require(codes.contains(code.toString()),
            QStringLiteral("Required sequence diagnostic is uncovered: %1").arg(code.toString()));
  require(comparedLines == cases.size() && comparedColumns == cases.size(),
          QStringLiteral("Sequence stable line/column coverage regressed"));
  for (const QString& operation : {
           QStringLiteral("delete-required-terminal"),
           QStringLiteral("replace-terminal-class"),
           QStringLiteral("duplicate-separator"),
           QStringLiteral("truncate-recursive-production"),
           QStringLiteral("break-paired-delimiter"),
           QStringLiteral("insert-nullable-boundary-token")})
    require(operatorCounts.contains(operation),
            QStringLiteral("Sequence mutation operator is uncovered: %1").arg(operation));

  qDebug() << "MermaidSequenceDifferentialFuzzTest:" << cases.size()
           << "mutations with stable stage/code/line/column passed";
  return 0;
}
