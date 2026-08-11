#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QJsonObject nodeJson(const ishikawa::IshikawaNode& node) {
  QJsonArray children;
  for (const auto& child : node.children) children.append(nodeJson(child));
  return {{QStringLiteral("text"), node.text},
          {QStringLiteral("children"), children}};
}

QJsonValue rootJson(const ishikawa::IshikawaData& data) {
  return data.hasRoot ? QJsonValue(nodeJson(data.root))
                      : QJsonValue(QJsonValue::Null);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Ishikawa grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "3438bb83b17532c65b03443013ef76e0b22fcbb99873a5ff401f55ffc13eb5d7"),
          QStringLiteral("Ishikawa grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "c111ef042cef8c257123dcae7fef7bd13c696322fb824a9a69b1104c46b76919"),
          QStringLiteral("Ishikawa grammar fixture provenance changed"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  int accepted = 0;
  int rejected = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const MermaidPreprocessResult pre =
        preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) ==
                 QLatin1String("ishikawa");
    } catch (const UnknownDiagramError&) {
    }
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source not detected"));
      require(fixture.value(QStringLiteral("reject")).toObject()
                      .value(QStringLiteral("kind")).toString() ==
                  QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    if (accept) {
      ++accepted;
      ishikawa::IshikawaData data;
      try {
        data = ishikawa::IshikawaDiagram::parse(pre.code);
      } catch (const ishikawa::IshikawaParseError& error) {
        fail(QStringLiteral("%1: accepted input threw %2:%3 %4")
                 .arg(id)
                 .arg(error.line)
                 .arg(error.column)
                 .arg(QString::fromUtf8(error.what())));
      }
      const QJsonObject expected = fixture.value(QStringLiteral("db")).toObject();
      require(rootJson(data) == expected.value(QStringLiteral("root")),
              id + QStringLiteral(": tree DB mismatch"));
      require(data.title == expected.value(QStringLiteral("title")).toString(),
              id + QStringLiteral(": title mismatch"));
      require(data.accTitle ==
                  expected.value(QStringLiteral("accTitle")).toString(),
              id + QStringLiteral(": accTitle mismatch"));
      require(data.accDescr ==
                  expected.value(QStringLiteral("accDescr")).toString(),
              id + QStringLiteral(": accDescr mismatch"));
      continue;
    }

    ++rejected;
    const QJsonObject expected = fixture.value(QStringLiteral("reject")).toObject();
    bool threw = false;
    try {
      (void)ishikawa::IshikawaDiagram::parse(pre.code);
    } catch (const ishikawa::IshikawaParseError& error) {
      threw = true;
      const QString kind =
          error.kind == ishikawa::IshikawaErrorKind::Lexer
              ? QStringLiteral("lexer")
              : QStringLiteral("parser");
      require(kind == expected.value(QStringLiteral("kind")).toString(),
              id + QStringLiteral(": error kind"));
      const int expectedLine = expected.value(QStringLiteral("line")).toInt();
      const int expectedColumn =
          expected.value(QStringLiteral("column")).toInt();
      if (expectedLine > 0)
        require(error.line == expectedLine,
                id + QStringLiteral(": error line, actual ") +
                    QString::number(error.line) + QStringLiteral(" token ") +
                    error.token + QStringLiteral(" message ") +
                    QString::fromUtf8(error.what()));
      if (expectedColumn > 0)
        require(error.column == expectedColumn,
                id + QStringLiteral(": error column, actual ") +
                    QString::number(error.column));
      const QString expectedToken =
          expected.value(QStringLiteral("token")).toString();
      if (!expectedToken.isEmpty())
        require(error.token == expectedToken,
                id + QStringLiteral(": error token, actual ") + error.token);
      require(QString::fromUtf8(error.what()) ==
                  expected.value(QStringLiteral("message")).toString(),
              id + QStringLiteral(": error message, actual ") +
                  QString::fromUtf8(error.what()));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }

  require(cases.size() == 30 && accepted == 26 && rejected == 4,
          QStringLiteral("Ishikawa grammar table not fully visited"));
  std::puts("MermaidIshikawaParserTest: 30 source-entry cases passed");
  return 0;
}
