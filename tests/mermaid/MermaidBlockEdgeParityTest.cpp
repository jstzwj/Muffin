#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) { if (!value) fail(message); }
QVector<qreal> numbers(const QString& value) {
  static const QRegularExpression pattern(QStringLiteral(
      R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
  QVector<qreal> result;
  auto matches = pattern.globalMatch(value);
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}
bool close(qreal actual, qreal expected) {
  return std::isfinite(actual) && std::isfinite(expected) &&
      std::abs(actual - expected) <=
          std::max<qreal>(0.005, std::abs(expected) * 1e-10);
}
bool samePaint(const QString& actual, const QString& expected) {
  if (!color::isParsableColor(actual) || !color::isParsableColor(expected))
    return actual.trimmed().compare(expected.trimmed(),
                                    Qt::CaseInsensitive) == 0;
  return color::toQColor(actual).rgba() == color::toQColor(expected).rgba();
}
std::shared_ptr<const block::BlockScene> render(
    const QString& source, editor::MermaidRenderEntry* entry = nullptr) {
  editor::MermaidRenderCache cache;
  auto value = cache.getSync(cache.makeKey(source), source);
  if (entry) *entry = value;
  if (value.status != editor::MermaidRenderStatus::Ready) return {};
  return std::dynamic_pointer_cast<const block::BlockScene>(value.scene);
}
void compareCase(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = render(fixture.value(QStringLiteral("source")).toString(),
                            &entry);
  require(scene != nullptr, id + QStringLiteral("/ready"));
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject attrs = root.value(QStringLiteral("attrs")).toObject();
  const QVector<qreal> viewBox =
      numbers(attrs.value(QStringLiteral("viewBox")).toString());
  require(viewBox.size() == 4, id + QStringLiteral("/viewBox-oracle"));
  const bool systemFontCase = id == QLatin1String("font-family");
  require(systemFontCase || (close(scene->bounds.x(), viewBox[0]) &&
              close(scene->bounds.y(), viewBox[1]) &&
              close(scene->bounds.width(), viewBox[2]) &&
              close(scene->bounds.height(), viewBox[3])),
          id + QStringLiteral("/viewBox %1,%2 %3x%4 vs %5,%6 %7x%8")
                   .arg(scene->bounds.x()).arg(scene->bounds.y())
                   .arg(scene->bounds.width()).arg(scene->bounds.height())
                   .arg(viewBox[0]).arg(viewBox[1])
                   .arg(viewBox[2]).arg(viewBox[3]));
  const bool expectedMaxWidth =
      attrs.value(QStringLiteral("width")).toString() == QLatin1String("100%");
  require(scene->useMaxWidth == expectedMaxWidth,
          id + QStringLiteral("/useMaxWidth"));

  const QJsonObject computed = root.value(QStringLiteral("computed")).toObject();
  QString expectedFamily = computed.value(QStringLiteral("fontFamily")).toString();
  expectedFamily.remove(QLatin1Char('"'));
  require(scene->fontFamily.contains(expectedFamily, Qt::CaseInsensitive) ||
              expectedFamily.contains(scene->fontFamily, Qt::CaseInsensitive),
          id + QStringLiteral("/fontFamily"));

  QJsonObject expectedNode;
  QJsonObject expectedEdge;
  for (const QJsonValue& primitiveValue :
       expected.value(QStringLiteral("primitives")).toArray()) {
    const QJsonObject primitive = primitiveValue.toObject();
    const QJsonObject primitiveAttrs =
        primitive.value(QStringLiteral("attrs")).toObject();
    if (expectedNode.isEmpty() &&
        primitive.value(QStringLiteral("parentClass")).toString()
            .startsWith(QLatin1String("node ")) &&
        primitive.value(QStringLiteral("tag")).toString() != QLatin1String("g"))
      expectedNode = primitive.value(QStringLiteral("computed")).toObject();
    if (expectedEdge.isEmpty() &&
        primitiveAttrs.value(QStringLiteral("class")).toString()
            .contains(QStringLiteral("flowchart-link")))
      expectedEdge = primitive.value(QStringLiteral("computed")).toObject();
  }
  if (!scene->flow.nodes.isEmpty() && !expectedNode.isEmpty()) {
    const auto& node = scene->flow.nodes.first();
    require(samePaint(node.fill,
                      expectedNode.value(QStringLiteral("fill")).toString()),
            id + QStringLiteral("/node-fill"));
    require(samePaint(node.stroke,
                      expectedNode.value(QStringLiteral("stroke")).toString()),
            id + QStringLiteral("/node-stroke"));
  }
  if (!scene->flow.edges.isEmpty() && !expectedEdge.isEmpty()) {
    require(samePaint(scene->flow.edges.first().stroke,
                      expectedEdge.value(QStringLiteral("stroke")).toString()),
            id + QStringLiteral("/edge-stroke"));
  }
  if (id == QLatin1String("frontmatter")) {
    require(entry.metadata.title.isEmpty() &&
                entry.metadata.accessibleTitle.isEmpty() &&
                !entry.metadata.svgEmitAccessibleTitle,
            id + QStringLiteral("/metadata-ignored"));
  }
}
void requireDiagnostic(const QString& source, const QString& code) {
  editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Error,
          code + QStringLiteral("/status"));
  require(entry.diagnostic.stage == QLatin1String("parse") &&
              entry.diagnostic.code == code,
          code + QStringLiteral("/diagnostic"));
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Block config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("e6b299a3ff1182b0bc4f4228d9a3b77f0efe4375ed1fba83de83b200dd8514ed"),
          QStringLiteral("Block config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("db72a05c77ed386dc4d75298bf139c93cc04017840f20bdc1b84381bd7ba2901") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Block config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 31, QStringLiteral("Block config case count"));
  for (const QJsonValue& value : cases) compareCase(value.toObject());

  requireDiagnostic(QStringLiteral("block-beta\na[A]"),
                    QStringLiteral("block-lexer-error"));
  requireDiagnostic(QStringLiteral("block-beta"),
                    QStringLiteral("block-parse-error"));
  requireDiagnostic(QStringLiteral("block-beta\ncolumns 0\na[\"A\"]"),
                    QStringLiteral("block-runtime-error"));
  std::puts("MermaidBlockEdgeParityTest: 31/31 passed");
  return 0;
}
