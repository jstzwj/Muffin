// Timeline parser/DB oracle frozen from Mermaid 11.16.0's source entry point.
#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/timeline/TimelineDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

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

void compareTasks(const QVector<timeline::TimelineTask>& actual,
                  const QJsonArray& expected, const QString& id) {
  require(actual.size() == expected.size(),
          id + QStringLiteral(": task count %1 != %2")
                   .arg(actual.size())
                   .arg(expected.size()));
  for (qsizetype index = 0; index < actual.size(); ++index) {
    const timeline::TimelineTask& task = actual.at(index);
    const QJsonObject oracle = expected.at(index).toObject();
    const QString path = id + QStringLiteral("/task%1").arg(index);
    require(task.id == oracle.value(QStringLiteral("id")).toInt(),
            path + QStringLiteral(": id mismatch"));
    require(task.section == oracle.value(QStringLiteral("section")).toString(),
            path + QStringLiteral(": section mismatch"));
    require(task.type == oracle.value(QStringLiteral("type")).toString(),
            path + QStringLiteral(": type mismatch"));
    require(task.task == oracle.value(QStringLiteral("task")).toString(),
            path + QStringLiteral(": text mismatch"));
    require(task.score == oracle.value(QStringLiteral("score")).toDouble(),
            path + QStringLiteral(": score mismatch"));
    const QJsonArray events = oracle.value(QStringLiteral("events")).toArray();
    require(task.events.size() == events.size(),
            path + QStringLiteral(": event count mismatch"));
    for (qsizetype event = 0; event < task.events.size(); ++event) {
      require(task.events.at(event) == events.at(event).toString(),
              path + QStringLiteral("/event%1 mismatch").arg(event));
    }
  }
}

void compareData(const timeline::TimelineData& actual,
                 const QJsonObject& expected, const QString& id) {
  const timeline::TimelineDirection direction =
      expected.value(QStringLiteral("direction")).toString() ==
              QStringLiteral("TD")
          ? timeline::TimelineDirection::TopDown
          : timeline::TimelineDirection::LeftToRight;
  require(actual.direction == direction, id + QStringLiteral(": direction mismatch"));
  require(actual.title == expected.value(QStringLiteral("title")).toString(),
          id + QStringLiteral(": title mismatch"));
  require(actual.accTitle ==
              expected.value(QStringLiteral("accTitle")).toString(),
          id + QStringLiteral(": accTitle mismatch"));
  require(actual.accDescr ==
              expected.value(QStringLiteral("accDescr")).toString(),
          id + QStringLiteral(": accDescr mismatch"));

  const QJsonArray sections = expected.value(QStringLiteral("sections")).toArray();
  require(actual.sections.size() == sections.size(),
          id + QStringLiteral(": section count mismatch"));
  for (qsizetype index = 0; index < actual.sections.size(); ++index) {
    require(actual.sections.at(index) == sections.at(index).toString(),
            id + QStringLiteral("/section%1 mismatch").arg(index));
  }
  compareTasks(actual.tasks, expected.value(QStringLiteral("tasks")).toArray(), id);
}

void compareExplicitTimelineConfig(const MermaidPreprocessResult& pre,
                                   const QJsonObject& fixture,
                                   const QString& id) {
  if (!fixture.contains(QStringLiteral("effectiveTimelineConfig"))) return;
  const QJsonObject sourceConfig =
      pre.config.value(QStringLiteral("timeline")).toObject();
  const QJsonObject upstream =
      fixture.value(QStringLiteral("effectiveTimelineConfig")).toObject();
  for (auto it = sourceConfig.begin(); it != sourceConfig.end(); ++it) {
    if (it.value().isNull() || !upstream.contains(it.key())) continue;
    require(it.value() == upstream.value(it.key()),
            id + QStringLiteral(": source-entry config %1 mismatch").arg(it.key()));
  }
  if (id == QStringLiteral("config-top-level-ignored")) {
    require(sourceConfig.isEmpty(),
            id + QStringLiteral(": top-level config leaked into timeline"));
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2,
          QStringLiteral("Expected timeline grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError,
          QStringLiteral("Timeline grammar JSON: ") + error.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version"))
                  .toString() == QStringLiteral("11.16.0"),
          QStringLiteral("Timeline fixture Mermaid version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QStringLiteral(
                  "4f434c4d40dfdaac2d1320e3e9b53fcb4bdaba7bbf09751687f43f0a312f4bdc"),
          QStringLiteral("Timeline fixture changed; audit its provenance"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 82,
          QStringLiteral("Timeline fixture case count drifted"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();
    const QString rejectClass = reject.value(QStringLiteral("class")).toString();
    const MermaidPreprocessResult pre = preprocessDiagram(source);

    bool detected = false;
    try {
      detected = detectDiagramType(source, pre.config) == QStringLiteral("timeline");
    } catch (const UnknownDiagramError&) {
      detected = false;
    }
    const bool expectedDetector = rejectClass != QStringLiteral("no-diagram");
    require(detected == expectedDetector, id + QStringLiteral(": detector mismatch"));
    if (!detected) continue;

    try {
      const timeline::TimelineData data = timeline::TimelineDiagram::parse(pre.code);
      require(accept,
              id + QStringLiteral(": native accepted upstream rejection"));
      compareData(data, fixture.value(QStringLiteral("expectedDb")).toObject(), id);
      compareExplicitTimelineConfig(pre, fixture, id);
      if (id == QStringLiteral("frontmatter-title-db-inert")) {
        require(pre.hasTitle && pre.title == QStringLiteral("Front") &&
                    data.title.isEmpty(),
                id + QStringLiteral(": frontmatter title must stay outside DB"));
      }
    } catch (const timeline::TimelineParseError& native) {
      require(!accept,
              id + QStringLiteral(": native rejected upstream acceptance at %1:%2: %3")
                       .arg(native.line)
                       .arg(native.column)
                       .arg(QString::fromUtf8(native.what())));
      const timeline::TimelineErrorKind expectedKind =
          rejectClass == QStringLiteral("runtime")
              ? timeline::TimelineErrorKind::Runtime
              : timeline::TimelineErrorKind::Parser;
      require(native.kind == expectedKind,
              id + QStringLiteral(": error phase mismatch"));
      if (reject.contains(QStringLiteral("line"))) {
        require(native.line == reject.value(QStringLiteral("line")).toInt(),
                id + QStringLiteral(": error line mismatch (%1 vs %2)")
                         .arg(native.line)
                         .arg(reject.value(QStringLiteral("line")).toInt()));
      }
      if (reject.contains(QStringLiteral("column"))) {
        require(native.column == reject.value(QStringLiteral("column")).toInt(),
                id + QStringLiteral(": error column mismatch (%1 vs %2)")
                         .arg(native.column)
                         .arg(reject.value(QStringLiteral("column")).toInt()));
      }
    }
  }

  std::puts("MermaidTimelineParserTest passed");
  return 0;
}
