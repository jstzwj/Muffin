// Packet Langium/parser-DB oracle captured live from Mermaid 11.16.0.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/packet/PacketDiagram.h"
#include "blocks/html/HtmlSanitizer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

double oracleNumber(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  const QString text = value.toString();
  if (text == QLatin1String("NaN"))
    return std::numeric_limits<double>::quiet_NaN();
  if (text == QLatin1String("Infinity"))
    return std::numeric_limits<double>::infinity();
  if (text == QLatin1String("-Infinity"))
    return -std::numeric_limits<double>::infinity();
  bool ok = false;
  const double result = text.toDouble(&ok);
  require(ok, QStringLiteral("invalid packet oracle number: ") + text);
  return result;
}

void compareNumber(qreal actual, const QJsonValue& expected,
                   const QString& path) {
  const double wanted = oracleNumber(expected);
  if (std::isnan(wanted)) {
    require(std::isnan(actual), path + QStringLiteral(": expected NaN"));
    return;
  }
  if (std::isinf(wanted)) {
    require(std::isinf(actual) &&
                std::signbit(actual) == std::signbit(wanted),
            path + QStringLiteral(": infinity mismatch"));
    return;
  }
  require(double(actual) == wanted,
          path + QStringLiteral(": %1 != %2")
                     .arg(double(actual), 0, 'g', 17)
                     .arg(wanted, 0, 'g', 17));
}

void compareWord(const packet::PacketWord& actual, const QJsonArray& expected,
                 const QString& path) {
  require(actual.size() == expected.size(),
          path + QStringLiteral(" count %1 != %2")
                     .arg(actual.size())
                     .arg(expected.size()));
  for (qsizetype i = 0; i < actual.size(); ++i) {
    const QJsonObject block = expected.at(i).toObject();
    const QString blockPath = path + QStringLiteral("/%1").arg(i);
    compareNumber(actual.at(i).start, block.value(QStringLiteral("start")),
                  blockPath + QStringLiteral("/start"));
    compareNumber(actual.at(i).end, block.value(QStringLiteral("end")),
                  blockPath + QStringLiteral("/end"));
    compareNumber(actual.at(i).bits, block.value(QStringLiteral("bits")),
                  blockPath + QStringLiteral("/bits"));
    require(actual.at(i).label == block.value(QStringLiteral("label")).toString(),
            blockPath + QStringLiteral("/label mismatch"));
  }
}

void compareDb(const packet::PacketData& actual, const QJsonObject& expected,
               const QString& id) {
  const auto compareText = [&](const QString& field, const QString& actualText,
                               const QString& expectedText) {
    require(actualText == expectedText,
            id + QLatin1Char('/') + field +
                QStringLiteral(" mismatch: actual [%1], expected [%2]")
                    .arg(actualText, expectedText));
  };
  compareText(QStringLiteral("title"), actual.title,
              expected.value(QStringLiteral("title")).toString());
  compareText(QStringLiteral("accTitle"), actual.accTitle,
              expected.value(QStringLiteral("accTitle")).toString());
  compareText(QStringLiteral("accDescription"), actual.accDescr,
              expected.value(QStringLiteral("accDescription")).toString());
  const qsizetype expectedCount =
      qsizetype(expected.value(QStringLiteral("wordCount")).toDouble());
  require(actual.words.size() == expectedCount,
          id + QStringLiteral("/wordCount %1 != %2")
                   .arg(actual.words.size())
                   .arg(expectedCount));

  const QJsonValue wordsValue = expected.value(QStringLiteral("words"));
  if (wordsValue.isArray()) {
    const QJsonArray words = wordsValue.toArray();
    require(actual.words.size() == words.size(),
            id + QStringLiteral("/words fixture count mismatch"));
    for (qsizetype i = 0; i < actual.words.size(); ++i)
      compareWord(actual.words.at(i), words.at(i).toArray(),
                  id + QStringLiteral("/words/%1").arg(i));
  } else if (!actual.words.isEmpty()) {
    compareWord(actual.words.first(),
                expected.value(QStringLiteral("firstWord")).toArray(),
                id + QStringLiteral("/firstWord"));
    compareWord(actual.words.last(),
                expected.value(QStringLiteral("lastWord")).toArray(),
                id + QStringLiteral("/lastWord"));
  }
}

packet::PacketErrorKind expectedKind(const QString& value) {
  if (value == QLatin1String("lexer")) return packet::PacketErrorKind::Lexer;
  if (value == QLatin1String("parser")) return packet::PacketErrorKind::Parser;
  return packet::PacketErrorKind::Runtime;
}

QJsonValue parseBitsPerRow(const MermaidPreprocessResult& pre) {
  return pre.config.value(QStringLiteral("packet"))
      .toObject()
      .value(QStringLiteral("bitsPerRow"));
}

QJsonValue effectiveBitsPerRow(const QJsonValue& raw) {
  return raw.isUndefined() || raw.isNull() || raw.isArray() || raw.isObject()
             ? QJsonValue(32.0)
             : raw;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected packet grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray fixtureBytes = file.readAll();
  QByteArray canonicalFixtureBytes = fixtureBytes;
  canonicalFixtureBytes.replace("\r\n", "\n");
  canonicalFixtureBytes.replace('\r', '\n');
  require(QCryptographicHash::hash(canonicalFixtureBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral(
                  "55833af2ddd6e9dc228ce65ebcf4a83b9eaafe3e18b22bf6f54892e5a0dbf953"),
          QStringLiteral("Packet grammar fixture bytes changed; regenerate and audit"));
  QJsonParseError jsonError;
  const QJsonObject root =
      QJsonDocument::fromJson(fixtureBytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Packet grammar JSON: ") + jsonError.errorString());
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Packet grammar Mermaid version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("8c3ede49b5d398a8114f1c3c0f4030d7628b3ed6be44cae5cf1def5f435a0ad8"),
          QStringLiteral("Packet grammar fixture changed; audit its contract"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);

    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) ==
                 QLatin1String("packet");
    } catch (const UnknownDiagramError&) {
    }

    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source was not detected"));
      require(fixture.value(QStringLiteral("reject")).toObject()
                      .value(QStringLiteral("class")).toString() ==
                  QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    const QJsonValue rawBits = parseBitsPerRow(pre);
    if (accept) {
      ++accepted;
      packet::PacketData data;
      try {
        data = packet::PacketDiagram::parse(pre.code, rawBits);
      } catch (const packet::PacketParseError& error) {
        fail(id + QStringLiteral(": accepted input threw at %1:%2: %3")
                      .arg(error.line)
                      .arg(error.column)
                      .arg(QString::fromUtf8(error.what())));
      }
      if (data.title.isEmpty() && pre.hasTitle)
        data.title = muffin::HtmlSanitizer().sanitizedMermaidText(pre.title);
      const QJsonObject expectedDb =
          fixture.value(QStringLiteral("expectedDb")).toObject();
      compareDb(data, expectedDb, id);
      require(effectiveBitsPerRow(rawBits) ==
                  expectedDb.value(QStringLiteral("config")).toObject()
                      .value(QStringLiteral("bitsPerRow")),
              id + QStringLiteral("/preprocessed bitsPerRow mismatch"));
      continue;
    }

    ++rejected;
    const QJsonObject reject =
        fixture.value(QStringLiteral("reject")).toObject();
    const QString rejectClass = reject.value(QStringLiteral("class")).toString();
    bool threw = false;
    try {
      (void)packet::PacketDiagram::parse(pre.code, rawBits);
    } catch (const packet::PacketParseError& error) {
      threw = true;
      require(error.line >= 1 && error.column >= 1,
              id + QStringLiteral(": native diagnostic lacks location"));
      require(error.kind == expectedKind(rejectClass),
              id + QStringLiteral(": error kind mismatch"));
      const int oracleLine = reject.value(QStringLiteral("line")).toInt();
      const int oracleColumn = reject.value(QStringLiteral("column")).toInt();
      if (oracleLine > 0)
        require(error.line == oracleLine && error.column == oracleColumn,
                id + QStringLiteral(": location %1:%2 != oracle %3:%4")
                         .arg(error.line)
                         .arg(error.column)
                         .arg(oracleLine)
                         .arg(oracleColumn));
      if (rejectClass == QLatin1String("runtime"))
        require(QString::fromUtf8(error.what()) ==
                    reject.value(QStringLiteral("message")).toString(),
                id + QStringLiteral(": exact DB runtime error drifted"));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }

  require(cases.size() == 69 && accepted == 42 && rejected == 27,
          QStringLiteral("Packet grammar table was not fully visited"));
  std::puts("MermaidPacketParserTest: 69 source-entry cases passed");
  return 0;
}
