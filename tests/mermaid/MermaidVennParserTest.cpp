#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/venn/VennDiagram.h"

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

QJsonArray stringArray(const QStringList& values) {
  QJsonArray result;
  for (const QString& value : values) result.append(value);
  return result;
}

QJsonObject subsetJson(const venn::VennSubset& subset) {
  QJsonObject result{{QStringLiteral("sets"), stringArray(subset.sets)},
                     {QStringLiteral("size"), subset.size}};
  if (subset.hasLabel) result.insert(QStringLiteral("label"), subset.label);
  return result;
}

QJsonObject textNodeJson(const venn::VennTextNode& node) {
  QJsonObject result{{QStringLiteral("sets"), stringArray(node.sets)},
                     {QStringLiteral("id"), node.id}};
  if (node.hasLabel) result.insert(QStringLiteral("label"), node.label);
  return result;
}

QJsonObject styleJson(const venn::VennStyleEntry& style) {
  QJsonObject declarations;
  for (const auto& declaration : style.declarations)
    declarations.insert(declaration.first, declaration.second);
  return {{QStringLiteral("targets"), stringArray(style.targets)},
          {QStringLiteral("styles"), declarations}};
}

QJsonObject dataJson(const venn::VennData& data) {
  QJsonArray subsets;
  for (const auto& subset : data.subsets) subsets.append(subsetJson(subset));
  QJsonArray textNodes;
  for (const auto& node : data.textNodes) textNodes.append(textNodeJson(node));
  QJsonArray styles;
  for (const auto& style : data.styles) styles.append(styleJson(style));
  return {{QStringLiteral("subsets"), subsets},
          {QStringLiteral("textNodes"), textNodes},
          {QStringLiteral("styles"), styles},
          {QStringLiteral("title"), data.title},
          {QStringLiteral("accTitle"), data.accTitle},
          {QStringLiteral("accDescr"), data.accDescr}};
}

QString errorKind(venn::VennErrorKind kind) {
  if (kind == venn::VennErrorKind::Lexer) return QStringLiteral("lexer");
  if (kind == venn::VennErrorKind::Runtime) return QStringLiteral("runtime");
  return QStringLiteral("parser");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Venn grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "bd225f48455c40252e20e7e166b957cf32cb8e42a31e8f2e6d7f4c4cc7036f18"),
          QStringLiteral("Venn grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "92ba76150149396c2bc0a5d16629e083543a9f2d59fcb05bda214886ce48d088"),
          QStringLiteral("Venn grammar fixture provenance changed"));
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("vennModuleSha256")).toString() ==
                  QLatin1String(
                      "382a5b11e80ff80f1481f88c113cc036879d160f7fcd1a029d0f4a08b7548eb7"),
          QStringLiteral("Venn upstream provenance changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const MermaidPreprocessResult pre =
        preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("venn");
    } catch (const UnknownDiagramError&) {
    }
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();
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
      venn::VennData data;
      try {
        data = venn::VennDiagram::parse(pre.code);
      } catch (const venn::VennParseError& error) {
        fail(QStringLiteral("%1: accepted input threw %2:%3 %4")
                 .arg(id)
                 .arg(error.line)
                 .arg(error.column)
                 .arg(QString::fromUtf8(error.what())));
      }
      if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
      require(dataJson(data) == fixture.value(QStringLiteral("db")).toObject(),
              id + QStringLiteral(": DB mismatch\nactual: ") +
                  QString::fromUtf8(QJsonDocument(dataJson(data)).toJson(
                      QJsonDocument::Compact)));
      continue;
    }

    ++rejected;
    const QJsonObject expected = fixture.value(QStringLiteral("reject")).toObject();
    bool threw = false;
    try {
      (void)venn::VennDiagram::parse(pre.code);
    } catch (const venn::VennParseError& error) {
      threw = true;
      require(errorKind(error.kind) ==
                  expected.value(QStringLiteral("kind")).toString(),
              id + QStringLiteral(": error kind, actual ") +
                  errorKind(error.kind));
      const int line = expected.value(QStringLiteral("line")).toInt();
      const int column = expected.value(QStringLiteral("column")).toInt();
      if (line > 0)
        require(error.line == line,
                id + QStringLiteral(": error line, actual ") +
                    QString::number(error.line));
      if (column > 0)
        require(error.column == column,
                id + QStringLiteral(": error column, actual ") +
                    QString::number(error.column));
      const QString token = expected.value(QStringLiteral("token")).toString();
      if (!token.isEmpty())
        require(error.token == token,
                id + QStringLiteral(": error token, actual ") + error.token);
      require(QString::fromUtf8(error.what()) ==
                  expected.value(QStringLiteral("message")).toString(),
              id + QStringLiteral(": error message, actual ") +
                  QString::fromUtf8(error.what()));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }

  require(cases.size() == 50 && accepted == 36 && rejected == 14,
          QStringLiteral("Venn grammar table not fully visited"));
  std::puts("MermaidVennParserTest: 50 source-entry cases passed");
  return 0;
}
