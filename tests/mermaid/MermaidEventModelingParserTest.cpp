#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"

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
QJsonValue nullable(const QString& value) {
  return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}
QJsonArray strings(const QVector<QString>& values) {
  QJsonArray result;
  for (const QString& value : values) result.append(value);
  return result;
}
QJsonObject ast(const eventmodeling::EventModelingData& data) {
  QJsonObject result{{QStringLiteral("title"), data.title},
                     {QStringLiteral("accTitle"), data.accTitle},
                     {QStringLiteral("accDescr"), data.accDescr},
                     {QStringLiteral("modelEntities"), strings(data.modelEntities)}};
  QJsonArray frames;
  for (const auto& frame : data.frames)
    frames.append(QJsonObject{
        {QStringLiteral("kind"), frame.reset ? QStringLiteral("EmResetFrame")
                                               : QStringLiteral("EmTimeFrame")},
        {QStringLiteral("name"), frame.name},
        {QStringLiteral("modelEntityType"), frame.modelEntityType},
        {QStringLiteral("entityIdentifier"), frame.entityIdentifier},
        {QStringLiteral("sourceFrames"), strings(frame.sourceFrames)},
        {QStringLiteral("dataReference"), nullable(frame.dataReference)},
        {QStringLiteral("dataType"), nullable(frame.dataType)},
        {QStringLiteral("dataInlineValue"), nullable(frame.dataInlineValue)}});
  result[QStringLiteral("frames")] = frames;
  QJsonArray entities;
  for (const auto& entity : data.dataEntities)
    entities.append(QJsonObject{{QStringLiteral("name"), entity.name},
                                {QStringLiteral("dataType"), nullable(entity.dataType)},
                                {QStringLiteral("dataBlockValue"), entity.dataBlockValue}});
  result[QStringLiteral("dataEntities")] = entities;
  QJsonArray notes;
  for (const auto& note : data.notes)
    notes.append(QJsonObject{{QStringLiteral("sourceFrame"), note.sourceFrame},
                             {QStringLiteral("dataType"), nullable(note.dataType)},
                             {QStringLiteral("dataBlockValue"), note.dataBlockValue}});
  result[QStringLiteral("notes")] = notes;
  QJsonArray gwt;
  for (const auto& item : data.gwt) {
    const auto names = [](const QVector<eventmodeling::EventModelingGwtStatement>& values) {
      QJsonArray result;
      for (const auto& value : values) result.append(value.entityIdentifier);
      return result;
    };
    gwt.append(QJsonObject{{QStringLiteral("sourceFrame"), item.sourceFrame},
                           {QStringLiteral("given"), names(item.given)},
                           {QStringLiteral("when"), names(item.when)},
                           {QStringLiteral("then"), names(item.then)}});
  }
  result[QStringLiteral("gwt")] = gwt;
  return result;
}
QByteArray compact(const QJsonObject& value) {
  return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Event Modeling grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("6ad15afd09231f10075a25ebdfb93c41bcdc37e107443ca942a8d62fd1d201fa"),
          QStringLiteral("Event Modeling grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("1d4fba6b8d5feff98a41b328fb03bdb91bdd998ff0dcf718c05e11863ccf55c5"),
          QStringLiteral("Event Modeling grammar fixture provenance changed"));
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
                 QLatin1String("eventmodeling");
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
      eventmodeling::EventModelingData data;
      try {
        data = eventmodeling::EventModelingDiagram::parse(pre.code);
      } catch (const eventmodeling::EventModelingParseError& error) {
        fail(QStringLiteral("%1: accepted input threw %2:%3 %4")
                 .arg(id).arg(error.line).arg(error.column)
                 .arg(QString::fromUtf8(error.what())));
      }
      const QByteArray actual = compact(ast(data));
      const QByteArray expected = compact(fixture.value(QStringLiteral("ast")).toObject());
      require(actual == expected,
              id + QStringLiteral(": AST mismatch\nactual=") + actual +
                  QStringLiteral("\nexpected=") + expected);
      continue;
    }
    ++rejected;
    const QJsonObject expected = fixture.value(QStringLiteral("reject")).toObject();
    bool threw = false;
    try {
      (void)eventmodeling::EventModelingDiagram::parse(pre.code);
    } catch (const eventmodeling::EventModelingParseError& error) {
      threw = true;
      const QString kind = error.kind == eventmodeling::EventModelingErrorKind::Lexer
                               ? QStringLiteral("lexer")
                               : QStringLiteral("parser");
      require(kind == expected.value(QStringLiteral("kind")).toString(),
              id + QStringLiteral(": error kind"));
      const int expectedLine = expected.value(QStringLiteral("line")).toInt();
      const int expectedColumn = expected.value(QStringLiteral("column")).toInt();
      if (expectedLine > 0)
        require(error.line == expectedLine, id + QStringLiteral(": error line"));
      if (expectedColumn > 0)
        require(error.column == expectedColumn, id + QStringLiteral(": error column"));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }
  require(cases.size() == 37 && accepted == 25 && rejected == 12,
          QStringLiteral("Event Modeling grammar table not fully visited"));
  std::puts("MermaidEventModelingParserTest: 37 source-entry cases passed");
  return 0;
}
