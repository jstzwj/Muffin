#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/theme/MermaidColor.h"

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

void require(bool value, const QString& message) {
  if (!value) fail(message);
}

std::shared_ptr<const mindmap::MindmapScene> render(
    const QString& source, editor::MermaidRenderEntry* output = nullptr) {
  editor::MermaidRenderCache cache;
  auto entry = cache.getSync(cache.makeKey(source), source);
  if (output) *output = entry;
  return entry.status == editor::MermaidRenderStatus::Ready
      ? std::dynamic_pointer_cast<const mindmap::MindmapScene>(entry.scene)
      : nullptr;
}

QVector<qreal> numbers(const QString& source) {
  static const QRegularExpression pattern(QStringLiteral(
      R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
  QVector<qreal> result;
  auto match = pattern.globalMatch(source);
  while (match.hasNext()) result.push_back(match.next().captured().toDouble());
  return result;
}

bool close(qreal actual, qreal expected, qreal tolerance = 0.8) {
  return std::isfinite(actual) && std::isfinite(expected) &&
      std::abs(actual - expected) <= tolerance;
}

bool samePaint(const QString& actual, const QString& expected) {
  if (!color::isParsableColor(actual) || !color::isParsableColor(expected))
    return actual.trimmed().compare(expected.trimmed(), Qt::CaseInsensitive) == 0;
  const QColor actualColor = color::toQColor(actual);
  const QColor expectedColor = color::toQColor(expected);
  return actualColor.rgba() == expectedColor.rgba();
}

void compareReadyCase(const QJsonObject& oracle,
                      const mindmap::MindmapScene& scene) {
  const QString id = oracle.value(QStringLiteral("id")).toString();
  const QJsonObject expected = oracle.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject rootAttrs = root.value(QStringLiteral("attrs")).toObject();
  const QVector<qreal> viewBox = numbers(
      rootAttrs.value(QStringLiteral("viewBox")).toString());
  require(viewBox.size() == 4, id + QStringLiteral(": root viewBox oracle"));
  require(close(scene.bounds.x(), viewBox[0]) &&
              close(scene.bounds.y(), viewBox[1]) &&
              close(scene.bounds.width(), viewBox[2]) &&
              close(scene.bounds.height(), viewBox[3]),
          id + QStringLiteral(": viewBox %1,%2 %3x%4 vs %5,%6 %7x%8")
              .arg(scene.bounds.x()).arg(scene.bounds.y())
              .arg(scene.bounds.width()).arg(scene.bounds.height())
              .arg(viewBox[0]).arg(viewBox[1])
              .arg(viewBox[2]).arg(viewBox[3]) +
              (scene.nodes.size() > 1
                   ? QStringLiteral(" centers %1,%2 / %3,%4 child %5x%6")
                         .arg(scene.nodes[0].center.x())
                         .arg(scene.nodes[0].center.y())
                         .arg(scene.nodes[1].center.x())
                         .arg(scene.nodes[1].center.y())
                         .arg(scene.nodes[1].layoutBounds.width())
                         .arg(scene.nodes[1].layoutBounds.height())
                   : QString()));
  const bool expectedMaxWidth =
      rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%");
  require(scene.useMaxWidth == expectedMaxWidth,
          id + QStringLiteral(": useMaxWidth"));

  const QJsonObject rootComputed =
      root.value(QStringLiteral("computed")).toObject();
  QString expectedFamily =
      rootComputed.value(QStringLiteral("fontFamily")).toString();
  expectedFamily.remove(QLatin1Char('"'));
  require(scene.style.fontFamily.contains(expectedFamily, Qt::CaseInsensitive) ||
              expectedFamily.contains(scene.style.fontFamily, Qt::CaseInsensitive),
          id + QStringLiteral(": font family"));
  const QString expectedSize =
      rootComputed.value(QStringLiteral("fontSize")).toString();
  require(close(scene.style.fontSize,
                expectedSize.left(expectedSize.size() - 2).toDouble(), 0.01),
          id + QStringLiteral(": font size"));

  const QJsonArray nodes = expected.value(QStringLiteral("nodes")).toArray();
  require(scene.nodes.size() == nodes.size(), id + QStringLiteral(": node count"));
  if (!nodes.isEmpty()) {
    const QJsonObject expectedNode = nodes.first().toObject();
    const QJsonObject shape = expectedNode.value(QStringLiteral("shape")).toObject();
    const QJsonObject shapeBox = shape.value(QStringLiteral("bbox")).toObject();
    const QJsonObject shapeComputed =
        shape.value(QStringLiteral("computed")).toObject();
    const QJsonObject nodeComputed =
        expectedNode.value(QStringLiteral("computed")).toObject();
    const QJsonObject label = expectedNode.value(QStringLiteral("label")).toObject();
    const QJsonObject labelBox = label.value(QStringLiteral("bbox")).toObject();
    const QJsonObject labelComputed =
        label.value(QStringLiteral("computed")).toObject();
    const auto& actual = scene.nodes.first();
    require(close(actual.localBounds.width(),
                  shapeBox.value(QStringLiteral("width")).toDouble()) &&
                close(actual.localBounds.height(),
                  shapeBox.value(QStringLiteral("height")).toDouble()),
            id + QStringLiteral(": root shape box"));
    require(close(actual.label.bounds.width(),
                  labelBox.value(QStringLiteral("width")).toDouble()) &&
                close(actual.label.bounds.height(),
                  labelBox.value(QStringLiteral("height")).toDouble()),
            id + QStringLiteral(": root label box"));
    if (actual.handDrawn) {
      const QJsonArray roughPaths = shape.value(QStringLiteral("roughPaths"))
          .toArray();
      require(roughPaths.size() >= 2,
              id + QStringLiteral(": root rough paths"));
      const QJsonObject fillAttrs = roughPaths.first().toObject()
          .value(QStringLiteral("attrs")).toObject();
      const QJsonObject strokeAttrs = roughPaths.last().toObject()
          .value(QStringLiteral("attrs")).toObject();
      require(samePaint(actual.roughDrawable.options.fill,
                        fillAttrs.value(QStringLiteral("stroke")).toString()),
              id + QStringLiteral(": root rough fill"));
      require(samePaint(actual.roughDrawable.options.stroke,
                        strokeAttrs.value(QStringLiteral("stroke")).toString()),
              id + QStringLiteral(": root rough stroke"));
      require(close(actual.roughDrawable.options.fillWeight,
                    numbers(fillAttrs.value(QStringLiteral("stroke-width"))
                                .toString()).value(0), 0.01),
              id + QStringLiteral(": root rough fill width"));
      require(close(actual.roughDrawable.options.strokeWidth,
                    numbers(strokeAttrs.value(QStringLiteral("stroke-width"))
                                .toString()).value(0), 0.01),
              id + QStringLiteral(": root rough stroke width"));
    } else {
      require(samePaint(actual.fill,
                        shapeComputed.value(QStringLiteral("fill")).toString()),
              id + QStringLiteral(": root shape fill %1 vs %2")
                  .arg(actual.fill,
                       shapeComputed.value(QStringLiteral("fill")).toString()));
      const QString expectedStroke =
          shapeComputed.value(QStringLiteral("stroke")).toString();
      require(expectedStroke.startsWith(QLatin1String("url("))
                  ? actual.gradient
                  : samePaint(actual.stroke, expectedStroke),
              id + QStringLiteral(": root shape stroke"));
      require(close(actual.strokeWidth,
                    numbers(shapeComputed.value(QStringLiteral("strokeWidth"))
                                .toString()).value(0), 0.01),
              id + QStringLiteral(": root shape stroke width"));
    }
    require(actual.dropShadow ==
                (nodeComputed.value(QStringLiteral("filter")).toString() !=
                 QLatin1String("none")),
            id + QStringLiteral(": root filter"));
    require(samePaint(actual.label.fill,
                      labelComputed.value(QStringLiteral("fill")).toString()),
            id + QStringLiteral(": root label fill %1 vs %2")
                .arg(actual.label.fill,
                     labelComputed.value(QStringLiteral("fill")).toString()));
    require(scene.config.htmlLabels ==
                label.value(QStringLiteral("foreignObject")).isObject(),
            id + QStringLiteral(": htmlLabels"));
  }

  const QJsonArray edges = expected.value(QStringLiteral("edges")).toArray();
  require(scene.edges.size() == edges.size(), id + QStringLiteral(": edge count"));
  if (!edges.isEmpty()) {
    const QJsonObject computed = edges.first().toObject()
        .value(QStringLiteral("computed")).toObject();
    require(samePaint(scene.edges.first().stroke,
                      computed.value(QStringLiteral("stroke")).toString()),
            id + QStringLiteral(": edge stroke %1 vs %2")
                     .arg(scene.edges.first().stroke,
                          computed.value(QStringLiteral("stroke")).toString()));
    require(close(scene.edges.first().strokeWidth,
                  numbers(computed.value(QStringLiteral("strokeWidth"))
                              .toString()).value(0), 0.01),
            id + QStringLiteral(": edge stroke width %1 vs %2")
                .arg(scene.edges.first().strokeWidth)
                .arg(computed.value(QStringLiteral("strokeWidth")).toString()));
  }
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("fixture arg"));
  QFile fixture(QString::fromLocal8Bit(argv[1]));
  require(fixture.open(QIODevice::ReadOnly), fixture.errorString());
  const QJsonObject root = QJsonDocument::fromJson(fixture.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("71485106642915ced70ff7e8c61357ee8974208fc70e82e91812c96ebcad07ac"),
          QStringLiteral("fixture drift"));
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject oracle = value.toObject();
    editor::MermaidRenderEntry entry;
    const auto scene = render(oracle.value(QStringLiteral("source")).toString(),
                              &entry);
    const bool error = oracle.value(QStringLiteral("status")).toString() ==
        QLatin1String("error");
    require(error ? !scene : bool(scene),
            oracle.value(QStringLiteral("id")).toString() +
                QStringLiteral(": status"));
    if (scene) compareReadyCase(oracle, *scene);
  }

  auto dagre = render(QStringLiteral(
      "%%{init:{\"layout\":\"dagre\",\"themeVariables\":{\"fontFamily\":\"Noto Sans\"}}}%%\n"
      "mindmap\n  root((Root))\n    A\n      B"));
  require(dagre && dagre->effectiveLayout == QLatin1String("dagre") &&
              !dagre->edges.isEmpty(), QStringLiteral("top-level dagre live"));
  auto unknown = render(QStringLiteral(
      "%%{init:{\"layout\":\"wat\",\"themeVariables\":{\"fontFamily\":\"Noto Sans\"}}}%%\n"
      "mindmap\n  root((Root))\n    A"));
  require(unknown && unknown->effectiveLayout == QLatin1String("cose-bilkent"),
          QStringLiteral("unknown layout fallback"));
  auto safe = render(QStringLiteral(
      "mindmap\n  root((Root))\n    a[\"pre <a href='https://example.org/docs'>Link</a> post\"]"));
  require(safe && safe->interactions.size() == 1 &&
              safe->interactions[0].href == QLatin1String("https://example.org/docs") &&
              safe->nodes[1].label.document.text == QLatin1String("pre Link post") &&
              safe->nodes[1].anchors.size() == 1,
          QStringLiteral("HTML anchor span projection"));
  auto blocked = render(QStringLiteral(
      "mindmap\n  root((Root))\n    a[\"<a href='javascript:alert(1)'>BadLink</a>\"]"));
  require(blocked && blocked->interactions.isEmpty() &&
              blocked->nodes[1].label.document.text == QLatin1String("BadLink"),
          QStringLiteral("unsafe anchor stripped"));
}
