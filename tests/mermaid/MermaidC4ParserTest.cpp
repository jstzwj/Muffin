#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/c4/C4Diagram.h"

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

QString textField(const QJsonObject& object, const QString& key) {
  return object.value(key).toObject().value(QStringLiteral("text")).toString();
}

void compareOptional(const std::optional<QString>& actual,
                     const QJsonObject& expected, const QString& key,
                     const QString& path) {
  const QJsonValue value = expected.value(key);
  const bool present = !value.isUndefined() && !value.isNull();
  require(actual.has_value() == present, path + u'/' + key + QStringLiteral(" presence"));
  if (present) require(*actual == value.toString(), path + u'/' + key);
}

void compareElement(const c4::C4Element& actual, const QJsonObject& expected,
                    const QString& path) {
  require(actual.alias == expected.value(QStringLiteral("alias")).toString(), path + QStringLiteral("/alias"));
  require(actual.label == textField(expected, QStringLiteral("label")), path + QStringLiteral("/label"));
  require(actual.description == textField(expected, QStringLiteral("descr")), path + QStringLiteral("/descr"));
  require(actual.technology == textField(expected, QStringLiteral("techn")), path + QStringLiteral("/techn"));
  require(actual.type == textField(expected, QStringLiteral("type")), path + QStringLiteral("/type"));
  require(actual.typeC4Shape == textField(expected, QStringLiteral("typeC4Shape")), path + QStringLiteral("/typeC4Shape"));
  require(actual.parentBoundary == expected.value(QStringLiteral("parentBoundary")).toString(), path + QStringLiteral("/parentBoundary"));
  require(actual.nodeType == expected.value(QStringLiteral("nodeType")).toString(), path + QStringLiteral("/nodeType"));
  compareOptional(actual.sprite, expected, QStringLiteral("sprite"), path);
  compareOptional(actual.tags, expected, QStringLiteral("tags"), path);
  compareOptional(actual.link, expected, QStringLiteral("link"), path);
  compareOptional(actual.backgroundColor, expected, QStringLiteral("bgColor"), path);
  compareOptional(actual.fontColor, expected, QStringLiteral("fontColor"), path);
  compareOptional(actual.borderColor, expected, QStringLiteral("borderColor"), path);
  compareOptional(actual.shadowing, expected, QStringLiteral("shadowing"), path);
  compareOptional(actual.shape, expected, QStringLiteral("shape"), path);
  compareOptional(actual.legendText, expected, QStringLiteral("legendText"), path);
  compareOptional(actual.legendSprite, expected, QStringLiteral("legendSprite"), path);
}

void compareDb(const c4::C4Data& actual, const QJsonObject& expected,
               const QString& id) {
  require(actual.c4Type == expected.value(QStringLiteral("c4Type")).toString(), id + QStringLiteral("/c4Type"));
  require(actual.title == expected.value(QStringLiteral("title")).toString(), id + QStringLiteral("/title"));
  require(actual.accTitle == expected.value(QStringLiteral("accTitle")).toString(), id + QStringLiteral("/accTitle"));
  require(actual.accDescr == expected.value(QStringLiteral("accDescr")).toString(), id + QStringLiteral("/accDescr"));
  require(actual.shapeInRow == expected.value(QStringLiteral("shapeInRow")).toInt(), id + QStringLiteral("/shapeInRow"));
  require(actual.boundaryInRow == expected.value(QStringLiteral("boundaryInRow")).toInt(), id + QStringLiteral("/boundaryInRow"));

  const QJsonArray shapes = expected.value(QStringLiteral("shapes")).toArray();
  require(actual.shapes.size() == shapes.size(), id + QStringLiteral("/shape count"));
  for (qsizetype i = 0; i < actual.shapes.size(); ++i)
    compareElement(actual.shapes.at(i), shapes.at(i).toObject(), id + QStringLiteral("/shapes/%1").arg(i));

  const QJsonArray boundaries = expected.value(QStringLiteral("boundaries")).toArray();
  require(actual.boundaries.size() == boundaries.size(), id + QStringLiteral("/boundary count"));
  for (qsizetype i = 0; i < actual.boundaries.size(); ++i)
    compareElement(actual.boundaries.at(i), boundaries.at(i).toObject(), id + QStringLiteral("/boundaries/%1").arg(i));

  const QJsonArray relations = expected.value(QStringLiteral("relations")).toArray();
  require(actual.relations.size() == relations.size(), id + QStringLiteral("/relation count"));
  for (qsizetype i = 0; i < actual.relations.size(); ++i) {
    const auto& item = actual.relations.at(i);
    const QJsonObject oracle = relations.at(i).toObject();
    const QString path = id + QStringLiteral("/relations/%1").arg(i);
    require(item.type == oracle.value(QStringLiteral("type")).toString(), path + QStringLiteral("/type"));
    require(item.from == oracle.value(QStringLiteral("from")).toString(), path + QStringLiteral("/from"));
    require(item.to == oracle.value(QStringLiteral("to")).toString(), path + QStringLiteral("/to"));
    require(item.label == textField(oracle, QStringLiteral("label")), path + QStringLiteral("/label"));
    require(item.technology == textField(oracle, QStringLiteral("techn")), path + QStringLiteral("/techn"));
    require(item.description == textField(oracle, QStringLiteral("descr")), path + QStringLiteral("/descr"));
    compareOptional(item.sprite, oracle, QStringLiteral("sprite"), path);
    compareOptional(item.tags, oracle, QStringLiteral("tags"), path);
    compareOptional(item.link, oracle, QStringLiteral("link"), path);
    compareOptional(item.textColor, oracle, QStringLiteral("textColor"), path);
    compareOptional(item.lineColor, oracle, QStringLiteral("lineColor"), path);
    const bool hasX = oracle.contains(QStringLiteral("offsetX"));
    const bool hasY = oracle.contains(QStringLiteral("offsetY"));
    require(item.offsetX.has_value() == hasX, path + QStringLiteral("/offsetX presence"));
    require(item.offsetY.has_value() == hasY, path + QStringLiteral("/offsetY presence"));
    if (hasX) require(*item.offsetX == oracle.value(QStringLiteral("offsetX")).toInt(), path + QStringLiteral("/offsetX"));
    if (hasY) require(*item.offsetY == oracle.value(QStringLiteral("offsetY")).toInt(), path + QStringLiteral("/offsetY"));
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected C4 grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("ce03241dacc8347fdd25b666c068722b9ee6d57e9b6ff3609b41c3e9e3ddf2c3"),
          QStringLiteral("C4 grammar fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("a99f31961d48cc1fb4f5b6ebfea09fe3eeb7c64217cdc7c490eaf277d91fa718"),
          QStringLiteral("C4 grammar fixture provenance drifted"));
  require(root.value(QStringLiteral("provenance")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("C4 Mermaid version drifted"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try { detected = detectDiagramType(pre.code, pre.config) == QLatin1String("c4"); }
    catch (const UnknownDiagramError&) {}

    if (!fixture.value(QStringLiteral("accepted")).toBool()) {
      ++rejected;
      if (!detected) continue;
      bool threw = false;
      try { (void)c4::C4Diagram::parse(pre.code); }
      catch (const c4::C4ParseError&) { threw = true; }
      require(threw, id + QStringLiteral(": upstream rejected but native parsed"));
      continue;
    }

    ++accepted;
    require(detected, id + QStringLiteral(": detector rejected accepted source"));
    compareDb(c4::C4Diagram::parse(pre.code),
              fixture.value(QStringLiteral("database")).toObject(), id);
  }
  require(cases.size() == 35 && accepted == 22 && rejected == 13,
          QStringLiteral("C4 grammar coverage drifted"));
  std::puts("MermaidC4ParserTest: 35 cases passed");
  return 0;
}
