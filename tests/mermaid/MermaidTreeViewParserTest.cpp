#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/treeview/TreeViewDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

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

void compareNode(const treeview::TreeViewData& actual, int actualIndex,
                 const QJsonObject& expected, const QString& path) {
  require(actualIndex >= 0 && actualIndex < actual.nodes.size(),
          path + QStringLiteral("/native-index"));
  const treeview::TreeViewNode& node = actual.nodes.at(actualIndex);
  require(node.id == expected.value(QStringLiteral("id")).toInt(),
          path + QStringLiteral("/id"));
  require(node.level == expected.value(QStringLiteral("level")).toInt(),
          path + QStringLiteral("/level"));
  require(node.name == expected.value(QStringLiteral("name")).toString(),
          path + QStringLiteral("/name [%1] != [%2]")
                     .arg(node.name,
                          expected.value(QStringLiteral("name")).toString()));
  require(node.directory ==
              (expected.value(QStringLiteral("nodeType")).toString() ==
               QLatin1String("directory")),
          path + QStringLiteral("/nodeType"));
  const QJsonValue expectedIcon = expected.value(QStringLiteral("icon"));
  require(node.hasIcon == !expectedIcon.isNull(),
          path + QStringLiteral("/hasIcon"));
  if (!expectedIcon.isNull())
    require(node.icon == expectedIcon.toString(),
            path + QStringLiteral("/icon"));
  const auto expectedString = [&](const char* key) {
    const QJsonValue value = expected.value(QLatin1String(key));
    return value.isNull() ? QString() : value.toString();
  };
  require(node.cssClass == expectedString("cssClass"),
          path + QStringLiteral("/cssClass"));
  require(node.description == expectedString("description"),
          path + QStringLiteral("/description"));
  const QJsonArray children = expected.value(QStringLiteral("children")).toArray();
  require(node.children.size() == children.size(),
          path + QStringLiteral("/children %1 != %2")
                     .arg(node.children.size())
                     .arg(children.size()));
  for (int i = 0; i < children.size(); ++i)
    compareNode(actual, node.children.at(i), children.at(i).toObject(),
                path + QStringLiteral("/children/%1").arg(i));
}

treeview::TreeViewErrorKind errorKind(const QString& value) {
  if (value == QLatin1String("lexer"))
    return treeview::TreeViewErrorKind::Lexer;
  if (value == QLatin1String("preprocess"))
    return treeview::TreeViewErrorKind::Preprocess;
  return treeview::TreeViewErrorKind::Parser;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected TreeView grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  QByteArray canonical = bytes;
  canonical.replace("\r\n", "\n");
  canonical.replace('\r', '\n');
  require(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral(
                  "35dac9a91673913dd8087dd508fa2c7c380fb0e543693000550338269593a94d"),
          QStringLiteral("TreeView grammar fixture bytes changed"));
  QJsonParseError error;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &error).object();
  require(error.error == QJsonParseError::NoError, error.errorString());
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("TreeView upstream version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "e5eeb0749d306f415b312caad20261de4283574275ebce2c9a88dc1f01dc0848"),
          QStringLiteral("TreeView grammar semantic fixture changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) ==
                 QLatin1String("treeView");
    } catch (const UnknownDiagramError&) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source not detected"));
      require(fixture.value(QStringLiteral("reject")).toObject()
                      .value(QStringLiteral("kind")).toString() ==
                  QLatin1String("no-diagram"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    if (accept) {
      ++accepted;
      treeview::TreeViewData data;
      try {
        data = treeview::TreeViewDiagram::parse(pre.code);
      } catch (const treeview::TreeViewParseError& parseError) {
        fail(id + QStringLiteral(": accepted input threw %1:%2 %3")
                      .arg(parseError.line)
                      .arg(parseError.column)
                      .arg(QString::fromUtf8(parseError.what())));
      }
      const QJsonObject db = fixture.value(QStringLiteral("db")).toObject();
      require(data.title == db.value(QStringLiteral("title")).toString(),
              id + QStringLiteral("/title"));
      require(data.accTitle == db.value(QStringLiteral("accTitle")).toString(),
              id + QStringLiteral("/accTitle"));
      require(data.accDescr == db.value(QStringLiteral("accDescr")).toString(),
              id + QStringLiteral("/accDescr"));
      compareNode(data, data.rootIndex,
                  db.value(QStringLiteral("root")).toObject(), id + "/root");
      continue;
    }

    ++rejected;
    const QJsonObject expected =
        fixture.value(QStringLiteral("reject")).toObject();
    bool threw = false;
    try {
      (void)treeview::TreeViewDiagram::parse(pre.code);
    } catch (const treeview::TreeViewParseError& parseError) {
      threw = true;
      require(parseError.kind == errorKind(expected.value(QStringLiteral("kind")).toString()),
              id + QStringLiteral(": error kind"));
      const int line = expected.value(QStringLiteral("line")).toInt();
      if (line > 0)
        require(parseError.line == line,
                id + QStringLiteral(": line %1 != %2")
                         .arg(parseError.line)
                         .arg(line));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }
  require(cases.size() == 37 && accepted == 26 && rejected == 11,
          QStringLiteral("TreeView grammar table not fully visited"));
  std::puts("MermaidTreeViewParserTest: 37 source-entry cases passed");
  return 0;
}
