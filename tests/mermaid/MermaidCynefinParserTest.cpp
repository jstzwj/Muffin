#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/cynefin/CynefinDiagram.h"

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
[[noreturn]] void fail(const QString &message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString &message) {
  if (!condition) fail(message);
}
QString kind(cynefin::CynefinErrorKind value) {
  if (value == cynefin::CynefinErrorKind::Lexer) return QStringLiteral("Lexer");
  if (value == cynefin::CynefinErrorKind::Runtime) return QStringLiteral("Runtime");
  return QStringLiteral("Parser");
}
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Cynefin grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "4455acab8abfa5bb37cbffdb6eeb4e1dd19a9b06a7dd767583bb5ab89052e35a"),
          QStringLiteral("Cynefin grammar fixture bytes changed"));
  QJsonParseError jsonError;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError, jsonError.errorString());
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("cynefinModuleSha256")).toString() ==
                  QLatin1String(
                      "3dbe403effb1abbc413cce6a4433bc2dd59feef86c5da296057459b40a5ffc9c") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "e89d8d33ea05b05e538b12f141b3063ef3ecb4d7d4421e0f841b81c5688db0d3"),
          QStringLiteral("Cynefin grammar provenance changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue &value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const bool accept = expected.value(QStringLiteral("parse")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("cynefin");
    } catch (const UnknownDiagramError &) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source was not detected"));
      require(expected.value(QStringLiteral("error")).toObject()
                      .value(QStringLiteral("name")).toString() ==
                  QLatin1String("UnknownDiagramError"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }

    if (!accept) {
      ++rejected;
      bool threw = false;
      try {
        (void)cynefin::CynefinDiagram::parse(pre.code);
      } catch (const cynefin::CynefinParseError &error) {
        threw = true;
        const QJsonObject oracle = expected.value(QStringLiteral("error")).toObject();
        require(kind(error.kind) == oracle.value(QStringLiteral("kind")).toString(),
                id + QStringLiteral("/kind"));
        require(error.line == oracle.value(QStringLiteral("line")).toInt(),
                id + QStringLiteral("/line %1 != %2")
                         .arg(error.line)
                         .arg(oracle.value(QStringLiteral("line")).toInt()));
        require(error.column == oracle.value(QStringLiteral("column")).toInt(),
                id + QStringLiteral("/column %1 != %2")
                         .arg(error.column)
                         .arg(oracle.value(QStringLiteral("column")).toInt()));
      }
      require(threw, id + QStringLiteral(": rejected source parsed"));
      continue;
    }

    ++accepted;
    const cynefin::CynefinData data = cynefin::CynefinDiagram::parse(pre.code);
    const QJsonObject db = expected.value(QStringLiteral("db")).toObject();
    require(data.title == db.value(QStringLiteral("title")).toString(), id + "/title");
    require(data.accTitle == db.value(QStringLiteral("accTitle")).toString(),
            id + "/accTitle");
    require(data.accDescr == db.value(QStringLiteral("accDescr")).toString(),
            id + "/accDescr");
    const QJsonArray domains = db.value(QStringLiteral("domains")).toArray();
    require(data.domains.size() == domains.size(), id + "/domain-count");
    for (qsizetype i = 0; i < domains.size(); ++i) {
      const QJsonArray pair = domains.at(i).toArray();
      const QJsonObject domain = pair.at(1).toObject();
      require(data.domains.at(i).name == pair.at(0).toString() &&
                  data.domains.at(i).name == domain.value(QStringLiteral("name")).toString(),
              id + QStringLiteral("/domain/%1/name").arg(i));
      const QJsonArray items = domain.value(QStringLiteral("items")).toArray();
      require(data.domains.at(i).items.size() == items.size(),
              id + QStringLiteral("/domain/%1/item-count").arg(i));
      for (qsizetype j = 0; j < items.size(); ++j)
        require(data.domains.at(i).items.at(j).label ==
                    items.at(j).toObject().value(QStringLiteral("label")).toString(),
                id + QStringLiteral("/domain/%1/item/%2").arg(i).arg(j));
    }
    const QJsonArray transitions = db.value(QStringLiteral("transitions")).toArray();
    require(data.transitions.size() == transitions.size(), id + "/transition-count");
    for (qsizetype i = 0; i < transitions.size(); ++i) {
      const QJsonObject oracle = transitions.at(i).toObject();
      const auto &actual = data.transitions.at(i);
      require(actual.from == oracle.value(QStringLiteral("from")).toString() &&
                  actual.to == oracle.value(QStringLiteral("to")).toString(),
              id + QStringLiteral("/transition/%1/endpoints").arg(i));
      const bool hasLabel = oracle.contains(QStringLiteral("label"));
      require(actual.hasLabel == hasLabel &&
                  (!hasLabel || actual.label == oracle.value(QStringLiteral("label")).toString()),
              id + QStringLiteral("/transition/%1/label").arg(i));
    }
  }
  require(cases.size() == 38 && accepted == 29 && rejected == 9,
          QStringLiteral("Cynefin grammar table not fully visited"));
  std::puts("MermaidCynefinParserTest: 38/38 passed");
  return 0;
}
