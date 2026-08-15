#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/railroad/RailroadDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
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

QString nodeType(railroad::RailroadNodeType type) {
  switch (type) {
    case railroad::RailroadNodeType::Terminal: return QStringLiteral("terminal");
    case railroad::RailroadNodeType::NonTerminal: return QStringLiteral("nonterminal");
    case railroad::RailroadNodeType::Sequence: return QStringLiteral("sequence");
    case railroad::RailroadNodeType::Choice: return QStringLiteral("choice");
    case railroad::RailroadNodeType::Optional: return QStringLiteral("optional");
    case railroad::RailroadNodeType::Repetition: return QStringLiteral("repetition");
    case railroad::RailroadNodeType::Special: return QStringLiteral("special");
  }
  return {};
}

void compareNode(const railroad::RailroadNode& actual,
                 const QJsonObject& expected, const QString& path) {
  const QString type = expected.value(QStringLiteral("type")).toString();
  require(nodeType(actual.type) == type, path + QStringLiteral("/type"));
  if (type == QLatin1String("terminal"))
    require(actual.text == expected.value(QStringLiteral("value")).toString(),
            path + QStringLiteral("/value"));
  else if (type == QLatin1String("nonterminal"))
    require(actual.text == expected.value(QStringLiteral("name")).toString(),
            path + QStringLiteral("/name"));
  else if (type == QLatin1String("special"))
    require(actual.text == expected.value(QStringLiteral("text")).toString(),
            QStringLiteral("%1/text: '%2' != '%3'")
                .arg(path, actual.text,
                     expected.value(QStringLiteral("text")).toString()));

  QJsonArray children;
  if (type == QLatin1String("sequence"))
    children = expected.value(QStringLiteral("elements")).toArray();
  else if (type == QLatin1String("choice"))
    children = expected.value(QStringLiteral("alternatives")).toArray();
  else if (type == QLatin1String("optional") ||
           type == QLatin1String("repetition"))
    children.append(expected.value(QStringLiteral("element")));
  require(actual.children.size() == children.size(),
          path + QStringLiteral("/child count"));
  for (qsizetype i = 0; i < actual.children.size(); ++i)
    compareNode(actual.children.at(i), children.at(i).toObject(),
                path + QStringLiteral("/children/%1").arg(i));

  if (type == QLatin1String("repetition")) {
    require(actual.min == expected.value(QStringLiteral("min")).toDouble(),
            path + QStringLiteral("/min"));
    const QJsonValue maximum = expected.value(QStringLiteral("max"));
    require((maximum.isNull() && std::isinf(actual.max)) ||
                (!maximum.isNull() && actual.max == maximum.toDouble()),
            path + QStringLiteral("/max"));
    const QJsonValue separator = expected.value(QStringLiteral("separator"));
    const bool hasSeparator = !separator.isUndefined() && !separator.isNull();
    require(!actual.separator.isEmpty() == hasSeparator,
            path + QStringLiteral("/separator presence"));
    if (hasSeparator) {
      require(actual.separator.size() == 1,
              path + QStringLiteral("/separator count"));
      compareNode(actual.separator.front(), separator.toObject(),
                  path + QStringLiteral("/separator"));
    }
  }
}

railroad::RailroadDialect dialectFor(const QString& type) {
  if (type == QLatin1String("railroadEbnf"))
    return railroad::RailroadDialect::Ebnf;
  if (type == QLatin1String("railroadAbnf"))
    return railroad::RailroadDialect::Abnf;
  if (type == QLatin1String("railroadPeg"))
    return railroad::RailroadDialect::Peg;
  return railroad::RailroadDialect::Direct;
}

void compareDatabase(const railroad::RailroadData& actual,
                     const QJsonObject& expected, const QString& id) {
  require(actual.title == expected.value(QStringLiteral("title")).toString(),
          id + QStringLiteral("/title"));
  require(actual.accTitle ==
              expected.value(QStringLiteral("accTitle")).toString(),
          id + QStringLiteral("/accTitle"));
  require(actual.accDescr ==
              expected.value(QStringLiteral("accDescr")).toString(),
          id + QStringLiteral("/accDescr"));
  const QJsonArray rules = expected.value(QStringLiteral("rules")).toArray();
  require(actual.rules.size() == rules.size(), id + QStringLiteral("/rule count"));
  for (qsizetype i = 0; i < actual.rules.size(); ++i) {
    const QJsonObject expectedRule = rules.at(i).toObject();
    const auto& rule = actual.rules.at(i);
    const QString path = id + QStringLiteral("/rules/%1").arg(i);
    require(rule.name == expectedRule.value(QStringLiteral("name")).toString(),
            path + QStringLiteral("/name"));
    require(rule.comment ==
                expectedRule.value(QStringLiteral("comment")).toString(),
            path + QStringLiteral("/comment"));
    compareNode(rule.definition,
                expectedRule.value(QStringLiteral("definition")).toObject(),
                path + QStringLiteral("/definition"));
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected railroad grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("cc3dbf979c0e8c2ea45d010116adb1caebb506d2051677aaa3e1967682883b1a"),
          QStringLiteral("Railroad grammar fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("5b99dfb4224576630c9d05a4ef11518d2d5af1c3402d9a320c7a1ea73e68467e"),
          QStringLiteral("Railroad grammar fixture provenance drifted"));
  require(root.value(QStringLiteral("provenance")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Railroad Mermaid version drifted"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const MermaidPreprocessResult pre =
        preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    QString type;
    try { type = detectDiagramType(pre.code, pre.config); }
    catch (const UnknownDiagramError&) {}

    if (!fixture.value(QStringLiteral("accepted")).toBool()) {
      ++rejected;
      if (type.isEmpty()) continue;
      bool threw = false;
      try { (void)railroad::RailroadDiagram::parse(pre.code, dialectFor(type)); }
      catch (const railroad::RailroadParseError&) { threw = true; }
      require(threw, id + QStringLiteral(": upstream rejected but native parsed"));
      continue;
    }

    ++accepted;
    require(type.startsWith(QLatin1String("railroad")),
            id + QStringLiteral(": detector rejected accepted source"));
    compareDatabase(
        railroad::RailroadDiagram::parse(pre.code, dialectFor(type)),
        fixture.value(QStringLiteral("database")).toObject(), id);
  }
  require(cases.size() == 33 && accepted == 19 && rejected == 14,
          QStringLiteral("Railroad grammar coverage drifted"));
  std::puts("MermaidRailroadParserTest: 33 cases passed");
  return 0;
}
