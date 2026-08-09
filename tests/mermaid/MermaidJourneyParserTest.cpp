// Journey parser/DB oracle. Iterates every case generated from Mermaid 11.16.0
// in tests/fixtures/mermaid/journey-grammar.json. Source directives are
// preprocessed before parsing, while detection is checked against the original
// source so Mermaid's case-sensitive /^\s*journey/ boundary remains observable.
#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/journey/JourneyDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
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

void compareScore(double actual, const QJsonObject& expected,
                  const QString& path) {
  const QString kind = expected.value(QStringLiteral("kind")).toString();
  if (kind == QStringLiteral("nan")) {
    require(std::isnan(actual), path + QStringLiteral(": expected NaN"));
    return;
  }
  if (kind == QStringLiteral("infinity")) {
    const int sign = expected.value(QStringLiteral("sign")).toInt();
    require(std::isinf(actual), path + QStringLiteral(": expected infinity"));
    require((std::signbit(actual) ? -1 : 1) == sign,
            path + QStringLiteral(": infinity sign mismatch"));
    return;
  }
  require(kind == QStringLiteral("finite"),
          path + QStringLiteral(": unknown score kind ") + kind);
  const double value = expected.value(QStringLiteral("value")).toDouble();
  require(std::isfinite(actual) && std::fabs(actual - value) <= 1e-12,
          path + QStringLiteral(": finite value %1 != %2")
                     .arg(actual, 0, 'g', 17)
                     .arg(value, 0, 'g', 17));
  if (expected.value(QStringLiteral("negativeZero")).toBool()) {
    require(actual == 0.0 && std::signbit(actual),
            path + QStringLiteral(": expected negative zero"));
  } else if (value == 0.0) {
    require(!std::signbit(actual),
            path + QStringLiteral(": unexpected negative zero"));
  }
}

QStringList actorsFromTasks(const QVector<journey::JourneyTask>& tasks) {
  QStringList actors;
  for (const journey::JourneyTask& task : tasks) {
    for (const QString& person : task.people) {
      if (!actors.contains(person)) actors.append(person);
    }
  }
  std::sort(actors.begin(), actors.end(), [](const QString& a, const QString& b) {
    return QString::compare(a, b, Qt::CaseSensitive) < 0;
  });
  return actors;
}

QString effectiveTextMode(const QJsonValue& value) {
  if (value.isString() && value.toString() == QStringLiteral("fo"))
    return QStringLiteral("fo");
  if (value.isString() && value.toString() == QStringLiteral("old"))
    return QStringLiteral("old");
  return QStringLiteral("tspan");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected journey grammar fixture path"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError jsonError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Journey grammar JSON: ") + jsonError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QStringLiteral("11.16.0"),
          QStringLiteral("Journey grammar: Mermaid version drifted"));
  require(root.value(QStringLiteral("oracle")).toString().contains(
              QStringLiteral("journey jison")),
          QStringLiteral("Journey grammar: oracle contract drifted"));

  int visited = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    ++visited;
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);

    bool detectedJourney = false;
    try {
      detectedJourney =
          detectDiagramType(source, pre.config) == QStringLiteral("journey");
    } catch (const UnknownDiagramError&) {
      detectedJourney = false;
    }

    const QString rejectClass = fixture.value(QStringLiteral("reject"))
                                    .toObject()
                                    .value(QStringLiteral("class"))
                                    .toString();
    if (!detectedJourney) {
      require(!accept && rejectClass == QStringLiteral("no-diagram"),
              id + QStringLiteral(": native detector rejected a non-no-diagram case"));
      continue;
    }

    if (!accept) {
      bool threw = false;
      try {
        (void)journey::JourneyDiagram::parse(pre.code);
      } catch (const journey::JourneyParseError&) {
        threw = true;
      }
      require(threw,
              id + QStringLiteral(": upstream-rejected source parsed successfully"));
      continue;
    }

    journey::JourneyData actual;
    try {
      actual = journey::JourneyDiagram::parse(pre.code);
    } catch (const journey::JourneyParseError& error) {
      fail(id + QStringLiteral(": accepted source threw at line %1: %2")
                    .arg(error.line)
                    .arg(QString::fromUtf8(error.what())));
    }

    const QJsonObject expected = fixture.value(QStringLiteral("expectedDb")).toObject();
    require(actual.title == expected.value(QStringLiteral("title")).toString(),
            id + QStringLiteral(": title mismatch"));
    require(actual.accTitle == expected.value(QStringLiteral("accTitle")).toString(),
            id + QStringLiteral(": accTitle mismatch"));
    require(actual.accDescr ==
                expected.value(QStringLiteral("accDescription")).toString(),
            id + QStringLiteral(": accDescription mismatch"));

    const QJsonArray expectedSections =
        expected.value(QStringLiteral("sections")).toArray();
    require(actual.sections.size() == expectedSections.size(),
            id + QStringLiteral(": section count %1 != %2")
                     .arg(actual.sections.size())
                     .arg(expectedSections.size()));
    for (qsizetype index = 0; index < actual.sections.size(); ++index) {
      require(actual.sections.at(index) == expectedSections.at(index).toString(),
              id + QStringLiteral("/section%1 mismatch").arg(index));
    }

    const QJsonArray expectedTasks = expected.value(QStringLiteral("tasks")).toArray();
    require(actual.tasks.size() == expectedTasks.size(),
            id + QStringLiteral(": task count %1 != %2")
                     .arg(actual.tasks.size())
                     .arg(expectedTasks.size()));
    for (qsizetype index = 0; index < actual.tasks.size(); ++index) {
      const journey::JourneyTask& task = actual.tasks.at(index);
      const QJsonObject oracle = expectedTasks.at(index).toObject();
      const QString path = id + QStringLiteral("/task%1").arg(index);
      require(task.section == oracle.value(QStringLiteral("section")).toString(),
              path + QStringLiteral(": section mismatch"));
      require(task.type == oracle.value(QStringLiteral("type")).toString(),
              path + QStringLiteral(": type mismatch"));
      require(task.task == oracle.value(QStringLiteral("task")).toString(),
              path + QStringLiteral(": text mismatch"));
      compareScore(task.score, oracle.value(QStringLiteral("score")).toObject(),
                   path + QStringLiteral("/score"));
      const QJsonArray people = oracle.value(QStringLiteral("people")).toArray();
      require(task.people.size() == people.size(),
              path + QStringLiteral(": people count mismatch"));
      for (qsizetype person = 0; person < task.people.size(); ++person) {
        require(task.people.at(person) == people.at(person).toString(),
                path + QStringLiteral("/person%1 mismatch").arg(person));
      }
    }

    const QStringList actualActors = actorsFromTasks(actual.tasks);
    const QJsonArray expectedActors = expected.value(QStringLiteral("actors")).toArray();
    require(actualActors.size() == expectedActors.size(),
            id + QStringLiteral(": actor count mismatch"));
    for (qsizetype index = 0; index < actualActors.size(); ++index) {
      require(actualActors.at(index) == expectedActors.at(index).toString(),
              id + QStringLiteral("/actor%1 '%2' != '%3'")
                       .arg(index)
                       .arg(actualActors.at(index), expectedActors.at(index).toString()));
    }

    if (fixture.contains(QStringLiteral("expectedRender"))) {
      const QString expectedMode = fixture.value(QStringLiteral("expectedRender"))
                                       .toObject()
                                       .value(QStringLiteral("taskMode"))
                                       .toString();
      const QJsonValue configuredMode = pre.config.value(QStringLiteral("journey"))
                                            .toObject()
                                            .value(QStringLiteral("textPlacement"));
      require(effectiveTextMode(configuredMode) == expectedMode,
              id + QStringLiteral(": textPlacement dispatch mismatch"));
    }
  }

  require(visited == root.value(QStringLiteral("cases")).toArray().size() &&
              visited > 0,
          QStringLiteral("Journey grammar fixture was not fully visited"));
  return 0;
}
