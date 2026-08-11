#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/block/BlockDiagram.h"

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
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QString kind(block::BlockErrorKind value) {
  if (value == block::BlockErrorKind::Lexer) return QStringLiteral("Lexer");
  if (value == block::BlockErrorKind::Runtime) return QStringLiteral("Runtime");
  return QStringLiteral("Parser");
}

void compareNode(const block::BlockNode& actual, const QJsonObject& oracle,
                 const QString& path) {
  require(actual.id == oracle.value(QStringLiteral("id")).toString(), path + "/id");
  require(actual.type == oracle.value(QStringLiteral("type")).toString(),
          path + "/type: " + actual.type + " != " + oracle.value(QStringLiteral("type")).toString());
  const bool label = !oracle.value(QStringLiteral("label")).isNull();
  require(actual.hasLabel == label, path + "/label-presence");
  require(!label || actual.label == oracle.value(QStringLiteral("label")).toString(), path + "/label");
  const bool width = !oracle.value(QStringLiteral("width")).isNull();
  require(actual.hasWidth == width, path + "/width-presence");
  require(!width || actual.width == oracle.value(QStringLiteral("width")).toInt(), path + "/width");
  const bool widthInColumns = !oracle.value(QStringLiteral("widthInColumns")).isNull();
  require(actual.hasWidthInColumns == widthInColumns, path + "/widthInColumns-presence");
  require(!widthInColumns || actual.widthInColumns == oracle.value(QStringLiteral("widthInColumns")).toInt(), path + "/widthInColumns");
  const bool columns = !oracle.value(QStringLiteral("columns")).isNull();
  require(actual.hasColumns == columns, path + "/columns-presence");
  require(!columns || actual.columns == oracle.value(QStringLiteral("columns")).toInt(), path + "/columns");
  auto compareStrings = [&](const QStringList& values, QLatin1String key) {
    const QJsonArray expected = oracle.value(key).toArray();
    require(values.size() == expected.size(), path + "/" + key + "-count");
    for (qsizetype i = 0; i < values.size(); ++i)
      require(values.at(i) == expected.at(i).toString(), path + "/" + key);
  };
  compareStrings(actual.directions, QLatin1String("directions"));
  compareStrings(actual.classes, QLatin1String("classes"));
  compareStrings(actual.styles, QLatin1String("styles"));
  const QJsonArray children = oracle.value(QStringLiteral("children")).toArray();
  require(actual.children.size() == children.size(), path + "/children-count");
  for (qsizetype i = 0; i < children.size(); ++i)
    compareNode(actual.children.at(i), children.at(i).toObject(),
                path + QStringLiteral("/child-%1").arg(i));
}

void compareData(const block::BlockData& actual, const QJsonObject& oracle,
                 const QString& id) {
  const QJsonArray blocks = oracle.value(QStringLiteral("blocks")).toArray();
  require(actual.blocks.size() == blocks.size(), id + "/blocks-count");
  for (qsizetype i = 0; i < blocks.size(); ++i)
    compareNode(actual.blocks.at(i), blocks.at(i).toObject(), id + QStringLiteral("/block-%1").arg(i));
  const QJsonArray flat = oracle.value(QStringLiteral("flat")).toArray();
  require(actual.flat.size() == flat.size(), id + "/flat-count");
  for (qsizetype i = 0; i < flat.size(); ++i)
    compareNode(actual.flat.at(i), flat.at(i).toObject(), id + QStringLiteral("/flat-%1").arg(i));
  const int publicColumns = actual.root.columns != 0
      ? actual.root.columns : actual.root.children.size();
  require(publicColumns == oracle.value(QStringLiteral("columns")).toInt(), id + "/columns");
  const QJsonArray edges = oracle.value(QStringLiteral("edges")).toArray();
  require(actual.edges.size() == edges.size(), id + "/edges-count");
  for (qsizetype i = 0; i < edges.size(); ++i) {
    const auto& edge = actual.edges.at(i); const QJsonObject expected = edges.at(i).toObject();
    require(edge.id == expected.value(QStringLiteral("id")).toString(), id + "/edge-id");
    require(edge.start == expected.value(QStringLiteral("start")).toString(), id + "/edge-start");
    require(edge.end == expected.value(QStringLiteral("end")).toString(), id + "/edge-end");
    require(edge.label == expected.value(QStringLiteral("label")).toString(), id + "/edge-label");
    require(edge.thickness == expected.value(QStringLiteral("thickness")).toString(), id + "/edge-thickness");
    require(edge.pattern == expected.value(QStringLiteral("pattern")).toString(), id + "/edge-pattern");
    require(edge.arrowTypeStart == expected.value(QStringLiteral("arrowTypeStart")).toString(), id + "/edge-start-marker");
    require(edge.arrowTypeEnd == expected.value(QStringLiteral("arrowTypeEnd")).toString(), id + "/edge-end-marker");
  }
  require(actual.classes.size() == oracle.value(QStringLiteral("classes")).toArray().size(), id + "/classes-count");
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Block grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("bcc8028aebf5c1fdf6779211f6413581fc07fcd0a99e515dd928b058e92217a8"),
          QStringLiteral("Block grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("blockModuleSha256")).toString() ==
                  QLatin1String("2e993bfcf368ff51b44be0b4eea45d293fde010f1d5e79e3ed556fa648a77ba3") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("3bffe027123c3dece50b75cccb07eaa695b17d5d08782a2d1f5636d70ea099d3"),
          QStringLiteral("Block grammar provenance changed"));
  int accepted = 0, rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& item : cases) {
    const QJsonObject fixture = item.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const MermaidPreprocessResult pre = preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    bool detected = false;
    try { detected = detectDiagramType(pre.code, pre.config) == QLatin1String("block"); }
    catch (const UnknownDiagramError&) {}
    if (!expected.value(QStringLiteral("parse")).toBool()) {
      ++rejected;
      if (!detected) continue;
      bool threw = false;
      try { (void)block::BlockDiagram::parse(pre.code); }
      catch (const block::BlockParseError& error) {
        threw = true;
        require(kind(error.kind) == expected.value(QStringLiteral("error")).toObject().value(QStringLiteral("kind")).toString(), id + "/kind");
      }
      require(threw, id + ": rejected source parsed");
      continue;
    }
    ++accepted;
    require(detected, id + ": accepted source not detected");
    compareData(block::BlockDiagram::parse(pre.code), expected.value(QStringLiteral("db")).toObject(), id);
  }
  require(cases.size() == 60 && accepted == 47 && rejected == 13,
          QStringLiteral("Block grammar table not fully visited"));
  std::puts("MermaidBlockParserTest: 60/60 passed");
  return 0;
}
