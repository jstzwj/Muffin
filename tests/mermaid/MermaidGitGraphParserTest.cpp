#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/gitgraph/GitGraphDiagram.h"

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

QString direction(gitgraph::Direction value) {
  if (value == gitgraph::Direction::TopToBottom) return QStringLiteral("TB");
  if (value == gitgraph::Direction::BottomToTop) return QStringLiteral("BT");
  return QStringLiteral("LR");
}

void compareStrings(const QVector<QString>& actual, const QJsonArray& expected,
                    const QString& path) {
  require(actual.size() == expected.size(), path + QStringLiteral(" count"));
  for (qsizetype i = 0; i < actual.size(); ++i)
    require(actual.at(i) == expected.at(i).toString(),
            path + QStringLiteral("/%1 mismatch").arg(i));
}

void compareDb(const gitgraph::GitGraphData& actual,
               const QJsonObject& expected, const QString& id) {
  require(direction(actual.direction) ==
              expected.value(QStringLiteral("direction")).toString(),
          id + QStringLiteral("/direction"));
  require(actual.currentBranch ==
              expected.value(QStringLiteral("currentBranch")).toString(),
          id + QStringLiteral("/currentBranch"));
  require(actual.title == expected.value(QStringLiteral("title")).toString(),
          id + QStringLiteral("/title"));
  require(actual.accTitle ==
              expected.value(QStringLiteral("accTitle")).toString(),
          id + QStringLiteral("/accTitle"));
  require(actual.accDescr ==
              expected.value(QStringLiteral("accDescr")).toString(),
          id + QStringLiteral("/accDescr"));

  const QJsonArray commits = expected.value(QStringLiteral("commits")).toArray();
  require(actual.commits.size() == commits.size(),
          id + QStringLiteral("/commit count"));
  for (qsizetype i = 0; i < actual.commits.size(); ++i) {
    const auto& value = actual.commits.at(i);
    const QJsonObject oracle = commits.at(i).toObject();
    const QString path = id + QStringLiteral("/commits/%1").arg(i);
    require(value.id == oracle.value(QStringLiteral("id")).toString(),
            path + QStringLiteral("/id"));
    require(value.message == oracle.value(QStringLiteral("message")).toString(),
            path + QStringLiteral("/message"));
    require(value.seq == oracle.value(QStringLiteral("seq")).toInt(),
            path + QStringLiteral("/seq"));
    require(int(value.type) == oracle.value(QStringLiteral("type")).toInt(),
            path + QStringLiteral("/type"));
    require(value.branch == oracle.value(QStringLiteral("branch")).toString(),
            path + QStringLiteral("/branch"));
    compareStrings(value.tags, oracle.value(QStringLiteral("tags")).toArray(),
                   path + QStringLiteral("/tags"));
    compareStrings(value.parents,
                   oracle.value(QStringLiteral("parents")).toArray(),
                   path + QStringLiteral("/parents"));
    const bool hasCustomType = oracle.contains(QStringLiteral("customType"));
    require(value.customType.has_value() == hasCustomType,
            path + QStringLiteral("/customType presence"));
    if (hasCustomType)
      require(int(*value.customType) ==
                  oracle.value(QStringLiteral("customType")).toInt(),
              path + QStringLiteral("/customType"));
    require(value.customId ==
                oracle.value(QStringLiteral("customId")).toBool(false),
            path + QStringLiteral("/customId"));
  }

  const QJsonArray branches = expected.value(QStringLiteral("branches")).toArray();
  require(actual.branches.size() == branches.size(),
          id + QStringLiteral("/branch count"));
  for (qsizetype i = 0; i < actual.branches.size(); ++i) {
    const auto& value = actual.branches.at(i);
    const QJsonObject oracle = branches.at(i).toObject();
    const QString path = id + QStringLiteral("/branches/%1").arg(i);
    require(value.name == oracle.value(QStringLiteral("name")).toString(),
            path + QStringLiteral("/name"));
    const bool oracleHasHead =
        !oracle.value(QStringLiteral("head")).isNull() &&
        !oracle.value(QStringLiteral("head")).isUndefined();
    require(value.hasHead == oracleHasHead,
            path + QStringLiteral("/head presence"));
    if (value.hasHead)
      require(value.head == oracle.value(QStringLiteral("head")).toString(),
              path + QStringLiteral("/head"));
    require(value.order.has_value() == oracle.contains(QStringLiteral("order")),
            path + QStringLiteral("/order presence"));
    if (value.order)
      require(*value.order == oracle.value(QStringLiteral("order")).toDouble(),
              path + QStringLiteral("/order"));
  }

  const QJsonArray ordered =
      expected.value(QStringLiteral("orderedBranches")).toArray();
  require(actual.orderedBranches.size() == ordered.size(),
          id + QStringLiteral("/orderedBranches count"));
  for (qsizetype i = 0; i < actual.orderedBranches.size(); ++i)
    require(actual.orderedBranches.at(i) ==
                ordered.at(i).toObject().value(QStringLiteral("name")).toString(),
            id + QStringLiteral("/orderedBranches/%1").arg(i));
}

gitgraph::GitGraphErrorKind errorKind(const QString& value) {
  if (value == QLatin1String("Lexer")) return gitgraph::GitGraphErrorKind::Lexer;
  if (value == QLatin1String("Runtime")) return gitgraph::GitGraphErrorKind::Runtime;
  return gitgraph::GitGraphErrorKind::Parser;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected GitGraph grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("f00fb92db7c0f755ad8d1700ee94d715ca050ecb7384bfe7a3d49abbf055e9d5"),
          QStringLiteral("GitGraph grammar fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("72fd6b2be6d989aa9bf906e368c357bfa47871e783308f2c1011c816c12b3676"),
          QStringLiteral("GitGraph grammar semantic fixture drifted"));
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("GitGraph Mermaid version drifted"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) ==
                 QLatin1String("gitGraph");
    } catch (const UnknownDiagramError&) {
    }
    if (!expected.value(QStringLiteral("parse")).toBool()) {
      ++rejected;
      if (!detected) continue;
      bool threw = false;
      try {
        (void)gitgraph::GitGraphDiagram::parse(pre.code);
      } catch (const gitgraph::GitGraphParseError& error) {
        threw = true;
        const QJsonObject oracle = expected.value(QStringLiteral("error")).toObject();
        require(error.kind == errorKind(oracle.value(QStringLiteral("kind")).toString()),
                id + QStringLiteral("/error kind"));
        require(error.line == oracle.value(QStringLiteral("line")).toInt() &&
                    error.column == oracle.value(QStringLiteral("column")).toInt(),
                id + QStringLiteral("/error location %1:%2 != %3:%4")
                         .arg(error.line).arg(error.column)
                         .arg(oracle.value(QStringLiteral("line")).toInt())
                         .arg(oracle.value(QStringLiteral("column")).toInt()));
      }
      require(threw, id + QStringLiteral(": upstream rejected but native parsed"));
      continue;
    }
    ++accepted;
    require(detected, id + QStringLiteral(": detector rejected accepted input"));
    compareDb(gitgraph::GitGraphDiagram::parse(pre.code),
              expected.value(QStringLiteral("db")).toObject(), id);
  }
  require(cases.size() == 32 && accepted == 18 && rejected == 14,
          QStringLiteral("GitGraph grammar table coverage drifted"));
  std::puts("MermaidGitGraphParserTest: 32 cases passed");
  return 0;
}
