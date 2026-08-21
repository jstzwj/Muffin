#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/gantt/GanttDiagram.h"

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

QString localDate(const QDateTime& value) {
  // Compare the UTC instant: the golden fixture serializes UTC, so the check
  // holds on runners in any timezone (local-time rendering embedded the
  // fixture host's UTC+8 and failed everywhere else).
  return value.isValid()
             ? value.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"))
             : QString();
}

void compareStringList(const QStringList& actual, const QJsonArray& expected,
                       const QString& path) {
  require(actual.size() == expected.size(), path + QStringLiteral("/size"));
  for (qsizetype i = 0; i < actual.size(); ++i)
    require(actual.at(i) == expected.at(i).toString(),
            path + QStringLiteral("/%1").arg(i));
}

void compareTask(const gantt::GanttTask& actual, const QJsonObject& expected,
                 const QString& path) {
  require(actual.section == expected.value(QStringLiteral("section")).toString(),
          path + QStringLiteral("/section"));
  require(actual.type == expected.value(QStringLiteral("type")).toString(),
          path + QStringLiteral("/type"));
  require(actual.processed == expected.value(QStringLiteral("processed")).toBool(),
          path + QStringLiteral("/processed"));
  require(actual.manualEndTime == expected.value(QStringLiteral("manualEndTime")).toBool(),
          path + QStringLiteral("/manualEndTime"));
  const QJsonValue renderEnd = expected.value(QStringLiteral("renderEndTime"));
  require(localDate(actual.renderEndTime) ==
              (renderEnd.isNull() ? QString() : renderEnd.toString()),
          path + QStringLiteral("/renderEndTime"));
  const QJsonObject raw = expected.value(QStringLiteral("raw")).toObject();
  require(actual.rawData == raw.value(QStringLiteral("data")).toString(),
          path + QStringLiteral("/raw/data"));
  const QJsonObject rawStart = raw.value(QStringLiteral("startTime")).toObject();
  require(actual.rawStartType == rawStart.value(QStringLiteral("type")).toString(),
          path + QStringLiteral("/raw/start/type"));
  require(actual.rawStartData == rawStart.value(QStringLiteral("startData")).toString(),
          path + QStringLiteral("/raw/start/data"));
  require(actual.rawEndData == raw.value(QStringLiteral("endTime"))
                                   .toObject()
                                   .value(QStringLiteral("data"))
                                   .toString(),
          path + QStringLiteral("/raw/end/data"));
  require(actual.task == expected.value(QStringLiteral("task")).toString(),
          path + QStringLiteral("/task"));
  compareStringList(actual.classes, expected.value(QStringLiteral("classes")).toArray(),
                    path + QStringLiteral("/classes"));
  require(actual.id == expected.value(QStringLiteral("id")).toString(),
          path + QStringLiteral("/id"));
  const QJsonValue previous = expected.value(QStringLiteral("prevTaskId"));
  require(actual.prevTaskId == (previous.isNull() ? QString() : previous.toString()),
          path + QStringLiteral("/prevTaskId"));
  require(actual.active == expected.value(QStringLiteral("active")).toBool(),
          path + QStringLiteral("/active"));
  require(actual.done == expected.value(QStringLiteral("done")).toBool(),
          path + QStringLiteral("/done"));
  require(actual.crit == expected.value(QStringLiteral("crit")).toBool(),
          path + QStringLiteral("/crit"));
  require(actual.milestone == expected.value(QStringLiteral("milestone")).toBool(),
          path + QStringLiteral("/milestone"));
  require(actual.vert == expected.value(QStringLiteral("vert")).toBool(),
          path + QStringLiteral("/vert"));
  require(actual.order == expected.value(QStringLiteral("order")).toInt(),
          path + QStringLiteral("/order"));
  const QJsonValue start = expected.value(QStringLiteral("startTime"));
  const QJsonValue end = expected.value(QStringLiteral("endTime"));
  require(localDate(actual.startTime) == (start.isNull() ? QString() : start.toString()),
          path + QStringLiteral("/startTime"));
  require(localDate(actual.endTime) == (end.isNull() ? QString() : end.toString()),
          path + QStringLiteral("/endTime"));
}

void compareData(const gantt::GanttData& actual, const QJsonObject& expected,
                 const QString& id) {
  const auto string = [&](const char* key) {
    return expected.value(QLatin1String(key)).toString();
  };
  require(actual.title == string("title"), id + QStringLiteral("/title"));
  require(actual.accTitle == string("accTitle"), id + QStringLiteral("/accTitle"));
  require(actual.accDescr == string("accDescr"), id + QStringLiteral("/accDescr"));
  require(actual.dateFormat == string("dateFormat"), id + QStringLiteral("/dateFormat"));
  require(actual.axisFormat == string("axisFormat"), id + QStringLiteral("/axisFormat"));
  require(actual.tickInterval == string("tickInterval"), id + QStringLiteral("/tickInterval"));
  require(actual.todayMarker == string("todayMarker"), id + QStringLiteral("/todayMarker"));
  compareStringList(actual.includes, expected.value(QStringLiteral("includes")).toArray(),
                    id + QStringLiteral("/includes"));
  compareStringList(actual.excludes, expected.value(QStringLiteral("excludes")).toArray(),
                    id + QStringLiteral("/excludes"));
  compareStringList(actual.sections, expected.value(QStringLiteral("sections")).toArray(),
                    id + QStringLiteral("/sections"));
  require(actual.inclusiveEndDates ==
              expected.value(QStringLiteral("inclusiveEndDates")).toBool(),
          id + QStringLiteral("/inclusiveEndDates"));
  require(actual.topAxis == expected.value(QStringLiteral("topAxis")).toBool(),
          id + QStringLiteral("/topAxis"));
  require(actual.weekday == string("weekday"), id + QStringLiteral("/weekday"));
  require(actual.displayMode == string("displayMode"), id + QStringLiteral("/displayMode"));
  const QJsonArray links = expected.value(QStringLiteral("links")).toArray();
  require(actual.links.size() == links.size(), id + QStringLiteral("/links/size"));
  for (const QJsonValue& linkValue : links) {
    const QJsonArray link = linkValue.toArray();
    require(actual.links.value(link.at(0).toString()) == link.at(1).toString(),
            id + QStringLiteral("/links/") + link.at(0).toString());
  }
  const QJsonArray tasks = expected.value(QStringLiteral("tasks")).toArray();
  require(actual.tasks.size() == tasks.size(), id + QStringLiteral("/tasks/size"));
  for (qsizetype i = 0; i < actual.tasks.size(); ++i)
    compareTask(actual.tasks.at(i), tasks.at(i).toObject(),
                id + QStringLiteral("/task%1").arg(i));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Gantt grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF. Git may materialize JSON fixtures as CRLF on
  // Windows; provenance must describe content, not checkout line endings.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("e9c1798387a091efa3dfe1f0ea3658eeeefd03db1021bb00ec8feb032d99862d"),
          QStringLiteral("Gantt grammar fixture bytes changed"));
  QJsonParseError jsonError;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Gantt fixture JSON: ") + jsonError.errorString());
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Gantt Mermaid version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("f448954c123453fbfa9b4d5863b375c76366e27d28669ccb5d5eba404d9b9093"),
          QStringLiteral("Gantt fixture semantic digest drifted"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 53, QStringLiteral("Gantt grammar case count drifted"));
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();
    const QString rejectClass = reject.value(QStringLiteral("class")).toString();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(source, pre.config) == QLatin1String("gantt");
    } catch (const UnknownDiagramError&) {
      detected = false;
    }
    require(detected == (rejectClass != QLatin1String("no-diagram")),
            id + QStringLiteral("/detector"));
    if (!detected) continue;

    try {
      const gantt::GanttData data = gantt::GanttDiagram::parse(pre.code);
      require(fixture.value(QStringLiteral("accept")).toBool(),
              id + QStringLiteral(": native accepted upstream reject"));
      compareData(data, fixture.value(QStringLiteral("expectedDb")).toObject(), id);
    } catch (const gantt::GanttParseError& error) {
      require(!fixture.value(QStringLiteral("accept")).toBool(),
              id + QStringLiteral(": native reject %1:%2 %3")
                       .arg(error.line).arg(error.column)
                       .arg(QString::fromUtf8(error.what())));
      const gantt::GanttErrorKind expectedKind =
          rejectClass == QLatin1String("lexer") ? gantt::GanttErrorKind::Lexer
          : rejectClass == QLatin1String("runtime") ? gantt::GanttErrorKind::Runtime
                                                     : gantt::GanttErrorKind::Parser;
      require(error.kind == expectedKind, id + QStringLiteral("/error-kind"));
      if (reject.contains(QStringLiteral("line")))
        require(error.line == reject.value(QStringLiteral("line")).toInt(),
                id + QStringLiteral("/error-line"));
    }
  }
  std::puts("MermaidGanttParserTest passed");
  return 0;
}
