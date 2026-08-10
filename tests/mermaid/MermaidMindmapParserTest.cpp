// Mindmap Jison/parser-DB oracle captured live from Mermaid 11.16.0.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/mindmap/MindmapDiagram.h"

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

bool sameScalar(const QJsonValue& actual, const QJsonValue& expected) {
  if (expected.isObject() && expected.toObject().contains(QStringLiteral("$number"))) {
    if (!actual.isDouble()) return false;
    const QString kind = expected.toObject().value(QStringLiteral("$number")).toString();
    const double value = actual.toDouble();
    if (kind == QLatin1String("NaN")) return std::isnan(value);
    if (kind == QLatin1String("Infinity")) return std::isinf(value) && value > 0;
    if (kind == QLatin1String("-Infinity")) return std::isinf(value) && value < 0;
    return kind == QLatin1String("-0") && value == 0.0 && std::signbit(value);
  }
  if (actual.isUndefined() && expected.isUndefined()) return true;
  if (actual.type() != expected.type()) return false;
  if (actual.isDouble()) return actual.toDouble() == expected.toDouble();
  return actual == expected;
}

QJsonValue rawNodeField(const mindmap::MindmapNode& node, const QString& key) {
  if (key == QLatin1String("id")) return node.id;
  if (key == QLatin1String("nodeId")) return node.nodeId;
  if (key == QLatin1String("level")) return node.level;
  if (key == QLatin1String("descr")) return node.descr;
  if (key == QLatin1String("type")) return int(node.type);
  if (key == QLatin1String("width")) return node.width;
  if (key == QLatin1String("padding")) return node.padding;
  if (key == QLatin1String("isRoot")) return node.isRoot;
  if (key == QLatin1String("icon"))
    return node.icon.isNull() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(node.icon);
  if (key == QLatin1String("class"))
    return node.cssClass.isNull() ? QJsonValue(QJsonValue::Undefined)
                                  : QJsonValue(node.cssClass);
  if (key == QLatin1String("section"))
    return node.hasSection ? QJsonValue(node.section)
                           : QJsonValue(QJsonValue::Undefined);
  fail(QStringLiteral("unknown raw node field: ") + key);
}

void compareRawNode(const mindmap::MindmapData& data, int nodeId,
                    const QJsonObject& expected, const QString& path) {
  require(nodeId >= 0 && nodeId < data.nodes.size(), path + QStringLiteral(" invalid node id"));
  const mindmap::MindmapNode& actual = data.nodes.at(nodeId);
  static const QStringList fields = {
      QStringLiteral("id"),       QStringLiteral("nodeId"),
      QStringLiteral("level"),    QStringLiteral("descr"),
      QStringLiteral("type"),     QStringLiteral("width"),
      QStringLiteral("padding"),  QStringLiteral("isRoot"),
      QStringLiteral("icon"),     QStringLiteral("class"),
      QStringLiteral("section")};
  for (const QString& field : fields) {
    const QJsonValue wanted = expected.contains(field)
                                  ? expected.value(field)
                                  : QJsonValue(QJsonValue::Undefined);
    require(sameScalar(rawNodeField(actual, field), wanted),
            path + QLatin1Char('/') + field + QStringLiteral(" mismatch"));
  }
  const QJsonArray anchors = expected.value(QStringLiteral("anchors")).toArray();
  require(actual.anchors.size() == anchors.size(),
          path + QStringLiteral(" anchors count mismatch"));
  for (qsizetype i = 0; i < anchors.size(); ++i) {
    const QJsonObject wanted = anchors.at(i).toObject();
    require(actual.anchors.at(i).href == wanted.value(QStringLiteral("href")).toString(),
            path + QStringLiteral("/anchors/%1/href mismatch").arg(i));
    require(actual.anchors.at(i).label == wanted.value(QStringLiteral("label")).toString(),
            path + QStringLiteral("/anchors/%1/label mismatch").arg(i));
    require(actual.anchors.at(i).start == wanted.value(QStringLiteral("start")).toInteger(),
            path + QStringLiteral("/anchors/%1/start mismatch").arg(i));
    require(actual.anchors.at(i).length == wanted.value(QStringLiteral("length")).toInteger(),
            path + QStringLiteral("/anchors/%1/length mismatch").arg(i));
  }
  const QJsonArray children = expected.value(QStringLiteral("children")).toArray();
  require(actual.children.size() == children.size(),
          path + QStringLiteral(" children count mismatch"));
  for (qsizetype i = 0; i < children.size(); ++i)
    compareRawNode(data, actual.children.at(i), children.at(i).toObject(),
                   path + QStringLiteral("/children/%1").arg(i));
}

QJsonValue flatNodeField(const mindmap::MindmapNode& node, const QString& key) {
  if (key == QLatin1String("id")) return QString::number(node.id);
  if (key == QLatin1String("domId")) return QStringLiteral("node_%1").arg(node.id);
  if (key == QLatin1String("label")) return node.descr;
  if (key == QLatin1String("labelType")) return QStringLiteral("markdown");
  if (key == QLatin1String("isGroup")) return false;
  if (key == QLatin1String("shape")) return node.shape;
  if (key == QLatin1String("width")) return node.width;
  if (key == QLatin1String("height")) return 0.0;
  if (key == QLatin1String("padding")) return node.padding;
  if (key == QLatin1String("cssClasses")) return node.cssClasses;
  if (key == QLatin1String("cssStyles")) return QJsonArray();
  if (key == QLatin1String("look")) return node.look;
  if (key == QLatin1String("icon"))
    return node.icon.isNull() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(node.icon);
  if (key == QLatin1String("level")) return node.level;
  if (key == QLatin1String("nodeId")) return node.nodeId;
  if (key == QLatin1String("type")) return int(node.type);
  if (key == QLatin1String("section"))
    return node.hasSection ? QJsonValue(node.section)
                           : QJsonValue(QJsonValue::Undefined);
  fail(QStringLiteral("unknown flat node field: ") + key);
}

void compareFlatNodes(const mindmap::MindmapData& data, const QJsonArray& expected,
                      const QString& path) {
  require(data.nodes.size() == expected.size(), path + QStringLiteral(" count mismatch"));
  static const QStringList fields = {
      QStringLiteral("id"),         QStringLiteral("domId"),
      QStringLiteral("label"),      QStringLiteral("labelType"),
      QStringLiteral("isGroup"),    QStringLiteral("shape"),
      QStringLiteral("width"),      QStringLiteral("height"),
      QStringLiteral("padding"),    QStringLiteral("cssClasses"),
      QStringLiteral("cssStyles"),  QStringLiteral("look"),
      QStringLiteral("icon"),       QStringLiteral("level"),
      QStringLiteral("nodeId"),     QStringLiteral("type"),
      QStringLiteral("section")};
  for (qsizetype i = 0; i < data.nodes.size(); ++i) {
    const QJsonObject wanted = expected.at(i).toObject();
    for (const QString& field : fields) {
      const QJsonValue expectedValue = wanted.contains(field)
                                           ? wanted.value(field)
                                           : QJsonValue(QJsonValue::Undefined);
      require(sameScalar(flatNodeField(data.nodes.at(i), field), expectedValue),
              path + QStringLiteral("/%1/%2 mismatch").arg(i).arg(field));
    }
  }
}

QJsonValue edgeField(const mindmap::MindmapEdge& edge, const QString& key) {
  if (key == QLatin1String("id")) return edge.id;
  if (key == QLatin1String("start")) return QString::number(edge.start);
  if (key == QLatin1String("end")) return QString::number(edge.end);
  if (key == QLatin1String("type")) return QStringLiteral("normal");
  if (key == QLatin1String("curve")) return QStringLiteral("basis");
  if (key == QLatin1String("thickness")) return QStringLiteral("normal");
  if (key == QLatin1String("look")) return edge.look;
  if (key == QLatin1String("classes")) return edge.classes;
  if (key == QLatin1String("depth")) return edge.depth;
  if (key == QLatin1String("section"))
    return edge.hasSection ? QJsonValue(edge.section)
                           : QJsonValue(QJsonValue::Undefined);
  fail(QStringLiteral("unknown edge field: ") + key);
}

void compareEdges(const mindmap::MindmapData& data, const QJsonArray& expected,
                  const QString& path) {
  require(data.edges.size() == expected.size(), path + QStringLiteral(" count mismatch"));
  static const QStringList fields = {
      QStringLiteral("id"),        QStringLiteral("start"),
      QStringLiteral("end"),       QStringLiteral("type"),
      QStringLiteral("curve"),     QStringLiteral("thickness"),
      QStringLiteral("look"),      QStringLiteral("classes"),
      QStringLiteral("depth"),     QStringLiteral("section")};
  for (qsizetype i = 0; i < data.edges.size(); ++i) {
    const QJsonObject wanted = expected.at(i).toObject();
    for (const QString& field : fields) {
      const QJsonValue expectedValue = wanted.contains(field)
                                           ? wanted.value(field)
                                           : QJsonValue(QJsonValue::Undefined);
      require(sameScalar(edgeField(data.edges.at(i), field), expectedValue),
              path + QStringLiteral("/%1/%2 mismatch").arg(i).arg(field));
    }
  }
}

mindmap::MindmapParseConfig parseConfig(const MermaidPreprocessResult& pre) {
  mindmap::MindmapParseConfig config;
  const QJsonObject object = pre.config.value(QStringLiteral("mindmap")).toObject();
  const auto scalar = [&object](const QString& key, const QJsonValue& fallback) {
    const QJsonValue value = object.value(key);
    return value.isUndefined() || value.isNull() ? fallback : value;
  };
  config.padding = scalar(QStringLiteral("padding"), QJsonValue(10.0));
  config.maxNodeWidth = scalar(QStringLiteral("maxNodeWidth"), QJsonValue(200.0));
  config.useMaxWidth = scalar(QStringLiteral("useMaxWidth"), QJsonValue(true));
  if (pre.config.value(QStringLiteral("look")).isString())
    config.look = pre.config.value(QStringLiteral("look")).toString();
  if (pre.config.value(QStringLiteral("theme")).isString())
    config.theme = pre.config.value(QStringLiteral("theme")).toString();
  if (pre.config.contains(QStringLiteral("layout"))) {
    config.userDefinedLayout = true;
    config.layout = pre.config.value(QStringLiteral("layout")).toString();
  }
  return config;
}

mindmap::MindmapErrorKind expectedKind(const QString& value) {
  if (value == QLatin1String("lexer")) return mindmap::MindmapErrorKind::Lexer;
  if (value == QLatin1String("runtime")) return mindmap::MindmapErrorKind::Runtime;
  return mindmap::MindmapErrorKind::Parser;
}

bool sourceEntryDetectsMindmap(const QString& source) {
  try {
    return detectDiagramType(source) == QLatin1String("mindmap");
  } catch (const UnknownDiagramError&) {
    return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Mindmap grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray fixtureBytes = file.readAll();
  QByteArray canonicalBytes = fixtureBytes;
  canonicalBytes.replace("\r\n", "\n");
  canonicalBytes.replace('\r', '\n');
  require(QCryptographicHash::hash(canonicalBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral("c1d3e626ff925fcc0af4941ff2c8006a5101c5087c85cacf887d8e7c9c633fae"),
          QStringLiteral("Mindmap fixture file sha256 drift"));

  QJsonParseError jsonError;
  const QJsonDocument document = QJsonDocument::fromJson(fixtureBytes, &jsonError);
  require(jsonError.error == QJsonParseError::NoError, jsonError.errorString());
  const QJsonObject root = document.object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Mindmap oracle version drift"));
  require(upstream.value(QStringLiteral("moduleSha256")).toString() ==
              QLatin1String("fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b"),
          QStringLiteral("Mermaid module hash drift"));
  require(upstream.value(QStringLiteral("mindmapModuleSha256")).toString() ==
              QLatin1String("584e20b0980902d5749171aca4d48dcdfe9e674df21cdbade0077e7214511ea8"),
          QStringLiteral("Mindmap chunk hash drift"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("d87fb2c9d93602f27c1066de0c073e058cdd24e90aaa2bfffe0f3d51bd47a94f"),
          QStringLiteral("Mindmap semantic fixture hash drift"));

  int accepted = 0;
  int rejected = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool expectedAccept = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();
    if (!sourceEntryDetectsMindmap(source)) {
      require(!expectedAccept && reject.value(QStringLiteral("class")).toString() ==
                                     QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    const MermaidPreprocessResult pre = preprocessDiagram(source);
    try {
      const mindmap::MindmapData actual =
          mindmap::MindmapDiagram::parse(pre.code, parseConfig(pre));
      require(expectedAccept, id + QStringLiteral(": unexpectedly accepted"));
      const QJsonObject expected = fixture.value(QStringLiteral("expectedDb")).toObject();
      const QJsonValue expectedRoot = expected.value(QStringLiteral("root"));
      if (expectedRoot.isNull())
        require(actual.rootId < 0, id + QStringLiteral("/root expected null"));
      else
        compareRawNode(actual, actual.rootId, expectedRoot.toObject(),
                       id + QStringLiteral("/root"));
      compareFlatNodes(actual, expected.value(QStringLiteral("nodes")).toArray(),
                       id + QStringLiteral("/nodes"));
      compareEdges(actual, expected.value(QStringLiteral("edges")).toArray(),
                   id + QStringLiteral("/edges"));
      const QJsonObject expectedConfig = expected.value(QStringLiteral("config")).toObject();
      require(actual.config.theme == expectedConfig.value(QStringLiteral("theme")).toString(),
              id + QStringLiteral("/config/theme mismatch"));
      require(actual.config.look == expectedConfig.value(QStringLiteral("look")).toString(),
              id + QStringLiteral("/config/look mismatch"));
      require(actual.effectiveLayout == expectedConfig.value(QStringLiteral("layout")).toString(),
              id + QStringLiteral("/config/layout mismatch"));
      require(sameScalar(actual.config.padding,
                         expectedConfig.value(QStringLiteral("mindmapPadding"))),
              id + QStringLiteral("/config/padding mismatch"));
      require(sameScalar(actual.config.maxNodeWidth,
                         expectedConfig.value(QStringLiteral("mindmapMaxNodeWidth"))),
              id + QStringLiteral("/config/maxNodeWidth mismatch"));
      require(sameScalar(actual.config.useMaxWidth,
                         expectedConfig.value(QStringLiteral("mindmapUseMaxWidth"))),
              id + QStringLiteral("/config/useMaxWidth mismatch"));
      ++accepted;
    } catch (const mindmap::MindmapParseError& error) {
      require(!expectedAccept,
              id + QStringLiteral(": unexpectedly rejected: ") + error.what());
      require(error.kind == expectedKind(reject.value(QStringLiteral("class")).toString()),
              id + QStringLiteral(": reject kind mismatch"));
      const int line = reject.value(QStringLiteral("line")).toInt();
      const int column = reject.value(QStringLiteral("column")).toInt();
      if (line > 0)
        require(error.line == line,
                id + QStringLiteral(": line mismatch actual=%1 expected=%2 "
                                    "column=%3 token=%4 message=%5 code=%6")
                         .arg(error.line)
                         .arg(line)
                         .arg(error.column)
                         .arg(error.token, QString::fromUtf8(error.what()))
                         .arg(pre.code));
      if (column > 0)
        require(error.column == column,
                id + QStringLiteral(": column mismatch actual=%1 expected=%2")
                         .arg(error.column)
                         .arg(column));
      if (reject.value(QStringLiteral("token")).isString() &&
          !reject.value(QStringLiteral("token")).toString().isEmpty())
        require(error.token == reject.value(QStringLiteral("token")).toString(),
                id + QStringLiteral(": token mismatch"));
      if (error.kind == mindmap::MindmapErrorKind::Runtime)
        require(QString::fromUtf8(error.what()) ==
                    reject.value(QStringLiteral("message")).toString(),
                id + QStringLiteral(": runtime message mismatch"));
      ++rejected;
    }
  }
  require(accepted == 53,
          QStringLiteral("Mindmap accepted count %1 != 53").arg(accepted));
  require(rejected == 16,
          QStringLiteral("Mindmap rejected count %1 != 16").arg(rejected));
  std::printf("MermaidMindmapParserTest: %d accept + %d reject cases passed\n",
              accepted, rejected);
  return 0;
}
