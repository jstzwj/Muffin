#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartTokenizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <cstdlib>

using muffin::mermaid::flowchart::Flowchart;
using muffin::mermaid::flowchart::FlowchartErrorCategory;
using muffin::mermaid::flowchart::FlowchartErrorCode;

namespace {

QString artifactRepositoryRoot;

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void configureFailureArtifacts(const QString& fixturePath) {
  QDir directory = QFileInfo(fixturePath).absoluteDir();
  for (int depth = 0; depth < 3; ++depth) directory.cdUp();
  artifactRepositoryRoot = directory.absolutePath();
}

[[noreturn]] void failDifferential(const QString& message, QJsonObject item) {
  if (!artifactRepositoryRoot.isEmpty()) {
    QString id = item.value(QStringLiteral("id")).toString(QStringLiteral("unknown"));
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    QDir artifacts(QDir(artifactRepositoryRoot).filePath(QStringLiteral("build/differential-failures")));
    artifacts.mkpath(QStringLiteral("."));
    const QString sourcePath = artifacts.filePath(id + QStringLiteral(".mmd"));
    const QString minimizedPath = artifacts.filePath(id + QStringLiteral(".min.mmd"));
    QFile sourceFile(sourcePath);
    if (sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      sourceFile.write(item.value(QStringLiteral("source")).toString().toUtf8());
      sourceFile.write("\n");
      sourceFile.close();
      const QString script = QDir(artifactRepositoryRoot).filePath(
          QStringLiteral("scripts/minimize_mermaid_flowchart_difference.mjs"));
      const QString mermaidRoot = QDir(artifactRepositoryRoot).filePath(
          QStringLiteral("../mermaid-cli/node_modules/mermaid"));
      if (QFileInfo::exists(script) && QFileInfo::exists(mermaidRoot)) {
        QProcess minimizer;
        minimizer.setWorkingDirectory(artifactRepositoryRoot);
        minimizer.start(QStringLiteral("node"),
                        {script, sourcePath, mermaidRoot,
                         QCoreApplication::applicationFilePath(), minimizedPath});
        minimizer.waitForFinished(-1);
        item.insert(QStringLiteral("minimizerExitCode"), minimizer.exitCode());
        item.insert(QStringLiteral("minimizerOutput"),
                    QString::fromUtf8(minimizer.readAllStandardOutput()));
      }
      if (QFile minimized(minimizedPath); minimized.open(QIODevice::ReadOnly))
        item.insert(QStringLiteral("minimizedSource"), QString::fromUtf8(minimized.readAll()));
      item.insert(QStringLiteral("failure"), message);
      QFile metadata(artifacts.filePath(id + QStringLiteral(".regression.json")));
      if (metadata.open(QIODevice::WriteOnly | QIODevice::Truncate))
        metadata.write(QJsonDocument(item).toJson(QJsonDocument::Indented));
    }
  }
  fail(message);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString json(const QJsonObject& value) {
  return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

QString errorClass(FlowchartErrorCategory category) {
  switch (category) {
    case FlowchartErrorCategory::MissingHeader: return QStringLiteral("detection");
    case FlowchartErrorCategory::LinkStyleBounds: return QStringLiteral("semantic");
    case FlowchartErrorCategory::Syntax:
    case FlowchartErrorCategory::UnclosedSubgraph:
    case FlowchartErrorCategory::UnexpectedEnd:
    case FlowchartErrorCategory::InvalidNode:
    case FlowchartErrorCategory::InvalidDirective:
      return QStringLiteral("syntax");
    case FlowchartErrorCategory::LimitExceeded: return QStringLiteral("limit");
    case FlowchartErrorCategory::SecurityViolation: return QStringLiteral("security");
  }
  return QStringLiteral("unknown");
}

int runOracle() {
  QTextStream input(stdin, QIODevice::ReadOnly);
  QTextStream output(stdout, QIODevice::WriteOnly);
  while (!input.atEnd()) {
    const QString line = input.readLine();
    if (line.isEmpty()) continue;
    QJsonParseError requestError;
    const QJsonDocument request = QJsonDocument::fromJson(line.toUtf8(), &requestError);
    QJsonObject response;
    if (requestError.error != QJsonParseError::NoError || !request.isObject()) {
      response.insert(QStringLiteral("ok"), false);
      response.insert(QStringLiteral("error"), QStringLiteral("Invalid JSON oracle request"));
    } else {
      try {
        const QJsonObject requestObject = request.object();
        const QString source = requestObject.value(QStringLiteral("source")).toString();
        if (requestObject.value(QStringLiteral("tokenize")).toBool()) {
          QJsonArray tokens;
          for (const auto& token : muffin::mermaid::flowchart::FlowchartTokenizer(
                   source, requestObject.value(QStringLiteral("document")).toBool()).tokenize()) {
            QJsonObject value;
            value.insert(QStringLiteral("kind"), muffin::mermaid::flowchart::flowTokenName(token.kind));
            value.insert(QStringLiteral("text"), token.text);
            value.insert(QStringLiteral("line"), token.line);
            value.insert(QStringLiteral("column"), token.column);
            tokens.push_back(value);
          }
          response.insert(QStringLiteral("ok"), true);
          response.insert(QStringLiteral("tokens"), tokens);
          output << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
          output.flush();
          continue;
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("ast"),
                        Flowchart::parse(source).toJson());
      } catch (const muffin::mermaid::flowchart::FlowchartParseError& error) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("category"), static_cast<int>(error.category()));
        response.insert(QStringLiteral("class"), errorClass(error.category()));
        response.insert(QStringLiteral("stage"), flowchartErrorStageName(error.stage()));
        response.insert(QStringLiteral("code"), flowchartErrorCodeName(error.code()));
        response.insert(QStringLiteral("line"), error.line());
        response.insert(QStringLiteral("column"), error.column());
        response.insert(QStringLiteral("offset"), static_cast<qint64>(error.offset()));
        response.insert(QStringLiteral("length"), static_cast<qint64>(error.length()));
        response.insert(QStringLiteral("production"), error.diagnostic().production);
        response.insert(QStringLiteral("actual"), error.diagnostic().actual);
        response.insert(QStringLiteral("expected"),
                        QJsonArray::fromStringList(error.diagnostic().expected));
        response.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
      }
    }
    output << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    output.flush();
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc == 2 && QByteArray(argv[1]) == QByteArrayLiteral("--oracle")) return runOracle();
  require(argc == 2, QStringLiteral("Expected differential fuzz fixture path"));
  const QString overrideFixture = qEnvironmentVariable("MUFFIN_FLOWCHART_DIFFERENTIAL_FIXTURE");
  QFile file(overrideFixture.isEmpty() ? QString::fromLocal8Bit(argv[1]) : overrideFixture);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open %1").arg(file.fileName()));
  configureFailureArtifacts(file.fileName());
  QJsonParseError error;
  const QJsonDocument fixture = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && fixture.isObject(),
          QStringLiteral("Invalid differential fuzz fixture"));

  const QJsonObject root = fixture.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Differential fuzz oracle version drifted"));
  const QJsonObject generator = root.value(QStringLiteral("generator")).toObject();
  const quint32 seed = generator.value(QStringLiteral("seed")).toVariant().toULongLong();
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == generator.value(QStringLiteral("caseCount")).toInt() && cases.size() >= 128,
          QStringLiteral("Differential fuzz corpus is incomplete"));

  QSet<int> coveredProductions;
  QSet<QString> coveredPairs;
  for (const QJsonValue& value : cases) {
    const QJsonObject item = value.toObject();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString source = item.value(QStringLiteral("source")).toString();
    QStringList featureList;
    for (const QJsonValue& feature : item.value(QStringLiteral("features")).toArray())
      featureList.push_back(feature.toString());
    const QString features = featureList.join(QStringLiteral(", "));
    QList<int> productions;
    for (const QJsonValue& production : item.value(QStringLiteral("productions")).toArray()) {
      productions.push_back(production.toInt());
      coveredProductions.insert(production.toInt());
    }
    require(!productions.isEmpty(), QStringLiteral("Differential case has no production feedback: %1").arg(id));
    for (qsizetype i = 0; i < productions.size(); ++i)
      for (qsizetype j = i + 1; j < productions.size(); ++j)
        coveredPairs.insert(QStringLiteral("%1:%2").arg(productions.at(i)).arg(productions.at(j)));
    QJsonObject actual;
    try {
      actual = Flowchart::parse(source).toJson();
    } catch (const std::exception& parseError) {
      failDifferential(
          QStringLiteral("Differential fuzz parse failure\ncase: %1\nseed: 0x%2\nfeatures: %3\nsource:\n%4\nerror: %5")
              .arg(id).arg(seed, 0, 16).arg(features, source,
                           QString::fromUtf8(parseError.what())),
          item);
    }
    const QJsonObject expected = item.value(QStringLiteral("expected")).toObject();
    if (actual != expected) {
      failDifferential(
          QStringLiteral("Differential fuzz mismatch\ncase: %1\nseed: 0x%2\nfeatures: %3\nsource:\n%4\nNative:\n%5\nUpstream:\n%6")
              .arg(id).arg(seed, 0, 16).arg(features, source,
                           json(actual), json(expected)),
          item);
    }
  }
  require(coveredProductions.size() == generator.value(QStringLiteral("productionCount")).toInt(),
          QStringLiteral("Differential production coverage metadata drifted"));
  require(coveredProductions.size() == generator.value(QStringLiteral("targetProductionCount")).toInt(),
          QStringLiteral("Differential corpus does not cover every successful production"));
  require(coveredPairs.size() == generator.value(QStringLiteral("productionPairCount")).toInt(),
          QStringLiteral("Differential production-pair coverage metadata drifted"));

  const QJsonArray negativeCases = root.value(QStringLiteral("negativeCases")).toArray();
  require(negativeCases.size() == generator.value(QStringLiteral("negativeCaseCount")).toInt() &&
              negativeCases.size() >= 16,
          QStringLiteral("Differential negative corpus is incomplete"));
  QJsonObject actualClassCounts;
  QSet<int> negativeOriginProductions;
  QSet<QString> mutationOperators;
  QSet<QString> diagnosticCodes;
  QSet<QString> diagnosticMatrix;
  QHash<QString, int> diagnosticCodeCounts;
  QHash<QString, int> diagnosticMatrixCounts;
  int stableLineCount = 0;
  int stableColumnCount = 0;
  QStringList positionMismatches;
  QJsonObject positionFailureItem;
  for (const QJsonValue& value : negativeCases) {
    const QJsonObject item = value.toObject();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString source = item.value(QStringLiteral("source")).toString();
    const QJsonObject upstreamError = item.value(QStringLiteral("upstreamError")).toObject();
    const QString mutationOperator = item.value(QStringLiteral("operator")).toString();
    mutationOperators.insert(mutationOperator);
    for (const QJsonValue& production : item.value(QStringLiteral("originProductions")).toArray())
      negativeOriginProductions.insert(production.toInt());
    const QString expectedClass = upstreamError.value(QStringLiteral("class")).toString();
    bool rejected = false;
    try {
      (void)Flowchart::parse(source);
    } catch (const muffin::mermaid::flowchart::FlowchartParseError& nativeError) {
      rejected = true;
      const QString actualClass = errorClass(nativeError.category());
      if (actualClass != expectedClass) {
        failDifferential(QStringLiteral("Differential negative error-class mismatch\ncase: %1\nsource:\n%2\n"
                            "native class: %3 (category=%4, line=%5, column=%6)\n"
                            "upstream class: %7 (line=%8, summary=%9)")
                 .arg(id, source, actualClass)
                 .arg(static_cast<int>(nativeError.category()))
                 .arg(nativeError.line()).arg(nativeError.column())
                 .arg(expectedClass)
                 .arg(upstreamError.value(QStringLiteral("raw")).toObject()
                          .value(QStringLiteral("line")).toInt())
                 .arg(upstreamError.value(QStringLiteral("summary")).toString()), item);
      }
      if (nativeError.code() == FlowchartErrorCode::Generic)
        failDifferential(
            QStringLiteral("Differential negative case has a generic diagnostic code: %1\n%2")
                .arg(id, QString::fromUtf8(nativeError.what())), item);
      const QString upstreamStage = upstreamError.value(QStringLiteral("stage")).toString();
      const QString nativeStage = flowchartErrorStageName(nativeError.stage());
      const QString nativeCode = flowchartErrorCodeName(nativeError.code());
      diagnosticCodes.insert(nativeCode);
      ++diagnosticCodeCounts[nativeCode];
      const QJsonArray originProductions = item.value(QStringLiteral("originProductions")).toArray();
      if (originProductions.isEmpty()) {
        const QString key = QStringLiteral("curated|%1|%2|%3")
                                .arg(mutationOperator, upstreamStage, nativeCode);
        diagnosticMatrix.insert(key);
        ++diagnosticMatrixCounts[key];
      } else {
        for (const QJsonValue& production : originProductions) {
          const QString key = QStringLiteral("%1|%2|%3|%4")
                                  .arg(production.toInt()).arg(mutationOperator,
                                       upstreamStage, nativeCode);
          diagnosticMatrix.insert(key);
          ++diagnosticMatrixCounts[key];
        }
      }
      const bool compatibleStage = upstreamStage == nativeStage ||
                                   (upstreamStage == QLatin1String("parser") &&
                                    nativeStage == QLatin1String("lexer"));
      if (!compatibleStage)
        failDifferential(
            QStringLiteral("Differential negative stage mismatch for %1: native=%2 upstream=%3")
                .arg(id, nativeStage, upstreamStage), item);
      const QJsonObject expectedPosition =
          upstreamStage == QLatin1String("parser") && nativeStage == QLatin1String("lexer")
            ? upstreamError.value(QStringLiteral("refinement")).toObject()
            : upstreamError.value(QStringLiteral("normalized")).toObject();
      const bool compareLine = upstreamError.value(QStringLiteral("compareLine")).toBool();
      const bool compareColumn = upstreamError.value(QStringLiteral("compareColumn")).toBool();
      if (compareLine)
        ++stableLineCount;
      if (compareLine && nativeError.line() != expectedPosition.value(QStringLiteral("line")).toInt()) {
        positionMismatches.push_back(
            QStringLiteral("%1 line: native=%2 upstream=%3 (%4)")
                .arg(id).arg(nativeError.line())
                .arg(expectedPosition.value(QStringLiteral("line")).toInt())
                .arg(expectedPosition.value(QStringLiteral("basis")).toString()));
        if (positionFailureItem.isEmpty()) positionFailureItem = item;
      }
      if (compareColumn)
        ++stableColumnCount;
      if (compareColumn && nativeError.column() != expectedPosition.value(QStringLiteral("column")).toInt()) {
        positionMismatches.push_back(
            QStringLiteral("%1 column: native=%2 upstream=%3 (%4)")
                .arg(id).arg(nativeError.column())
                .arg(expectedPosition.value(QStringLiteral("column")).toInt())
                .arg(expectedPosition.value(QStringLiteral("basis")).toString()));
        if (positionFailureItem.isEmpty()) positionFailureItem = item;
      }
      actualClassCounts.insert(actualClass, actualClassCounts.value(actualClass).toInt() + 1);
    } catch (const std::exception& nativeError) {
      failDifferential(QStringLiteral("Differential negative case threw an unclassified native exception\n"
                          "case: %1\nsource:\n%2\nerror: %3")
               .arg(id, source, QString::fromUtf8(nativeError.what())), item);
    }
    if (!rejected)
      failDifferential(QStringLiteral("Differential negative case succeeded natively: %1\n%2")
                           .arg(id, source), item);
  }
  require(actualClassCounts == generator.value(QStringLiteral("negativeClassCounts")).toObject(),
          QStringLiteral("Differential negative error-class metadata drifted"));
  require(negativeOriginProductions.size() ==
              generator.value(QStringLiteral("negativeOriginProductionCount")).toInt(),
          QStringLiteral("Differential negative origin-production metadata drifted"));
  if (!positionMismatches.isEmpty())
    failDifferential(QStringLiteral("Differential negative position mismatches:\n%1")
                         .arg(positionMismatches.join(QLatin1Char('\n'))),
                     positionFailureItem);
  const QJsonObject diagnosticCoverage = root.value(QStringLiteral("diagnosticCoverage")).toObject();
  for (const QJsonValue& code : diagnosticCoverage.value(QStringLiteral("requiredCodes")).toArray())
    require(diagnosticCodes.contains(code.toString()),
            QStringLiteral("Required flowchart diagnostic code is uncovered: %1")
                .arg(code.toString()));
  QJsonObject actualCodeCounts;
  for (auto it = diagnosticCodeCounts.cbegin(); it != diagnosticCodeCounts.cend(); ++it)
    actualCodeCounts.insert(it.key(), it.value());
  const QJsonObject expectedCodeCounts =
      diagnosticCoverage.value(QStringLiteral("codeCounts")).toObject();
  require(actualCodeCounts == expectedCodeCounts,
          QStringLiteral("Flowchart diagnostic code coverage changed\nexpected: %1\nactual: %2")
              .arg(QString::fromUtf8(QJsonDocument(expectedCodeCounts).toJson(QJsonDocument::Compact)),
                   QString::fromUtf8(QJsonDocument(actualCodeCounts).toJson(QJsonDocument::Compact))));
  QJsonObject actualMatrixCounts;
  for (auto it = diagnosticMatrixCounts.cbegin(); it != diagnosticMatrixCounts.cend(); ++it)
    actualMatrixCounts.insert(it.key(), it.value());
  require(actualMatrixCounts == diagnosticCoverage.value(QStringLiteral("matrixCounts")).toObject(),
          QStringLiteral("Flowchart production/operator/stage/code matrix changed"));
  require(diagnosticMatrix.size() == diagnosticCoverage.value(QStringLiteral("matrixEntryCount")).toInt(),
          QStringLiteral("Flowchart diagnostic matrix coverage decreased"));
  require(stableLineCount == diagnosticCoverage.value(QStringLiteral("stableLineCount")).toInt(),
          QStringLiteral("Stable error-line differential coverage changed"));
  require(stableColumnCount == diagnosticCoverage.value(QStringLiteral("stableColumnCount")).toInt(),
          QStringLiteral("Stable error-column differential coverage changed"));
  for (const QString& requiredOperator : {
           QStringLiteral("delete-required-terminal"),
           QStringLiteral("replace-terminal-class"),
           QStringLiteral("duplicate-separator"),
           QStringLiteral("truncate-recursive-production"),
           QStringLiteral("break-paired-delimiter"),
           QStringLiteral("insert-nullable-boundary-token")})
    require(mutationOperators.contains(requiredOperator),
            QStringLiteral("Production-aware mutation operator is uncovered: %1")
                .arg(requiredOperator));
  qDebug().noquote() << "Flowchart diagnostic codes:" << QStringList(diagnosticCodes.values()).join(", ")
                     << "matrix entries:" << diagnosticMatrix.size();
  qDebug().noquote() << QStringLiteral(
                            "Flowchart differential fuzz: %1 positive + %2 negative cases matched "
                            "Mermaid 11.16.0 (seed=0x%3, %4 productions)")
                            .arg(cases.size()).arg(negativeCases.size()).arg(seed, 0, 16)
                            .arg(coveredProductions.size());
  return 0;
}
