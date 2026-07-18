#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceTokenizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid::sequence;

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
  require(argc == 2, QStringLiteral("Expected sequence DB fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open sequence fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Sequence fixture version drifted"));
  require(root.value(QStringLiteral("fixtureDigest")).toString() ==
              QLatin1String("b84a517e40ec88a0b3beaf150bd4cdedc7c0f96d63bc63a306b1be5600e53a01"),
          QStringLiteral("Sequence fixture changed; audit DB/production diagnostics and update its digest"));

  const QJsonArray productions = root.value(QStringLiteral("productions")).toArray();
  require(productions.size() == 105,
          QStringLiteral("Expected all 105 sequenceDiagram.jison productions"));
  int coveredProductions = 0;
  int unreachableProductions = 0;
  int negativeOnlyProductions = 0;
  for (qsizetype i = 0; i < productions.size(); ++i) {
    const QJsonObject production = productions[i].toObject();
    require(production.value(QStringLiteral("id")).toInt() == i + 1 &&
                !production.value(QStringLiteral("lhs")).toString().isEmpty() &&
                !production.value(QStringLiteral("native")).toString().isEmpty(),
            QStringLiteral("Sequence production table is incomplete at %1").arg(i + 1));
    if (production.value(QStringLiteral("status")).toString() == QLatin1String("covered")) {
      ++coveredProductions;
      require(!production.value(QStringLiteral("fixtures")).toArray().isEmpty(),
              QStringLiteral("Covered sequence production has no fixture"));
    } else {
      const QString status = production.value(QStringLiteral("status")).toString();
      require(status == QLatin1String("unreachable") || status == QLatin1String("negative-only"),
              QStringLiteral("Sequence production %1 remains unclassified").arg(i + 1));
      if (status == QLatin1String("unreachable")) ++unreachableProductions;
      else ++negativeOnlyProductions;
      require(!production.value(QStringLiteral("reason")).toString().isEmpty(),
              QStringLiteral("Uncovered sequence production is unclassified"));
    }
  }
  require(coveredProductions == 98 && unreachableProductions == 6 &&
              negativeOnlyProductions == 1,
          QStringLiteral("Sequence production matrix regressed: %1 covered, %2 unreachable, %3 negative")
              .arg(coveredProductions).arg(unreachableProductions).arg(negativeOnlyProductions));

  QSet<QString> ids;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() >= 13, QStringLiteral("Sequence DB corpus is unexpectedly small"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!ids.contains(id), QStringLiteral("Duplicate sequence fixture id: %1").arg(id));
    ids.insert(id);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QVector<SequenceToken> tokens = SequenceTokenizer(source).tokenize();
    require(!tokens.isEmpty() && tokens.first().kind == SequenceTokenKind::Header,
            QStringLiteral("Sequence tokenizer missed header for %1").arg(id));
    for (const SequenceToken& token : tokens)
      require(token.kind != SequenceTokenKind::Invalid,
              QStringLiteral("Invalid sequence token in %1 at %2:%3: %4")
                  .arg(id).arg(token.line).arg(token.column).arg(token.text));

    QJsonObject actual;
    try {
      actual = SequenceDiagram::parse(source).toJson();
    } catch (const std::exception& error) {
      fail(QStringLiteral("Sequence parser failed for %1: %2")
               .arg(id, QString::fromUtf8(error.what())));
    }
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    require(actual == expected,
            QStringLiteral("Sequence DB mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id, json(actual), json(expected)));
  }

  {
    const QString mathLabel = QStringLiteral(
        "sequenceDiagram\nA->>B:start\n"
        "Note over A,B:$$p+\\sum_{i=1}^{n}+q:100%#ok$$");
    const QVector<SequenceToken> tokens = SequenceTokenizer(mathLabel).tokenize();
    require(std::none_of(tokens.cbegin(), tokens.cend(),
                         [](const SequenceToken& token) {
                           return token.kind == SequenceTokenKind::Invalid;
                         }),
            QStringLiteral("Math label lexical state emitted an invalid token"));
    const SequenceDiagram diagram = SequenceDiagram::parse(mathLabel);
    require(!diagram.data().messages.isEmpty() &&
                diagram.data().messages.back().message.toString() ==
                    QLatin1String("$$p+\\sum_{i=1}^{n}+q:100%#ok$$"),
            QStringLiteral("Math label lexical state lost opaque punctuation"));
  }

  QSet<int> diagnosticCodes;
  for (const QJsonValue& value : root.value(QStringLiteral("invalidCases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    if (!fixture.value(QStringLiteral("rejected")).toBool()) continue;
    bool rejected = false;
    try {
      SequenceDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    } catch (const SequenceParseError& error) {
      rejected = true;
      diagnosticCodes.insert(static_cast<int>(error.diagnostic().code));
      require(error.diagnostic().span.line >= 1 && error.diagnostic().span.column >= 0,
              QStringLiteral("Sequence diagnostic lacks a stable span"));
      require(error.diagnostic().code != SequenceErrorCode::Generic,
              QStringLiteral("Sequence diagnostic fell back to generic code"));
    }
    require(rejected, QStringLiteral("Native accepted upstream-invalid sequence case: %1")
                          .arg(fixture.value(QStringLiteral("id")).toString()));
  }

  for (const SequenceErrorCode code : {
           SequenceErrorCode::MissingHeader,
           SequenceErrorCode::UnexpectedToken,
           SequenceErrorCode::MissingEnd,
           SequenceErrorCode::UnexpectedEnd,
           SequenceErrorCode::InactiveParticipant,
           SequenceErrorCode::InvalidCreateMessage,
           SequenceErrorCode::InvalidDestroyMessage,
           SequenceErrorCode::DuplicateParticipant,
       })
    require(diagnosticCodes.contains(static_cast<int>(code)),
            QStringLiteral("Sequence diagnostic code lacks an upstream-invalid case: %1")
                .arg(sequenceErrorCodeName(code)));

  try {
    SequenceLimits limits;
    limits.maxActors = 1;
    SequenceDiagram::parse(QStringLiteral("sequenceDiagram\nA->>B:too many"), limits);
    fail(QStringLiteral("Sequence actor resource limit was not enforced"));
  } catch (const SequenceParseError& error) {
    require(error.diagnostic().code == SequenceErrorCode::LimitExceeded,
            QStringLiteral("Sequence resource limit has the wrong diagnostic code"));
  }

  qDebug() << "MermaidSequenceParserTest:" << cases.size() << "DB cases," <<
      coveredProductions << "of" << productions.size() << "productions covered";
  return 0;
}
