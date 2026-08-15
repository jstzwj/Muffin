#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/treemap/TreemapDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString &message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool condition, const QString &message) {
  if (!condition)
    fail(message);
}

void compareNode(const treemap::TreemapData &data, int index,
                 const QJsonObject &expected, const QString &path) {
  require(index >= 0 && index < data.nodes.size(), path + "/index");
  const auto &node = data.nodes.at(index);
  require(node.name == expected.value(QStringLiteral("name")).toString(),
          path + QStringLiteral("/name [%1] != [%2]")
                     .arg(node.name,
                          expected.value(QStringLiteral("name")).toString()));
  const bool hasValue = expected.contains(QStringLiteral("value"));
  require(node.hasValue == hasValue, path + "/hasValue");
  if (hasValue) {
    const QJsonValue value = expected.value(QStringLiteral("value"));
    if (value.isNull())
      require(std::isnan(node.value), path + "/value NaN");
    else
      require(std::abs(node.value - value.toDouble()) < 1e-12,
              path + "/value");
  }
  require(node.classSelector ==
              expected.value(QStringLiteral("classSelector")).toString(),
          path + "/classSelector");
  const QJsonArray styles =
      expected.value(QStringLiteral("cssCompiledStyles")).toArray();
  require(node.cssCompiledStyles.size() == styles.size(), path + "/styles");
  for (int i = 0; i < styles.size(); ++i)
    require(node.cssCompiledStyles.at(i) == styles.at(i).toString(),
            path + QStringLiteral("/styles/%1").arg(i));
  const QJsonArray children = expected.value(QStringLiteral("children")).toArray();
  require(node.children.size() == children.size(), path + "/children");
  for (int i = 0; i < children.size(); ++i)
    compareNode(data, node.children.at(i), children.at(i).toObject(),
                path + QStringLiteral("/children/%1").arg(i));
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Treemap grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "9ec4859b13096f846bb18fcdf6f68b097dff1775492b48650af492128d5cc03f"),
          QStringLiteral("Treemap grammar fixture bytes changed"));
  QJsonParseError jsonError;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError, jsonError.errorString());
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Treemap upstream version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "86381f50f147c463bcfc43fd492b430cf819a9bce59b0a265dedac28bdc02d75"),
          QStringLiteral("Treemap grammar semantic fixture changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue &entry : cases) {
    const QJsonObject fixture = entry.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const bool accept = expected.value(QStringLiteral("parse")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("treemap");
    } catch (const UnknownDiagramError &) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source not detected"));
      require(expected.value(QStringLiteral("error")).toObject()
                      .value(QStringLiteral("name")).toString() ==
                  QLatin1String("UnknownDiagramError"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    if (accept) {
      ++accepted;
      treemap::TreemapData data;
      try {
        data = treemap::TreemapDiagram::parse(pre.code);
      } catch (const treemap::TreemapParseError &error) {
        fail(id + QStringLiteral(": accepted input threw %1:%2 %3")
                      .arg(error.line).arg(error.column)
                      .arg(QString::fromUtf8(error.what())));
      }
      const QJsonObject db = expected.value(QStringLiteral("db")).toObject();
      require(data.title == db.value(QStringLiteral("title")).toString(),
              id + "/title");
      require(data.accTitle == db.value(QStringLiteral("accTitle")).toString(),
              id + "/accTitle");
      require(data.accDescr == db.value(QStringLiteral("accDescr")).toString(),
              id + "/accDescr");
      const QJsonArray roots = db.value(QStringLiteral("root")).toObject()
                                   .value(QStringLiteral("children")).toArray();
      require(data.roots.size() == roots.size(), id + "/roots");
      for (int i = 0; i < roots.size(); ++i)
        compareNode(data, data.roots.at(i), roots.at(i).toObject(),
                    id + QStringLiteral("/root/%1").arg(i));
      continue;
    }

    ++rejected;
    const QJsonObject errorExpected = expected.value(QStringLiteral("error")).toObject();
    bool threw = false;
    try {
      (void)treemap::TreemapDiagram::parse(pre.code);
    } catch (const treemap::TreemapParseError &error) {
      threw = true;
      const int line = errorExpected.value(QStringLiteral("line")).toInt();
      const int column = errorExpected.value(QStringLiteral("column")).toInt();
      if (line > 0)
        require(error.line == line,
                id + QStringLiteral(": line %1 != %2").arg(error.line).arg(line));
      if (column > 0)
        require(error.column == column,
                id + QStringLiteral(": column %1 != %2")
                         .arg(error.column).arg(column));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }
  require(cases.size() == 39 && accepted == 30 && rejected == 9,
          QStringLiteral("Treemap grammar table not fully visited"));
  std::puts("MermaidTreemapParserTest: 39 source-entry cases passed");
  return 0;
}
