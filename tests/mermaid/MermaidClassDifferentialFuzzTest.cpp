#include "mermaid/classdiagram/ClassDiagram.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid::classdiagram;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QString errorClass(ClassErrorStage stage) {
  return stage == ClassErrorStage::Detector ? QStringLiteral("detection") : QStringLiteral("syntax");
}
QJsonObject countsJson(const QMap<QString, int>& values) {
  QJsonObject result;
  for (auto it = values.cbegin(); it != values.cend(); ++it) result.insert(it.key(), it.value());
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected class differential fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open class differential fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Class differential version drifted"));
  require(root.value(QStringLiteral("fixtureDigest")).toString() ==
              QLatin1String("941abfebb0ca4731c5c3c02bfc5cb24703c9d79f0f00dab3b52ec08ae7c76a24"),
          QStringLiteral("Class differential fixture changed; audit and update its digest"));

  const QJsonObject generator = root.value(QStringLiteral("generator")).toObject();
  const QJsonArray cases = root.value(QStringLiteral("negativeCases")).toArray();
  require(cases.size() == 10 && generator.value(QStringLiteral("caseCount")).toInt() == cases.size(),
          QStringLiteral("Class mutation case count drifted"));
  QMap<QString, int> operatorCounts, stageCounts, matrixCounts, positionRuleCounts;
  QSet<QString> ids, codes;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id), QStringLiteral("Duplicate class mutation id: %1").arg(id));
    ids.insert(id);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString operation = fixture.value(QStringLiteral("operator")).toString();
    const QJsonObject upstream = fixture.value(QStringLiteral("upstreamError")).toObject();
    const QJsonObject position = upstream.value(QStringLiteral("normalized")).toObject();
    bool rejected = false;
    try {
      ClassDiagram::parse(source);
    } catch (const ClassParseError& error) {
      rejected = true;
      const ClassDiagnostic& diagnostic = error.diagnostic();
      const QString stage = classErrorStageName(diagnostic.stage);
      const QString code = classErrorCodeName(diagnostic.code);
      require(stage == upstream.value(QStringLiteral("stage")).toString(),
              QStringLiteral("%1 stage mismatch: native=%2 upstream=%3")
                  .arg(id, stage, upstream.value(QStringLiteral("stage")).toString()));
      require(errorClass(diagnostic.stage) == upstream.value(QStringLiteral("class")).toString(),
              QStringLiteral("%1 error class mismatch").arg(id));
      require(code == fixture.value(QStringLiteral("expectedNativeCode")).toString(),
              QStringLiteral("%1 code mismatch: native=%2 expected=%3")
                  .arg(id, code, fixture.value(QStringLiteral("expectedNativeCode")).toString()));
      require(diagnostic.span.line == position.value(QStringLiteral("line")).toInt() &&
                  diagnostic.span.column == position.value(QStringLiteral("column")).toInt(),
              QStringLiteral("%1 position mismatch: native=%2:%3 expected=%4:%5")
                  .arg(id).arg(diagnostic.span.line).arg(diagnostic.span.column)
                  .arg(position.value(QStringLiteral("line")).toInt())
                  .arg(position.value(QStringLiteral("column")).toInt()));
      codes.insert(code);
      ++stageCounts[stage];
      ++positionRuleCounts[upstream.value(QStringLiteral("positionRule")).toString()];
      const QString matrix = QStringLiteral("%1:%2|%3|%4|%5")
          .arg(fixture.value(QStringLiteral("targetProduction")).toInt())
          .arg(fixture.value(QStringLiteral("targetRhsLength")).toInt())
          .arg(operation, stage, code);
      ++matrixCounts[matrix];
    }
    require(rejected, QStringLiteral("Native accepted class mutation: %1\n%2").arg(id, source));
    ++operatorCounts[operation];
  }

  require(countsJson(operatorCounts) == generator.value(QStringLiteral("operatorCounts")).toObject(),
          QStringLiteral("Class mutation operator coverage drifted"));
  require(countsJson(stageCounts) == generator.value(QStringLiteral("stageCounts")).toObject(),
          QStringLiteral("Class diagnostic stage coverage drifted"));
  require(countsJson(matrixCounts) == generator.value(QStringLiteral("matrixCounts")).toObject(),
          QStringLiteral("Class production/operator/stage/code matrix drifted"));
  require(countsJson(positionRuleCounts) == generator.value(QStringLiteral("positionRuleCounts")).toObject(),
          QStringLiteral("Class position-rule coverage drifted"));
  const QJsonObject raw = generator.value(QStringLiteral("rawLocationCounts")).toObject();
  require(raw.value(QStringLiteral("lines")).toInt() >= 7 &&
              raw.value(QStringLiteral("columns")).toInt() >= 4,
          QStringLiteral("Class raw Jison location coverage regressed"));
  for (const QJsonValue& code : generator.value(QStringLiteral("requiredNativeCodes")).toArray())
    require(codes.contains(code.toString()),
            QStringLiteral("Required class diagnostic is uncovered: %1").arg(code.toString()));
  for (const QString& operation : {
           QStringLiteral("delete-required-terminal"), QStringLiteral("replace-terminal-class"),
           QStringLiteral("duplicate-separator"), QStringLiteral("truncate-recursive-production"),
           QStringLiteral("break-paired-delimiter"), QStringLiteral("insert-nullable-boundary-token")})
    require(operatorCounts.contains(operation),
            QStringLiteral("Class mutation operator is uncovered: %1").arg(operation));

  qDebug() << "MermaidClassDifferentialFuzzTest:" << cases.size()
           << "mutations with stable stage/code/line/column passed";
  return 0;
}
