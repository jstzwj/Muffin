#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/info/InfoDiagram.h"

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
info::InfoErrorKind expectedKind(const QString& value) {
  return value == QLatin1String("lexer") ? info::InfoErrorKind::Lexer
                                         : info::InfoErrorKind::Parser;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Info grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("db5cfd28f6fb0058b1a028942277727e9448ee24f20571ebfcee9daa77ac987e"),
          QStringLiteral("Info grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("b559ee3e6394d827c2c4be61992afdce5443dd915ae33e7a66e3dc901ab9c1bc"),
          QStringLiteral("Info grammar fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 20, QStringLiteral("Expected 20 Info grammar cases"));

  int accepted = 0;
  int rejected = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("info");
    } catch (const UnknownDiagramError&) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source was not detected"));
      require(fixture.value(QStringLiteral("reject")).toObject()
                      .value(QStringLiteral("kind")).toString() ==
                  QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    if (accept) {
      ++accepted;
      info::InfoData data;
      try {
        data = info::InfoDiagram::parse(pre.code);
      } catch (const info::InfoParseError& error) {
        fail(id + QStringLiteral(": accepted source threw at %1:%2: %3")
                      .arg(error.line)
                      .arg(error.column)
                      .arg(QString::fromUtf8(error.what())));
      }
      const QJsonObject ast = fixture.value(QStringLiteral("ast")).toObject();
      require(data.title == ast.value(QStringLiteral("title")).toString(),
              id + QStringLiteral("/title"));
      require(data.accTitle == ast.value(QStringLiteral("accTitle")).toString(),
              id + QStringLiteral("/accTitle"));
      require(data.accDescr == ast.value(QStringLiteral("accDescr")).toString(),
              id + QStringLiteral("/accDescr"));
      continue;
    }

    ++rejected;
    const QJsonObject expected = fixture.value(QStringLiteral("reject")).toObject();
    bool threw = false;
    try {
      (void)info::InfoDiagram::parse(pre.code);
    } catch (const info::InfoParseError& error) {
      threw = true;
      require(error.kind == expectedKind(expected.value(QStringLiteral("kind")).toString()),
              id + QStringLiteral("/kind"));
      require(error.line == expected.value(QStringLiteral("line")).toInt(),
              id + QStringLiteral("/line"));
      require(error.column == expected.value(QStringLiteral("column")).toInt(),
              QStringLiteral("%1/column native=%2 upstream=%3")
                  .arg(id)
                  .arg(error.column)
                  .arg(expected.value(QStringLiteral("column")).toInt()));
      require(QString::fromUtf8(error.what()) ==
                  expected.value(QStringLiteral("message")).toString(),
              id + QStringLiteral("/message: ") + QString::fromUtf8(error.what()));
    }
    require(threw, id + QStringLiteral(": rejected source parsed"));
  }
  std::fprintf(stderr, "Info parser: %d accepted, %d rejected\n", accepted,
               rejected);
  return 0;
}
