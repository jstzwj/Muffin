#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/architecture/ArchitectureDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

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

QString kind(architecture::ArchitectureErrorKind value) {
  switch (value) {
    case architecture::ArchitectureErrorKind::Lexer: return QStringLiteral("Lexer");
    case architecture::ArchitectureErrorKind::Parser: return QStringLiteral("Parser");
    case architecture::ArchitectureErrorKind::Runtime: return QStringLiteral("Runtime");
  }
  return {};
}

bool present(const QJsonObject& object, QLatin1String key) {
  return object.contains(key) && !object.value(key).isNull();
}

void compareData(const architecture::ArchitectureData& data,
                 const QJsonObject& db, const QString& id) {
  require(data.title == db.value(QStringLiteral("title")).toString(), id + "/title");
  require(data.accTitle == db.value(QStringLiteral("accTitle")).toString(), id + "/accTitle");
  require(data.accDescr == db.value(QStringLiteral("accDescr")).toString(), id + "/accDescr");

  const QJsonArray groups = db.value(QStringLiteral("groups")).toArray();
  require(data.groups.size() == groups.size(), id + "/group-count");
  for (qsizetype i = 0; i < groups.size(); ++i) {
    const auto& actual = data.groups.at(i);
    const QJsonObject oracle = groups.at(i).toObject();
    const QString path = id + QStringLiteral("/group/%1/").arg(i);
    require(actual.id == oracle.value(QStringLiteral("id")).toString(), path + "id");
    require(actual.hasIcon == present(oracle, QLatin1String("icon")), path + "icon/presence");
    require(!actual.hasIcon || actual.icon == oracle.value(QStringLiteral("icon")).toString(), path + "icon");
    require(actual.hasTitle == present(oracle, QLatin1String("title")), path + "title/presence");
    require(!actual.hasTitle || actual.title == oracle.value(QStringLiteral("title")).toString(), path + "title");
    require(actual.hasParent == present(oracle, QLatin1String("in")), path + "parent/presence");
    require(!actual.hasParent || actual.parent == oracle.value(QStringLiteral("in")).toString(), path + "parent");
  }

  const QJsonArray services = db.value(QStringLiteral("services")).toArray();
  require(data.services.size() == services.size(), id + "/service-count");
  for (qsizetype i = 0; i < services.size(); ++i) {
    const auto& actual = data.services.at(i);
    const QJsonObject oracle = services.at(i).toObject();
    const QString path = id + QStringLiteral("/service/%1/").arg(i);
    require(actual.id == oracle.value(QStringLiteral("id")).toString(), path + "id");
    require(actual.hasIcon == present(oracle, QLatin1String("icon")), path + "icon/presence");
    require(!actual.hasIcon || actual.icon == oracle.value(QStringLiteral("icon")).toString(), path + "icon");
    require(actual.hasIconText == present(oracle, QLatin1String("iconText")), path + "iconText/presence");
    require(!actual.hasIconText || actual.iconText == oracle.value(QStringLiteral("iconText")).toString(), path + "iconText");
    require(actual.hasTitle == present(oracle, QLatin1String("title")), path + "title/presence");
    require(!actual.hasTitle || actual.title == oracle.value(QStringLiteral("title")).toString(), path + "title");
    require(actual.hasParent == present(oracle, QLatin1String("in")), path + "parent/presence");
    require(!actual.hasParent || actual.parent == oracle.value(QStringLiteral("in")).toString(), path + "parent");
  }

  const QJsonArray junctions = db.value(QStringLiteral("junctions")).toArray();
  require(data.junctions.size() == junctions.size(), id + "/junction-count");
  for (qsizetype i = 0; i < junctions.size(); ++i) {
    const auto& actual = data.junctions.at(i);
    const QJsonObject oracle = junctions.at(i).toObject();
    const QString path = id + QStringLiteral("/junction/%1/").arg(i);
    require(actual.id == oracle.value(QStringLiteral("id")).toString(), path + "id");
    require(actual.hasParent == present(oracle, QLatin1String("in")), path + "parent/presence");
    require(!actual.hasParent || actual.parent == oracle.value(QStringLiteral("in")).toString(), path + "parent");
  }

  const QJsonArray edges = db.value(QStringLiteral("edges")).toArray();
  require(data.edges.size() == edges.size(), id + "/edge-count");
  for (qsizetype i = 0; i < edges.size(); ++i) {
    const auto& actual = data.edges.at(i);
    const QJsonObject oracle = edges.at(i).toObject();
    const QString path = id + QStringLiteral("/edge/%1/").arg(i);
    require(actual.lhsId == oracle.value(QStringLiteral("lhsId")).toString(), path + "lhsId");
    require(actual.rhsId == oracle.value(QStringLiteral("rhsId")).toString(), path + "rhsId");
    require(actual.lhsDir == oracle.value(QStringLiteral("lhsDir")).toString().at(0), path + "lhsDir");
    require(actual.rhsDir == oracle.value(QStringLiteral("rhsDir")).toString().at(0), path + "rhsDir");
    require(actual.lhsInto == oracle.value(QStringLiteral("lhsInto")).toBool(), path + "lhsInto");
    require(actual.rhsInto == oracle.value(QStringLiteral("rhsInto")).toBool(), path + "rhsInto");
    require(actual.lhsGroup == oracle.value(QStringLiteral("lhsGroup")).toBool(), path + "lhsGroup");
    require(actual.rhsGroup == oracle.value(QStringLiteral("rhsGroup")).toBool(), path + "rhsGroup");
    require(actual.hasTitle == present(oracle, QLatin1String("title")), path + "title/presence");
    require(!actual.hasTitle || actual.title == oracle.value(QStringLiteral("title")).toString(), path + "title");
  }

  const QJsonArray alignments = db.value(QStringLiteral("alignments")).toArray();
  require(data.alignments.size() == alignments.size(), id + "/alignment-count");
  for (qsizetype i = 0; i < alignments.size(); ++i) {
    const auto& actual = data.alignments.at(i);
    const QJsonObject oracle = alignments.at(i).toObject();
    const QString direction = actual.direction == architecture::ArchitectureAlignment::Direction::Row
        ? QStringLiteral("row") : QStringLiteral("column");
    require(direction == oracle.value(QStringLiteral("direction")).toString(), id + "/alignment/direction");
    const QJsonArray members = oracle.value(QStringLiteral("members")).toArray();
    require(actual.members.size() == members.size(), id + "/alignment/member-count");
    for (qsizetype j = 0; j < members.size(); ++j)
      require(actual.members.at(j) == members.at(j).toString(), id + "/alignment/member");
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Architecture grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("81aaf68e5c77332a6b7a8154ae860336654e23cc7a2fdf66700ca4ffc8427864"),
          QStringLiteral("Architecture grammar fixture bytes changed"));
  QJsonParseError jsonError;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError, jsonError.errorString());
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("architectureModuleSha256")).toString() ==
                  QLatin1String("d6f8424fba961c50f2cfcbd4e1c5f53f37311d83cc768bcf41afd8874c0454ba") &&
              upstream.value(QStringLiteral("parserModuleSha256")).toString() ==
                  QLatin1String("08628d5e6194206bf5f1d5afb9c456db355492cd4245d13b55303fa3d2267387") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("3f542aecad06f85e6d3e8736e4c31dfee89dad23401e88f4d34e4c3e37e264a1"),
          QStringLiteral("Architecture grammar provenance changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const bool accept = expected.value(QStringLiteral("parse")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("architecture");
    } catch (const UnknownDiagramError&) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source was not detected"));
      ++rejected;
      continue;
    }
    if (!accept) {
      ++rejected;
      bool threw = false;
      try {
        (void)architecture::ArchitectureDiagram::parse(pre.code);
      } catch (const architecture::ArchitectureParseError& error) {
        threw = true;
        const QJsonObject oracle = expected.value(QStringLiteral("error")).toObject();
        require(kind(error.kind) == oracle.value(QStringLiteral("kind")).toString(),
                id + QStringLiteral("/kind: %1 != %2")
                         .arg(kind(error.kind), oracle.value(QStringLiteral("kind")).toString()));
        if (error.kind != architecture::ArchitectureErrorKind::Runtime) {
          require(error.line == oracle.value(QStringLiteral("line")).toInt(),
                  id + QStringLiteral("/line: %1 != %2").arg(error.line).arg(oracle.value(QStringLiteral("line")).toInt()));
          require(error.column == oracle.value(QStringLiteral("column")).toInt(),
                  id + QStringLiteral("/column: %1 != %2").arg(error.column).arg(oracle.value(QStringLiteral("column")).toInt()));
        }
      }
      require(threw, id + QStringLiteral(": rejected source parsed"));
      continue;
    }
    ++accepted;
    compareData(architecture::ArchitectureDiagram::parse(pre.code),
                expected.value(QStringLiteral("db")).toObject(), id);
  }
  require(cases.size() == 58 && accepted == 37 && rejected == 21,
          QStringLiteral("Architecture grammar table not fully visited"));
  std::puts("MermaidArchitectureParserTest: 58/58 passed");
  return 0;
}
