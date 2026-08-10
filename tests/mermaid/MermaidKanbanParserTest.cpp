// Kanban Jison/parser-DB oracle captured live from Mermaid 11.16.0.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/kanban/KanbanDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

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

bool sameJson(const QJsonValue& actual, const QJsonValue& expected) {
  if (actual.isUndefined() && expected.isUndefined()) return true;
  if (actual.type() != expected.type()) return false;
  if (actual.isDouble()) return actual.toDouble() == expected.toDouble();
  return actual == expected;
}

QJsonValue nodeField(const kanban::KanbanNode& node, const QString& key) {
  if (key == QLatin1String("id")) return node.id;
  if (key == QLatin1String("level")) return node.level;
  if (key == QLatin1String("label")) return node.label;
  if (key == QLatin1String("parentId"))
    return node.parentId.isNull() ? QJsonValue(QJsonValue::Undefined)
                                  : QJsonValue(node.parentId);
  if (key == QLatin1String("width")) return node.width;
  if (key == QLatin1String("padding")) return node.padding;
  if (key == QLatin1String("isGroup")) return node.isGroup;
  // getType() is an addNode input used only to derive padding. Upstream never
  // persists it as a raw node property.
  if (key == QLatin1String("type"))
    return QJsonValue(QJsonValue::Undefined);
  if (key == QLatin1String("shape"))
    return node.shape.isNull() ? QJsonValue(QJsonValue::Undefined)
                               : QJsonValue(node.shape);
  if (key == QLatin1String("ticket"))
    return node.ticket.isNull() ? QJsonValue(QJsonValue::Undefined)
                                : QJsonValue(node.ticket);
  if (key == QLatin1String("priority")) return node.priority;
  if (key == QLatin1String("assigned"))
    return node.assigned.isNull() ? QJsonValue(QJsonValue::Undefined)
                                  : QJsonValue(node.assigned);
  if (key == QLatin1String("icon"))
    return node.icon.isNull() ? QJsonValue(QJsonValue::Undefined)
                              : QJsonValue(node.icon);
  if (key == QLatin1String("cssClasses"))
    return node.cssClasses.isNull() ? QJsonValue(QJsonValue::Undefined)
                                    : QJsonValue(node.cssClasses);
  if (key == QLatin1String("look"))
    return node.look.isNull() ? QJsonValue(QJsonValue::Undefined)
                              : QJsonValue(node.look);
  fail(QStringLiteral("unknown Kanban node fixture field: ") + key);
}

void compareNodes(const QVector<kanban::KanbanNode>& actual,
                  const QJsonArray& expected, const QString& path) {
  require(actual.size() == expected.size(),
          path + QStringLiteral(" count %1 != %2")
                     .arg(actual.size())
                     .arg(expected.size()));
  static const QStringList fields = {
      QStringLiteral("id"),         QStringLiteral("level"),
      QStringLiteral("label"),      QStringLiteral("parentId"),
      QStringLiteral("width"),      QStringLiteral("padding"),
      QStringLiteral("isGroup"),    QStringLiteral("type"),
      QStringLiteral("shape"),      QStringLiteral("ticket"),
      QStringLiteral("priority"),   QStringLiteral("assigned"),
      QStringLiteral("icon"),       QStringLiteral("cssClasses"),
      QStringLiteral("look")};
  for (qsizetype i = 0; i < actual.size(); ++i) {
    const QJsonObject wanted = expected.at(i).toObject();
    for (const QString& field : fields) {
      const QJsonValue expectedValue = wanted.contains(field)
                                           ? wanted.value(field)
                                           : QJsonValue(QJsonValue::Undefined);
      const QJsonValue actualValue = nodeField(actual.at(i), field);
      require(sameJson(actualValue, expectedValue),
              path + QStringLiteral("/%1/%2 mismatch: actual=%3 expected=%4")
                         .arg(i)
                         .arg(field,
                              QString::fromUtf8(QJsonDocument(
                                  QJsonArray{actualValue})
                                                    .toJson(QJsonDocument::Compact)),
                              QString::fromUtf8(QJsonDocument(
                                  QJsonArray{expectedValue})
                                                    .toJson(QJsonDocument::Compact))));
    }
  }
}

kanban::KanbanParseConfig parseConfig(const MermaidPreprocessResult& pre) {
  kanban::KanbanParseConfig config;
  const QJsonObject mindmap =
      pre.config.value(QStringLiteral("mindmap")).toObject();
  const QJsonValue padding = mindmap.value(QStringLiteral("padding"));
  const QJsonValue width = mindmap.value(QStringLiteral("maxNodeWidth"));
  if (!padding.isUndefined() && !padding.isNull())
    config.mindmapPadding = padding;
  if (!width.isUndefined() && !width.isNull())
    config.mindmapMaxNodeWidth = width;
  const QJsonValue look = pre.config.value(QStringLiteral("look"));
  if (look.isString()) config.look = look.toString();
  return config;
}

kanban::KanbanErrorKind expectedKind(const QString& value) {
  if (value == QLatin1String("lexer")) return kanban::KanbanErrorKind::Lexer;
  if (value == QLatin1String("yaml")) return kanban::KanbanErrorKind::Yaml;
  if (value == QLatin1String("runtime"))
    return kanban::KanbanErrorKind::Runtime;
  return kanban::KanbanErrorKind::Parser;
}

bool sourceEntryDetectsKanban(const QString& source) {
  try {
    return detectDiagramType(source) == QLatin1String("kanban");
  } catch (const UnknownDiagramError&) {
    return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Kanban grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray fixtureBytes = file.readAll();
  QByteArray canonicalFixtureBytes = fixtureBytes;
  canonicalFixtureBytes.replace("\r\n", "\n");
  canonicalFixtureBytes.replace('\r', '\n');
  require(
      QCryptographicHash::hash(canonicalFixtureBytes,
                               QCryptographicHash::Sha256)
              .toHex() ==
          QByteArrayLiteral(
              "9bcb092f476a486d395341eff28c291a09d91898f29a6aa5590a2370cfcc2d92"),
      QStringLiteral("Kanban fixture file sha256 drift"));

  QJsonParseError jsonError;
  const QJsonDocument document =
      QJsonDocument::fromJson(fixtureBytes, &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          jsonError.errorString());
  const QJsonObject root = document.object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Kanban oracle version drift"));
  require(upstream.value(QStringLiteral("moduleSha256")).toString() ==
              QLatin1String(
                  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b"),
          QStringLiteral("Mermaid module hash drift"));
  require(upstream.value(QStringLiteral("kanbanModuleSha256")).toString() ==
              QLatin1String(
                  "e72b92f9e32d5de4cdd57de283144e44bb868a82ac00871e7859f1294670ebcf"),
          QStringLiteral("Kanban chunk hash drift"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "be72acfa5f7a75e0a3b3ef2073723c6eaf6d61c1b988912a0aec4beaa10f6134"),
          QStringLiteral("Kanban semantic fixture hash drift"));

  int accepted = 0;
  int rejected = 0;
  for (const QJsonValue& caseValue :
       root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool expectedAccept = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();

    if (!sourceEntryDetectsKanban(source)) {
      require(!expectedAccept &&
                  reject.value(QStringLiteral("class")).toString() ==
                      QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    const MermaidPreprocessResult pre = preprocessDiagram(source);
    try {
      const kanban::KanbanData actual =
          kanban::KanbanDiagram::parse(pre.code, parseConfig(pre));
      require(expectedAccept, id + QStringLiteral(": unexpectedly accepted"));
      const QJsonObject expectedDb =
          fixture.value(QStringLiteral("expectedDb")).toObject();
      compareNodes(actual.sections,
                   expectedDb.value(QStringLiteral("sections")).toArray(),
                   id + QStringLiteral("/sections"));
      compareNodes(actual.nodes,
                   expectedDb.value(QStringLiteral("nodes")).toArray(),
                   id + QStringLiteral("/nodes"));
      const QJsonObject expectedConfig =
          expectedDb.value(QStringLiteral("config")).toObject();
      const kanban::KanbanParseConfig nativeConfig = parseConfig(pre);
      require(nativeConfig.look ==
                  expectedConfig.value(QStringLiteral("look")).toString(),
              id + QStringLiteral("/config/look mismatch"));
      require(sameJson(nativeConfig.mindmapPadding,
                       expectedConfig.value(QStringLiteral("mindmapPadding"))),
              id + QStringLiteral("/config/padding mismatch"));
      require(sameJson(
                  nativeConfig.mindmapMaxNodeWidth,
                  expectedConfig.value(QStringLiteral("mindmapMaxNodeWidth"))),
              id + QStringLiteral("/config/maxNodeWidth mismatch"));
      ++accepted;
    } catch (const kanban::KanbanParseError& error) {
      require(!expectedAccept,
              id + QStringLiteral(": unexpectedly rejected: ") + error.what());
      const QString rejectClass =
          reject.value(QStringLiteral("class")).toString();
      require(error.kind == expectedKind(rejectClass),
              id + QStringLiteral(": reject kind mismatch"));
      const int expectedLine = reject.value(QStringLiteral("line")).toInt();
      const int expectedColumn = reject.value(QStringLiteral("column")).toInt();
      if (expectedLine > 0)
        require(error.line == expectedLine,
                id + QStringLiteral(": line %1 != %2")
                         .arg(error.line)
                         .arg(expectedLine));
      if (expectedColumn > 0)
        require(error.column == expectedColumn,
                id + QStringLiteral(": column %1 != %2")
                         .arg(error.column)
                         .arg(expectedColumn));
      const QString expectedToken =
          reject.value(QStringLiteral("token")).toString();
      if (!expectedToken.isEmpty())
        require(error.token == expectedToken,
                id + QStringLiteral(": token %1 != %2")
                         .arg(error.token, expectedToken));
      if (error.kind == kanban::KanbanErrorKind::Runtime)
        require(QString::fromUtf8(error.what()) ==
                    reject.value(QStringLiteral("message")).toString(),
                id + QStringLiteral(": runtime message mismatch: ") +
                    QString::fromUtf8(error.what()));
      ++rejected;
    }
  }

  require(accepted == 38,
          QStringLiteral("Kanban accepted count %1 != 38").arg(accepted));
  require(rejected == 20,
          QStringLiteral("Kanban rejected count %1 != 20").arg(rejected));
  std::printf("MermaidKanbanParserTest: %d accept + %d reject cases passed\n",
              accepted, rejected);
  return 0;
}
