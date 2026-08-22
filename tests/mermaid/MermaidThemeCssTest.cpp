#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/journey/JourneyScenePainter.h"
#include "mermaid/gitgraph/GitGraphScene.h"
#include "mermaid/c4/C4Scene.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/treemap/TreemapScene.h"
#include "mermaid/wardley/WardleyScene.h"
#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/railroad/RailroadScene.h"
#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/kanban/KanbanScene.h"
#include "mermaid/timeline/TimelineScene.h"
#include "mermaid/timeline/TimelineScenePainter.h"
#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/treeview/TreeViewScene.h"
#include "mermaid/venn/VennScene.h"
#include "mermaid/xychart/XYChartScene.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

bool browserDisplayed(const QJsonObject& element);

QByteArray fileSha(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
      ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
      : QByteArray();
}

QImage decodePngDataUrl(const QString& dataUrl) {
  QImage image;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(
                           dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

qreal alphaIou(const QImage& actual, const QImage& expected) {
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool left = actual.pixelColor(x, y).alpha() >= 32;
      const bool right = expected.pixelColor(x, y).alpha() >= 32;
      intersection += left && right;
      united += left || right;
    }
  return united ? qreal(intersection) / united : 1.0;
}

qreal rgbaSimilarity(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  int count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor left = actual.pixelColor(x, y);
      const QColor right = expected.pixelColor(x, y);
      if (left.alpha() < 32 && right.alpha() < 32) continue;
      ++count;
      const auto premultiplied = [](int channel, int alpha) {
        return channel * alpha / 255;
      };
      difference += std::abs(premultiplied(left.red(), left.alpha()) -
                             premultiplied(right.red(), right.alpha()));
      difference += std::abs(premultiplied(left.green(), left.alpha()) -
                             premultiplied(right.green(), right.alpha()));
      difference += std::abs(premultiplied(left.blue(), left.alpha()) -
                             premultiplied(right.blue(), right.alpha()));
      difference += std::abs(left.alpha() - right.alpha());
    }
  return count ? 1.0 - difference / (count * 4.0 * 255.0) : 1.0;
}

QColor averageRoi(const QImage& image, const QRect& roi) {
  int red = 0;
  int green = 0;
  int blue = 0;
  int alpha = 0;
  int count = 0;
  for (int y = roi.top(); y <= roi.bottom(); ++y)
    for (int x = roi.left(); x <= roi.right(); ++x) {
      if (!image.rect().contains(x, y)) continue;
      const QColor value = image.pixelColor(x, y);
      red += value.red();
      green += value.green();
      blue += value.blue();
      alpha += value.alpha();
      ++count;
    }
  return count ? QColor(red / count, green / count, blue / count, alpha / count)
               : QColor();
}

void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.51) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual).arg(expected));
}

QString computed(const QJsonObject& element, const char* property) {
  return element.value(QStringLiteral("computed")).toObject()
      .value(QLatin1String(property)).toString();
}

// The fixture records the ancestor-composed opacity product per element.
qreal elementOpacity(const QJsonObject& element) {
  return element.value(QStringLiteral("effectiveOpacity")).toDouble();
}

qreal elementFillOpacity(const QJsonObject& element) {
  return computed(element, "fillOpacity").toDouble();
}

void sameColor(const QString& actual, const QString& expected,
               const QString& path) {
  const QColor native = color::resolveSvgPaint(
      actual, color::SvgPaintKind::Fill, QColor(Qt::black)).color;
  const QColor browser = color::resolveSvgPaint(
      expected, color::SvgPaintKind::Fill, QColor(Qt::black)).color;
  require(native.rgba() == browser.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}

void sameColor(const QColor& actual, const QString& expected,
               const QString& path) {
  const QColor browser = color::resolveSvgPaint(
      expected, color::SvgPaintKind::Fill, QColor(Qt::black)).color;
  require(actual.rgba() == browser.rgba(),
          QStringLiteral("%1: %2 != %3")
              .arg(path, actual.name(QColor::HexArgb), expected));
}

QString semanticNodeId(const QString& owner) {
  const int flowchart = owner.lastIndexOf(QStringLiteral("-flowchart-"));
  if (flowchart < 0) return {};
  QString suffix = owner.mid(flowchart + 11);
  const int counter = suffix.lastIndexOf(QLatin1Char('-'));
  return counter < 0 ? suffix : suffix.left(counter);
}

const flowscene::FlowSceneNode* nodeById(const flowscene::FlowScene& scene,
                                         const QString& id) {
  for (const auto& node : scene.nodes)
    if (node.id == id) return &node;
  return nullptr;
}

const flowscene::FlowSceneCluster* clusterById(
    const flowscene::FlowScene& scene, const QString& id) {
  for (const auto& cluster : scene.clusters)
    if (cluster.id == id) return &cluster;
  return nullptr;
}

QVector<QJsonObject> visibleElements(const QJsonObject& fixture,
                                     const QString& selector) {
  QVector<QJsonObject> result;
  for (const QJsonValue& value : fixture.value(QStringLiteral("matches"))
                                     .toObject().value(selector).toArray()) {
    const QJsonObject element = value.toObject();
    if (element.value(QStringLiteral("bbox")).toObject()
            .value(QStringLiteral("width")).toDouble() > 0.0)
      result.append(element);
  }
  return result;
}

void compareFlow(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  editor::MermaidRenderCache cache;
  const editor::MermaidRenderEntry entry = cache.getSync(
      editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          id + QStringLiteral(": native render failed: ") + entry.errorMessage);
  const auto scene = std::dynamic_pointer_cast<const flowscene::FlowScene>(entry.scene);
  require(bool(scene), id + QStringLiteral(": expected FlowScene"));

  const QJsonObject expectedConfig =
      fixture.value(QStringLiteral("effectiveConfig")).toObject();
  const QJsonObject nativeConfig =
      mermaidRenderConfig(preprocessDiagram(source).config);
  const bool expectedThemeCss =
      !expectedConfig.value(QStringLiteral("themeCSS")).isNull();
  require(nativeConfig.contains(QStringLiteral("themeCSS")) == expectedThemeCss,
          id + QStringLiteral(": effective themeCSS presence mismatch"));
  if (expectedThemeCss)
    require(nativeConfig.value(QStringLiteral("themeCSS")) ==
                expectedConfig.value(QStringLiteral("themeCSS")),
            id + QStringLiteral(": effective themeCSS mismatch"));
  for (const QString& secure : {QStringLiteral("maxEdges"),
                                QStringLiteral("maxTextSize"),
                                QStringLiteral("securityLevel")})
    require(!nativeConfig.contains(secure),
            id + QStringLiteral(": secure source key survived: ") + secure);

  const QJsonObject client = fixture.value(QStringLiteral("client")).toObject();
  static QSize nativeBaseline;
  static QSize expectedBaseline;
  const QSize expected(std::lround(client.value(QStringLiteral("width")).toDouble()),
                       std::lround(client.value(QStringLiteral("height")).toDouble()));
  if (id == QLatin1String("flow-baseline")) {
    nativeBaseline = entry.naturalSize;
    expectedBaseline = expected;
  }
  require(nativeBaseline.isValid() && expectedBaseline.isValid(),
          QStringLiteral("flow-baseline must be the first Flow oracle"));
  if (id != QLatin1String("flow-directive-single-quote-invalid") &&
      id != QLatin1String("flow-inline-important-conflict")) {
    near(entry.naturalSize.width() - nativeBaseline.width(),
         expected.width() - expectedBaseline.width(), id + QStringLiteral("/client/width-delta"));
    near(entry.naturalSize.height() - nativeBaseline.height(),
         expected.height() - expectedBaseline.height(), id + QStringLiteral("/client/height-delta"));
  }

  const QVector<QJsonObject> rects = visibleElements(
      fixture, QStringLiteral(".node rect"));
  for (const QJsonObject& rect : rects) {
    const QString nodeId = semanticNodeId(
        rect.value(QStringLiteral("ownerNodeId")).toString());
    const flowscene::FlowSceneNode* node = nodeById(*scene, nodeId);
    require(node, id + QStringLiteral(": missing node ") + nodeId);
    sameColor(node->fill, computed(rect, "fill"),
              id + QStringLiteral("/") + nodeId + QStringLiteral("/fill"));
    sameColor(node->stroke, computed(rect, "stroke"),
              id + QStringLiteral("/") + nodeId + QStringLiteral("/stroke"));
    require(node->strokeWidth == computed(rect, "strokeWidth"),
            id + QStringLiteral("/") + nodeId + QStringLiteral("/strokeWidth: ") +
                node->strokeWidth + QStringLiteral(" != ") +
                computed(rect, "strokeWidth"));
  }

  const QJsonArray labels = fixture.value(QStringLiteral("matches")).toObject()
      .value(QStringLiteral(".node .label")).toArray();
  for (const QJsonValue& value : labels) {
    const QJsonObject label = value.toObject();
    const QString nodeId = semanticNodeId(
        label.value(QStringLiteral("ownerNodeId")).toString());
    const flowscene::FlowSceneNode* node = nodeById(*scene, nodeId);
    if (!node) continue;
    sameColor(node->label.color, computed(label, "color"),
              id + QStringLiteral("/") + nodeId + QStringLiteral("/color"));
    require(node->label.fontSize == computed(label, "fontSize"),
            id + QStringLiteral("/") + nodeId + QStringLiteral("/fontSize: ") +
                node->label.fontSize + QStringLiteral(" != ") +
                computed(label, "fontSize"));
    require(node->label.fontWeight.isEmpty() ||
                node->label.fontWeight == computed(label, "fontWeight"),
            id + QStringLiteral("/") + nodeId + QStringLiteral("/fontWeight"));
  }

  const QJsonArray groups = fixture.value(QStringLiteral("matches")).toObject()
      .value(QStringLiteral(".node")).toArray();
  for (const QJsonValue& value : groups) {
    const QJsonObject group = value.toObject();
    const QString nodeId = semanticNodeId(group.value(QStringLiteral("id")).toString());
    const flowscene::FlowSceneNode* node = nodeById(*scene, nodeId);
    if (node)
      require(node->visible == browserDisplayed(group),
              id + QStringLiteral("/") + nodeId + QStringLiteral("/display"));
  }

  const QVector<QJsonObject> clusterRects = visibleElements(
      fixture, QStringLiteral(".cluster rect"));
  if (!clusterRects.isEmpty() && !scene->clusters.isEmpty()) {
    sameColor(scene->clusters.first().fill, computed(clusterRects.first(), "fill"),
              id + QStringLiteral("/cluster/fill"));
    sameColor(scene->clusters.first().stroke, computed(clusterRects.first(), "stroke"),
              id + QStringLiteral("/cluster/stroke"));
    require(scene->clusters.first().strokeWidth ==
                computed(clusterRects.first(), "strokeWidth"),
            id + QStringLiteral("/cluster/strokeWidth"));
  }

  const QJsonArray edgePaths = fixture.value(QStringLiteral("matches")).toObject()
      .value(QStringLiteral(".edgePath path")).toArray();
  const qsizetype edgeCount = std::min<qsizetype>(scene->edges.size(), edgePaths.size());
  for (qsizetype index = 0; index < edgeCount; ++index) {
    const QJsonObject path = edgePaths.at(index).toObject();
    sameColor(scene->edges.at(index).stroke, computed(path, "stroke"),
              id + QStringLiteral("/edge/%1/stroke").arg(index));
    require(scene->edges.at(index).strokeWidth == computed(path, "strokeWidth"),
            id + QStringLiteral("/edge/%1/strokeWidth").arg(index));
  }
}

void compareSequence(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  editor::MermaidRenderCache cache;
  const editor::MermaidRenderEntry entry = cache.getSync(
      editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          id + QStringLiteral(": native render failed: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const sequence::SequenceScene>(entry.scene);
  require(bool(scene), id + QStringLiteral(": expected SequenceScene"));
  const QJsonObject client = fixture.value(QStringLiteral("client")).toObject();
  near(entry.naturalSize.width(), client.value(QStringLiteral("width")).toDouble(),
       id + QStringLiteral("/client/width"));
  near(entry.naturalSize.height(), client.value(QStringLiteral("height")).toDouble(),
       id + QStringLiteral("/client/height"));

  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonObject actor = matches.value(QStringLiteral(".actor"))
                                .toArray().first().toObject();
  sameColor(scene->style.actorFill, computed(actor, "fill"),
            id + QStringLiteral("/actor/fill"));
  sameColor(scene->style.actorStroke, computed(actor, "stroke"),
            id + QStringLiteral("/actor/stroke"));
  near(scene->style.actorStrokeWidth,
       computed(actor, "strokeWidth").chopped(2).toDouble(),
       id + QStringLiteral("/actor/strokeWidth"), 0.001);
  const QJsonObject message = matches.value(QStringLiteral(".messageText"))
                                  .toArray().first().toObject();
  sameColor(scene->style.signalTextColor, computed(message, "fill"),
            id + QStringLiteral("/message/fill"));
  near(scene->style.messageFontSize,
       computed(message, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/message/fontSize"), 0.001);
}

template <typename Scene>
std::shared_ptr<const Scene> renderScene(const QJsonObject& fixture,
                                         editor::MermaidRenderEntry& entry) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  editor::MermaidRenderCache cache;
  entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          id + QStringLiteral(": native render failed: ") + entry.errorMessage);
  const auto scene = std::dynamic_pointer_cast<const Scene>(entry.scene);
  require(bool(scene), id + QStringLiteral(": scene type mismatch"));
  return scene;
}

void compareRoundedClient(const QJsonObject& fixture,
                          const editor::MermaidRenderEntry& entry,
                          qreal tolerance = 0.51) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QJsonObject client = fixture.value(QStringLiteral("client")).toObject();
  near(entry.naturalSize.width(),
       std::round(client.value(QStringLiteral("width")).toDouble()),
       id + QStringLiteral("/client/width"), tolerance);
  near(entry.naturalSize.height(),
       std::round(client.value(QStringLiteral("height")).toDouble()),
       id + QStringLiteral("/client/height"), tolerance);
}

void comparePacket(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<packet::PacketScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  require(!scene->words.isEmpty() && !scene->words.first().blocks.isEmpty(),
          id + QStringLiteral(": missing packet blocks"));
  const QJsonObject rect = fixture.value(QStringLiteral("matches")).toObject()
                               .value(QStringLiteral("rect")).toArray().first().toObject();
  const auto& block = scene->words.first().blocks.first();
  sameColor(block.fill, computed(rect, "fill"), id + QStringLiteral("/rect/fill"));
  sameColor(block.stroke, computed(rect, "stroke"), id + QStringLiteral("/rect/stroke"));
  near(block.strokeWidth, computed(rect, "strokeWidth").chopped(2).toDouble(),
       id + QStringLiteral("/rect/strokeWidth"), 0.001);
  const QJsonObject text = fixture.value(QStringLiteral("matches")).toObject()
                               .value(QStringLiteral("text")).toArray().first().toObject();
  sameColor(block.labelText.fill, computed(text, "fill"),
            id + QStringLiteral("/text/fill"));
  near(block.labelText.fontSize, computed(text, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/text/fontSize"), 0.001);
}

void comparePie(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<pie::PieScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonObject slice = matches.value(QStringLiteral(".pieCircle"))
                                .toArray().first().toObject();
  require(!scene->slices.isEmpty(), id + QStringLiteral(": missing pie slices"));
  sameColor(scene->slices.first().fill, computed(slice, "fill"),
            id + QStringLiteral("/slice/fill"));
  sameColor(scene->style.sliceStrokeColor, computed(slice, "stroke"),
            id + QStringLiteral("/slice/stroke"));
  near(scene->style.sliceStrokeWidth,
       computed(slice, "strokeWidth").chopped(2).toDouble(),
       id + QStringLiteral("/slice/strokeWidth"), 0.001);
  const QJsonObject text = matches.value(QStringLiteral(".slice"))
                               .toArray().first().toObject();
  sameColor(scene->style.sectionTextColor, computed(text, "fill"),
            id + QStringLiteral("/text/fill"));
  near(scene->style.sectionFontSize,
       computed(text, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/text/fontSize"), 0.001);
}

void compareEr(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<er::ErScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject line = fixture.value(QStringLiteral("matches")).toObject()
                               .value(QStringLiteral(".relationshipLine"))
                               .toArray().first().toObject();
  sameColor(scene->style.relationshipColor, computed(line, "stroke"),
            id + QStringLiteral("/relationship/stroke"));
  near(scene->style.relationshipStrokeWidth,
       computed(line, "strokeWidth").chopped(2).toDouble(),
       id + QStringLiteral("/relationship/strokeWidth"), 0.001);
}

void compareClass(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<classdiagram::ClassScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  require(!scene->nodes.isEmpty(), id + QStringLiteral(": missing class nodes"));
  const QJsonObject label = fixture.value(QStringLiteral("matches")).toObject()
                                .value(QStringLiteral(".nodeLabel"))
                                .toArray().first().toObject();
  sameColor(scene->style.textColor, computed(label, "color"),
            id + QStringLiteral("/label/color"));
  near(scene->style.fontSize, computed(label, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/label/fontSize"), 0.001);
  // Class nodes are generic-node <path> handlers, so `.node rect` correctly
  // does not match; their built-in paint must remain unchanged.
  const QJsonObject path = fixture.value(QStringLiteral("matches")).toObject()
                               .value(QStringLiteral(".node path"))
                               .toArray().first().toObject();
  sameColor(scene->nodes.first().fill, computed(path, "fill"),
            id + QStringLiteral("/node/fill"));
  sameColor(scene->nodes.first().stroke, computed(path, "stroke"),
            id + QStringLiteral("/node/stroke"));
}

void compareState(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<state::StateScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  require(!scene->nodes.isEmpty(), id + QStringLiteral(": missing state nodes"));
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  // Per-node comparison in DOM order: `.node .label-container` picks the
  // shape rects only (`.node rect` also matches the 0×0 svg-background rect
  // each html label carries — 2 entries per node). The scene's per-element
  // themeCSS slots (shapeCss/labelCss) carry the computed values; empty
  // slots keep the resolved-theme defaults.
  const QJsonArray rects =
      matches.value(QStringLiteral(".node .label-container")).toArray();
  require(rects.size() == static_cast<int>(scene->nodes.size()),
          id + QStringLiteral("/node/count"));
  for (int index = 0; index < rects.size(); ++index) {
    const state::StateSceneNode& node = scene->nodes.at(index);
    const QJsonObject rect = rects.at(index).toObject();
    sameColor(node.shapeCss.fill.isEmpty() ? node.fill : node.shapeCss.fill,
              computed(rect, "fill"), id + QStringLiteral("/node%1/fill").arg(index));
    sameColor(node.shapeCss.stroke.isEmpty() ? node.stroke : node.shapeCss.stroke,
              computed(rect, "stroke"),
              id + QStringLiteral("/node%1/stroke").arg(index));
    const qreal strokeWidth = node.shapeCss.strokeWidthPx > 0.0
        ? node.shapeCss.strokeWidthPx : node.strokeWidth;
    near(strokeWidth, computed(rect, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/node%1/strokeWidth").arg(index), 0.001);
  }
  const QJsonArray labels = matches.value(QStringLiteral(".nodeLabel")).toArray();
  require(labels.size() == static_cast<int>(scene->nodes.size()),
          id + QStringLiteral("/label/count"));
  for (int index = 0; index < labels.size(); ++index) {
    const state::StateSceneNode& node = scene->nodes.at(index);
    const QJsonObject label = labels.at(index).toObject();
    sameColor(node.labelCss.color.isEmpty() ? node.textColor : node.labelCss.color,
              computed(label, "color"),
              id + QStringLiteral("/label%1/color").arg(index));
    const qreal fontSize = node.labelCss.fontSize > 0.0
        ? node.labelCss.fontSize : scene->style.fontSize;
    near(fontSize, computed(label, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label%1/fontSize").arg(index), 0.001);
  }
}

QString mindmapSemanticId(const QString& owner) {
  const int at = owner.lastIndexOf(QStringLiteral("-node_"));
  return at < 0 ? QString() : owner.mid(at + 6);
}

// Journey keeps CSS spellings in its scene fields (the painter applies the
// used-value contract), so the comparator resolves them through the same
// exported painter helpers.
void compareJourney(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<journey::JourneyScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const journey::JourneyPaintState root = journey::journeyRootSvgFill(
      scene->rootCss.fill.isEmpty() ? scene->style.textColor
                                    : scene->rootCss.fill);
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto elementFill = [&](const journey::JourneyElementCss& css,
                               const QString& base, const QColor& presentation) {
    return journey::journeyElementSvgFill(
        effective(css.fill, base), root,
        journey::JourneyPaintState{false, presentation});
  };
  const auto elementFillPaint = [&](const journey::JourneyElementCss& css,
                                    const QString& base,
                                    const journey::JourneyPaintState& presentation) {
    return journey::journeyElementSvgFill(effective(css.fill, base), root,
                                          presentation);
  };
  const auto strokeOf = [&](const journey::JourneyElementCss& css,
                            const QString& base, const QColor& presentation) {
    return journey::journeyLineStroke(effective(css.stroke, base), presentation);
  };
  const auto strokeWidthOf = [&](const journey::JourneyElementCss& css,
                                 const QString& base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, base),
        editor::pieCssLengthContext(scene->style.fontFamily,
                                    scene->style.fontSize),
        std::sqrt(scene->bounds.width() * scene->bounds.width() +
                  scene->bounds.height() * scene->bounds.height()) /
            std::sqrt(2.0));
  };
  const auto fontSizeOf = [&](const journey::JourneyElementCss& css,
                              qreal base) {
    return css.fontSize >= 0.0 ? css.fontSize : base;
  };
  const auto assertStroke = [&](const journey::JourneyPaintState& paint,
                                const QJsonObject& element, const QString& what) {
    if (paint.none) {
      require(computed(element, "stroke") == QLatin1String("none"),
              id + QStringLiteral("/") + what + QStringLiteral("/stroke-none"));
    } else {
      sameColor(paint.color, computed(element, "stroke"),
                id + QStringLiteral("/") + what + QStringLiteral("/stroke"));
    }
  };

  // Legend texts: one text.legend per actor, in legend display order.
  const QJsonArray legends = matches.value(QStringLiteral(".legend")).toArray();
  require(legends.size() == scene->actors.size(),
          id + QStringLiteral("/legend/count"));
  for (qsizetype i = 0; i < scene->actors.size(); ++i) {
    const QJsonObject legend = legends.at(i).toObject();
    const journey::JourneyActor& actor = scene->actors.at(i);
    const journey::JourneyPaintState paint = elementFill(
        actor.text, scene->style.textColor, QColor(Qt::black));
    require(!paint.none, id + QStringLiteral("/legend/%1/fill-none").arg(i));
    sameColor(paint.color, computed(legend, "fill"),
              id + QStringLiteral("/legend/%1/fill").arg(i));
    near(fontSizeOf(actor.text, scene->style.fontSize),
         computed(legend, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/legend/%1/fontSize").arg(i), 0.001);
    const QString weight = actor.text.fontWeight.isEmpty()
                               ? QStringLiteral("400")
                               : actor.text.fontWeight;
    require(weight == computed(legend, "fontWeight"),
            id + QStringLiteral("/legend/%1/fontWeight").arg(i));
    require(actor.text.visible == browserDisplayed(legend),
            id + QStringLiteral("/legend/%1/visible").arg(i));
  }

  // Faces, task lines, task rects, fallback texts and mouths: task order.
  const QJsonArray faces = matches.value(QStringLiteral(".face")).toArray();
  const QJsonArray taskLines =
      matches.value(QStringLiteral(".task-line")).toArray();
  const QJsonArray taskRects =
      matches.value(QStringLiteral("rect.task")).toArray();
  const QJsonArray taskTexts =
      matches.value(QStringLiteral("text.task")).toArray();
  const QJsonArray mouths = matches.value(QStringLiteral(".mouth")).toArray();
  require(faces.size() == scene->tasks.size() &&
              taskLines.size() == scene->tasks.size() &&
              taskRects.size() == scene->tasks.size() &&
              taskTexts.size() == scene->tasks.size() &&
              mouths.size() == scene->tasks.size(),
          id + QStringLiteral("/task/count"));
  for (qsizetype i = 0; i < scene->tasks.size(); ++i) {
    const journey::JourneyTaskGeometry& task = scene->tasks.at(i);
    const QColor presentation(task.presentationFill);
    const journey::JourneyPaintState faceFill = elementFill(
        task.face, scene->style.faceColor, QColor(Qt::black));
    require(!faceFill.none, id + QStringLiteral("/face/%1/fill").arg(i));
    sameColor(faceFill.color, computed(faces.at(i).toObject(), "fill"),
              id + QStringLiteral("/face/%1/fill").arg(i));
    assertStroke(strokeOf(task.face, QStringLiteral("#999999"),
                          QColor(QStringLiteral("#999999"))),
                 faces.at(i).toObject(),
                 QStringLiteral("face/%1").arg(i));
    near(strokeWidthOf(task.face, QStringLiteral("2px")),
         computed(faces.at(i).toObject(), "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/face/%1/strokeWidth").arg(i), 0.001);
    require(task.face.visible == browserDisplayed(faces.at(i).toObject()),
            id + QStringLiteral("/face/%1/visible").arg(i));

    assertStroke(strokeOf(task.line, scene->style.textColor,
                          QColor(QStringLiteral("#666666"))),
                 taskLines.at(i).toObject(),
                 QStringLiteral("task-line/%1").arg(i));
    near(strokeWidthOf(task.line, QStringLiteral("1px")),
         computed(taskLines.at(i).toObject(), "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/task-line/%1/strokeWidth").arg(i), 0.001);
    require(task.line.visible == browserDisplayed(taskLines.at(i).toObject()),
            id + QStringLiteral("/task-line/%1/visible").arg(i));

    const journey::JourneyPaintState rectFill =
        task.cssFillActive
            ? elementFill(task.box, task.fill, presentation)
            : elementFill(task.box, QString(), presentation);
    require(!rectFill.none, id + QStringLiteral("/task-rect/%1/fill").arg(i));
    sameColor(rectFill.color, computed(taskRects.at(i).toObject(), "fill"),
              id + QStringLiteral("/task-rect/%1/fill").arg(i));
    assertStroke(strokeOf(task.box, QStringLiteral("#666666"),
                          QColor(QStringLiteral("#666666"))),
                 taskRects.at(i).toObject(),
                 QStringLiteral("task-rect/%1").arg(i));
    near(strokeWidthOf(task.box, QStringLiteral("1px")),
         computed(taskRects.at(i).toObject(), "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/task-rect/%1/strokeWidth").arg(i), 0.001);
    require(task.box.visible == browserDisplayed(taskRects.at(i).toObject()),
            id + QStringLiteral("/task-rect/%1/visible").arg(i));

    // The switch fallback text: byFo drops the fill attribute, and the task
    // text's `task` class matches no base fill rule (only `.task-type-N`
    // does, which the rect carries), so the base is the inherited root fill.
    const journey::JourneyPaintState textFill = elementFillPaint(
        task.svgText, QString(), root);
    require(!textFill.none, id + QStringLiteral("/task-text/%1/fill").arg(i));
    sameColor(textFill.color, computed(taskTexts.at(i).toObject(), "fill"),
              id + QStringLiteral("/task-text/%1/fill").arg(i));

    assertStroke(strokeOf(task.mouth, QStringLiteral("#666666"),
                          QColor(QStringLiteral("#666666"))),
                 mouths.at(i).toObject(),
                 QStringLiteral("mouth/%1").arg(i));
    require(task.mouth.visible == browserDisplayed(mouths.at(i).toObject()),
            id + QStringLiteral("/mouth/%1/visible").arg(i));
    // The mouth path carries no fill attribute: it paints the root fill.
    sameColor(root.color, computed(mouths.at(i).toObject(), "fill"),
              id + QStringLiteral("/mouth/%1/fill").arg(i));
  }

  // Section rects: `.journey-section` also matches the html wrappers; keep
  // the rect-tagged records, in section order.
  QVector<QJsonObject> sectionRects;
  for (const QJsonValue& value :
       matches.value(QStringLiteral(".journey-section")).toArray()) {
    const QJsonObject element = value.toObject();
    if (element.value(QStringLiteral("tag")).toString() == QLatin1String("rect"))
      sectionRects.append(element);
  }
  require(sectionRects.size() == scene->sections.size(),
          id + QStringLiteral("/section/count"));
  for (qsizetype i = 0; i < scene->sections.size(); ++i) {
    const journey::JourneySectionGeometry& section = scene->sections.at(i);
    const QColor presentation(section.presentationFill);
    const journey::JourneyPaintState fill =
        section.cssFillActive
            ? elementFill(section.box, section.fill, presentation)
            : elementFill(section.box, QString(), presentation);
    require(!fill.none, id + QStringLiteral("/section/%1/fill").arg(i));
    sameColor(fill.color, computed(sectionRects.at(i), "fill"),
              id + QStringLiteral("/section/%1/fill").arg(i));
    require(section.box.visible == browserDisplayed(sectionRects.at(i)),
            id + QStringLiteral("/section/%1/visible").arg(i));
  }

  // Actor circles: `.actor-N` records the legend circle first, then the
  // person circles of that position in task scan order.
  const auto positionOf = [&](const QString& name) {
    if (name == QStringLiteral("__proto__") && scene->hasPrototypeActor)
      return scene->prototypeActor.position;
    for (const journey::JourneyActor& actor : scene->actors)
      if (actor.name == name) return actor.position;
    return -1;
  };
  int maxPosition = -1;
  for (const journey::JourneyActor& actor : scene->actors)
    maxPosition = std::max(maxPosition, actor.position);
  if (scene->hasPrototypeActor)
    maxPosition = std::max(maxPosition, scene->prototypeActor.position);
  for (int position = 0; position <= maxPosition; ++position) {
    const QJsonArray circles =
        matches.value(QStringLiteral(".actor-%1").arg(position)).toArray();
    require(!circles.isEmpty(),
            id + QStringLiteral("/actor-%1/missing").arg(position));
    const journey::JourneyActor* legend = nullptr;
    if (scene->hasPrototypeActor &&
        scene->prototypeActor.position == position)
      legend = &scene->prototypeActor;
    for (const journey::JourneyActor& actor : scene->actors)
      if (actor.position == position) legend = &actor;
    require(legend, id + QStringLiteral("/actor-%1/legend").arg(position));
    const journey::JourneyPaintState fill =
        elementFill(legend->circle, legend->color, QColor(legend->color));
    require(!fill.none, id + QStringLiteral("/actor-%1/fill").arg(position));
    sameColor(fill.color, computed(circles.at(0).toObject(), "fill"),
              id + QStringLiteral("/actor-%1/legend/fill").arg(position));
    assertStroke(strokeOf(legend->circle, QStringLiteral("#000000"),
                          QColor(Qt::black)),
                 circles.at(0).toObject(),
                 QStringLiteral("actor-%1/legend").arg(position));
    require(legend->circle.visible == browserDisplayed(circles.at(0).toObject()),
            id + QStringLiteral("/actor-%1/legend/visible").arg(position));
    qsizetype match = 1;
    for (const journey::JourneyTaskGeometry& task : scene->tasks) {
      for (qsizetype j = 0; j < task.people.size(); ++j) {
        if (positionOf(task.people.at(j)) != position) continue;
        require(match < circles.size(),
                id + QStringLiteral("/actor-%1/person/count").arg(position));
        const QJsonObject circle = circles.at(match++).toObject();
        const QString personColor = [&]() {
          if (task.people.at(j) == QStringLiteral("__proto__") &&
              scene->hasPrototypeActor)
            return scene->prototypeActor.color;
          for (const journey::JourneyActor& actor : scene->actors)
            if (actor.name == task.people.at(j)) return actor.color;
          return QString();
        }();
        const journey::JourneyElementCss& css =
            task.peopleCircles.value(j, journey::JourneyElementCss{});
        const journey::JourneyPaintState personFill =
            elementFill(css, personColor, QColor(personColor));
        require(!personFill.none,
                id + QStringLiteral("/actor-%1/person/fill").arg(position));
        sameColor(personFill.color, computed(circle, "fill"),
                  id + QStringLiteral("/actor-%1/person/fill").arg(position));
        assertStroke(strokeOf(css, QStringLiteral("#000000"), QColor(Qt::black)),
                     circle,
                     QStringLiteral("actor-%1/person").arg(position));
        near(strokeWidthOf(css, QStringLiteral("1px")),
             computed(circle, "strokeWidth").chopped(2).toDouble(),
             id + QStringLiteral("/actor-%1/person/strokeWidth").arg(position),
             0.001);
        require(css.visible == browserDisplayed(circle),
                id + QStringLiteral("/actor-%1/person/visible").arg(position));
      }
    }
    require(match == circles.size(),
            id + QStringLiteral("/actor-%1/count %1 != %2")
                    .arg(position).arg(match).arg(circles.size()));
  }

  // foreignObject div.label records, in document order (sections and tasks
  // interleaved the way drawTasks emits them).
  QVector<const journey::JourneyElementCss*> labels;
  QString lastSection;
  qsizetype sectionIndex = 0;
  for (const journey::JourneyTaskGeometry& task : scene->tasks) {
    if (task.section != lastSection) {
      labels.append(&scene->sections.at(sectionIndex).label);
      lastSection = task.section;
      ++sectionIndex;
    }
    labels.append(&task.label);
  }
  const QJsonArray labelRecords = matches.value(QStringLiteral(".label")).toArray();
  require(labelRecords.size() == labels.size(),
          id + QStringLiteral("/label/count"));
  for (qsizetype i = 0; i < labels.size(); ++i) {
    const journey::JourneyElementCss* css = labels.at(i);
    const QColor color(effective(css->color, scene->style.textColor));
    sameColor(color, computed(labelRecords.at(i).toObject(), "color"),
              id + QStringLiteral("/label/%1/color").arg(i));
    near(css->fontSize >= 0.0 ? css->fontSize : scene->style.fontSize,
         computed(labelRecords.at(i).toObject(), "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/fontSize").arg(i), 0.001);
  }

  // Title and axis: the classless svg-level text/line records.
  for (const QJsonValue& value : matches.value(QStringLiteral("text")).toArray()) {
    const QJsonObject element = value.toObject();
    if (!element.value(QStringLiteral("class")).toString().isEmpty()) continue;
    if (element.value(QStringLiteral("parentTag")).toString() != QLatin1String("svg"))
      continue;
    const QString base = scene->config.titleColor.isEmpty()
                             ? scene->style.textColor
                             : scene->config.titleColor;
    const journey::JourneyPaintState paint =
        elementFill(scene->titleCss, base, QColor(Qt::black));
    require(!paint.none, id + QStringLiteral("/title/fill"));
    sameColor(paint.color, computed(element, "fill"),
              id + QStringLiteral("/title/fill"));
    near(scene->titleCss.fontSize >= 0.0 ? scene->titleCss.fontSize
                                         : scene->config.titleFontSize,
         computed(element, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/title/fontSize"), 0.51);
    require(scene->titleCss.visible == browserDisplayed(element),
            id + QStringLiteral("/title/visible"));
  }
  for (const QJsonValue& value : matches.value(QStringLiteral("line")).toArray()) {
    const QJsonObject element = value.toObject();
    if (!element.value(QStringLiteral("class")).toString().isEmpty()) continue;
    if (element.value(QStringLiteral("parentTag")).toString() != QLatin1String("svg"))
      continue;
    assertStroke(strokeOf(scene->axisCss, scene->style.textColor, QColor(Qt::black)),
                 element, QStringLiteral("axis"));
    near(strokeWidthOf(scene->axisCss, QStringLiteral("4px")),
         computed(element, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/axis/strokeWidth"), 0.001);
    require(scene->axisCss.visible == browserDisplayed(element),
            id + QStringLiteral("/axis/visible"));
  }
}

void compareGitGraph(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<gitgraph::GitGraphScene>(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QColor root = color::resolveSvgPaint(
      scene->style.textColor, color::SvgPaintKind::Fill, QColor(Qt::black))
                            .color;
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto strokeWidthOf = [&](const gitgraph::GitGraphElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth,
                  QString::number(base) + QStringLiteral("px")),
        editor::pieCssLengthContext(scene->style.fontFamily,
                                    scene->style.fontSize),
        std::hypot(scene->bounds.width(), scene->bounds.height()) /
            std::sqrt(2.0));
  };
  const auto byRole = [&scene](const char* role) {
    QVector<const gitgraph::GitGraphPrimitive*> out;
    for (const auto& primitive : scene->primitives)
      if (primitive.role == QLatin1String(role)) out.append(&primitive);
    return out;
  };
  // The setupGraphViewbox getBBox covers the content without padding.
  const QJsonObject bbox = fixture.value(QStringLiteral("bbox")).toObject();
  near(scene->contentBounds.width(),
       bbox.value(QStringLiteral("width")).toDouble(),
       id + QStringLiteral("/bbox/width"));
  near(scene->contentBounds.height(),
       bbox.value(QStringLiteral("height")).toDouble(),
       id + QStringLiteral("/bbox/height"));

  // Branch spines: `.branch` lists line.branch.branchN in branch order.
  const auto lines = byRole("branch");
  const QJsonArray branchEls = matches.value(QStringLiteral(".branch")).toArray();
  require(branchEls.size() == lines.size(), id + QStringLiteral("/branch/count"));
  for (qsizetype i = 0; i < lines.size(); ++i) {
    const auto& primitive = *lines.at(i);
    const QJsonObject el = branchEls.at(i).toObject();
    sameColor(effective(primitive.css.stroke, primitive.stroke),
              computed(el, "stroke"),
              id + QStringLiteral("/branch/%1/stroke").arg(i));
    near(strokeWidthOf(primitive.css, primitive.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/branch/%1/strokeWidth").arg(i), 0.001);
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/branch/%1/visible").arg(i));
  }

  // Branch label backgrounds: `.branchLabelBkg` in the same order.
  const auto bkgs = byRole("branch-label-background");
  const QJsonArray bkgEls =
      matches.value(QStringLiteral(".branchLabelBkg")).toArray();
  require(bkgEls.size() == bkgs.size(),
          id + QStringLiteral("/branch-bkg/count"));
  for (qsizetype i = 0; i < bkgs.size(); ++i) {
    const auto& primitive = *bkgs.at(i);
    const QJsonObject el = bkgEls.at(i).toObject();
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"),
              id + QStringLiteral("/branch-bkg/%1/fill").arg(i));
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/branch-bkg/%1/visible").arg(i));
  }

  // Texts: the `text` selector lists branch labels, commit labels, tag
  // labels and the title in document order — the same order as the scene's
  // Text primitives after the stable paint-phase sort.
  QVector<const gitgraph::GitGraphPrimitive*> texts;
  for (const auto& primitive : scene->primitives)
    if (primitive.kind == gitgraph::PrimitiveKind::Text) texts.append(&primitive);
  const QJsonArray textEls = matches.value(QStringLiteral("text")).toArray();
  require(textEls.size() == texts.size(), id + QStringLiteral("/text/count"));
  for (qsizetype i = 0; i < texts.size(); ++i) {
    const auto& primitive = *texts.at(i);
    const QJsonObject el = textEls.at(i).toObject();
    // The text element inherits its group's fill unless it matches an own
    // rule (e.g. `text { fill }`), which the resolved css slot captures.
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"), id + QStringLiteral("/text/%1/fill").arg(i));
    const qreal fontSize = primitive.css.fontSize >= 0.0
        ? primitive.css.fontSize : primitive.fontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/text/%1/fontSize").arg(i), 0.001);
    const QString weight = !primitive.css.fontWeight.isEmpty()
        ? primitive.css.fontWeight
        : primitive.bold ? QStringLiteral("600")
                         : QStringLiteral("400");
    require(weight == computed(el, "fontWeight"),
            id + QStringLiteral("/text/%1/fontWeight").arg(i));
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/text/%1/visible").arg(i));
  }

  // Commit bullets: `.commit-bullets circle` in draw order — the empty
  // measurement-pass group contributes no children; tag holes live in
  // g.commit-labels and are compared separately.
  QVector<const gitgraph::GitGraphPrimitive*> circles;
  for (const auto& primitive : scene->primitives)
    if (primitive.kind == gitgraph::PrimitiveKind::Circle &&
        (primitive.role == QLatin1String("commit") ||
         primitive.role == QLatin1String("commit-merge") ||
         primitive.role == QLatin1String("cherry-dot")))
      circles.append(&primitive);
  const QJsonArray circleEls = matches.value(QStringLiteral("circle")).toArray();
  QJsonArray bulletCircleEls;
  for (const QJsonValue& value : circleEls) {
    const QJsonObject el = value.toObject();
    // The generic circle list also carries the two tag holes, which live in
    // g.commit-labels and are compared with the tag block below.
    if (!el.value(QStringLiteral("class")).toString()
             .contains(QLatin1String("tag-hole")))
      bulletCircleEls.append(value);
  }
  require(bulletCircleEls.size() == circles.size(),
          id + QStringLiteral("/circle/count"));
  for (qsizetype i = 0; i < circles.size(); ++i) {
    const auto& primitive = *circles.at(i);
    const QJsonObject el = bulletCircleEls.at(i).toObject();
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"), id + QStringLiteral("/circle/%1/fill").arg(i));
    sameColor(effective(primitive.css.stroke, primitive.stroke),
              computed(el, "stroke"),
              id + QStringLiteral("/circle/%1/stroke").arg(i));
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/circle/%1/visible").arg(i));
  }

  // Commit label backgrounds: opacity compares the ancestor-composed
  // effective value against the fixture's walked product.
  const auto labelBg = byRole("commit-label-background");
  const QJsonArray labelBgEls =
      matches.value(QStringLiteral(".commit-label-bkg")).toArray();
  require(labelBgEls.size() == labelBg.size(),
          id + QStringLiteral("/label-bkg/count"));
  for (qsizetype i = 0; i < labelBg.size(); ++i) {
    const auto& primitive = *labelBg.at(i);
    const QJsonObject el = labelBgEls.at(i).toObject();
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"),
              id + QStringLiteral("/label-bkg/%1/fill").arg(i));
    const qreal opacity = primitive.css.opacity >= 0.0
        ? primitive.css.opacity : primitive.opacity;
    near(opacity, el.value(QStringLiteral("effectiveOpacity")).toDouble(),
         id + QStringLiteral("/label-bkg/%1/opacity").arg(i), 0.001);
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/label-bkg/%1/visible").arg(i));
  }

  // Commit symbol variants.
  const auto compareRole = [&](const char* role, const char* selector) {
    const auto primitives = byRole(role);
    const QJsonArray els = matches.value(QLatin1String(selector)).toArray();
    require(els.size() == primitives.size(),
            id + QStringLiteral("/%1/count").arg(QLatin1String(role)));
    for (qsizetype i = 0; i < primitives.size(); ++i) {
      const auto& primitive = *primitives.at(i);
      const QJsonObject el = els.at(i).toObject();
      if (primitive.kind != gitgraph::PrimitiveKind::Path)
        sameColor(effective(primitive.css.fill, primitive.fill),
                  computed(el, "fill"),
                  id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(role)).arg(i));
      sameColor(effective(primitive.css.stroke, primitive.stroke),
                computed(el, "stroke"),
                id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(role)).arg(i));
      near(strokeWidthOf(primitive.css, primitive.strokeWidth),
           computed(el, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/strokeWidth").arg(QLatin1String(role)).arg(i),
           0.001);
      require(primitive.css.visible == browserDisplayed(el),
              id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(role)).arg(i));
    }
  };
  compareRole("commit-merge", ".commit-merge");
  compareRole("commit-reverse", ".commit-reverse");
  compareRole("commit-highlight-outer", ".commit-highlight-outer");
  compareRole("commit-highlight-inner", ".commit-highlight-inner");
  compareRole("tag-background", ".tag-label-bkg");
  compareRole("tag-hole", ".tag-hole");

  // Arrows: `.arrow` in commit/parent draw order.
  const auto arrows = byRole("arrow");
  const QJsonArray arrowEls = matches.value(QStringLiteral(".arrow")).toArray();
  require(arrowEls.size() == arrows.size(), id + QStringLiteral("/arrow/count"));
  for (qsizetype i = 0; i < arrows.size(); ++i) {
    const auto& primitive = *arrows.at(i);
    const QJsonObject el = arrowEls.at(i).toObject();
    sameColor(effective(primitive.css.stroke, primitive.stroke),
              computed(el, "stroke"),
              id + QStringLiteral("/arrow/%1/stroke").arg(i));
    near(strokeWidthOf(primitive.css, primitive.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/arrow/%1/strokeWidth").arg(i), 0.001);
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"),
              id + QStringLiteral("/arrow/%1/fill").arg(i));
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/arrow/%1/visible").arg(i));
  }

  near(entry.naturalSize.width(),
       std::ceil(fixture.value(QStringLiteral("client")).toObject()
                     .value(QStringLiteral("width")).toDouble()),
       id + QStringLiteral("/client/width"));
  near(entry.naturalSize.height(),
       std::ceil(fixture.value(QStringLiteral("client")).toObject()
                     .value(QStringLiteral("height")).toDouble()),
       id + QStringLiteral("/client/height"));
}

void compareC4(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<c4::C4Scene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto strokeWidthOf = [&](const c4::C4ElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth,
                  QString::number(base) + QStringLiteral("px")),
        editor::pieCssLengthContext(scene->style.rootFontFamily,
                                    scene->style.rootFontSize),
        std::hypot(scene->bounds.width(), scene->bounds.height()) /
            std::sqrt(2.0));
  };
  // c4 measures through the config fonts, so layout is CSS-independent and
  // geometry locks come from the per-element bboxes (the root svg bbox is
  // skipped: the title ink pokes above the JS-computed content box).
  const auto effectiveOpacityOf = [](const c4::C4ElementCss& css) {
    return css.opacity >= 0.0 ? css.opacity : 1.0;
  };
  // Own display:none collapses the Chrome bbox to a 0x0 rect at the origin;
  // ancestor-only hiding keeps the geometry.
  const auto compareBox = [&](const char* part, qsizetype index,
                              const c4::C4ElementCss& css,
                              const QRectF& geometry,
                              const QJsonObject& el) {
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    const QString label = QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(index);
    const qreal expectedX = css.measures ? geometry.x() : 0.0;
    const qreal expectedY = css.measures ? geometry.y() : 0.0;
    const qreal expectedW = css.measures ? geometry.width() : 0.0;
    const qreal expectedH = css.measures ? geometry.height() : 0.0;
    near(expectedX, bbox.value(QStringLiteral("x")).toDouble(),
         id + label + QStringLiteral("x"));
    near(expectedY, bbox.value(QStringLiteral("y")).toDouble(),
         id + label + QStringLiteral("y"));
    near(expectedW, bbox.value(QStringLiteral("width")).toDouble(),
         id + label + QStringLiteral("width"));
    near(expectedH, bbox.value(QStringLiteral("height")).toDouble(),
         id + label + QStringLiteral("height"));
  };
  const auto comparePaint = [&](const char* part, const c4::C4Primitive& p,
                                const QJsonObject& el) {
    sameColor(effective(p.css.fill, p.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/fill").arg(QLatin1String(part)));
    sameColor(effective(p.css.stroke, p.stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/stroke").arg(QLatin1String(part)));
    near(strokeWidthOf(p.css, p.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/%1/strokeWidth").arg(QLatin1String(part)),
         0.001);
    require(p.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/visible").arg(QLatin1String(part)));
    near(effectiveOpacityOf(p.css), elementOpacity(el),
         id + QStringLiteral("/%1/opacity").arg(QLatin1String(part)), 0.01);
  };

  // The base `.person` rule is dead: shape groups are hardcoded person-man.
  require(matches.value(QStringLiteral(".person")).toArray().size() == 0,
          id + QStringLiteral("/person-dead-rule"));
  qsizetype shapeGroups = 0;
  for (const auto& p : scene->primitives)
    if (p.role == QLatin1String("shape")) ++shapeGroups;
  require(matches.value(QStringLiteral(".person-man")).toArray().size() ==
              shapeGroups,
          id + QStringLiteral("/person-man/count"));

  // Rects in document order (shapes then boundary) == Rect primitives.
  QVector<const c4::C4Primitive*> rects;
  for (const auto& p : scene->primitives)
    if (p.kind == c4::C4PrimitiveKind::Rect) rects.append(&p);
  const QJsonArray rectEls = matches.value(QStringLiteral("rect")).toArray();
  require(rectEls.size() == rects.size(), id + QStringLiteral("/rect/count"));
  for (qsizetype i = 0; i < rects.size(); ++i) {
    const auto& primitive = *rects.at(i);
    const QJsonObject el = rectEls.at(i).toObject();
    comparePaint("rect", primitive, el);
    compareBox("rect", i, primitive.css, primitive.rect, el);
  }

  // Database/queue body+lip paths: the `path` elements inside person-man.
  QVector<const c4::C4Primitive*> shapePaths;
  for (const auto& p : scene->primitives)
    if (p.kind == c4::C4PrimitiveKind::Path) shapePaths.append(&p);
  // (relation paths are appended later and filtered out below)
  QVector<QJsonObject> personPathEls;
  for (const QJsonValue& value : matches.value(QStringLiteral("path")).toArray()) {
    const QJsonObject el = value.toObject();
    if (el.value(QStringLiteral("parentClass")).toString() ==
        QLatin1String("person-man"))
      personPathEls.append(el);
  }
  qsizetype expectedShapePaths = 0;
  for (const auto* p : shapePaths)
    if (p->role == QLatin1String("shape") ||
        p->role == QLatin1String("shape-detail"))
      ++expectedShapePaths;
  require(personPathEls.size() == expectedShapePaths,
          id + QStringLiteral("/shape-path/count"));
  qsizetype pathIndex = 0;
  for (const auto* p : shapePaths) {
    if (p->role != QLatin1String("shape") &&
        p->role != QLatin1String("shape-detail"))
      continue;
    comparePaint("shape-path", *p, personPathEls.at(pathIndex));
    ++pathIndex;
  }

  // Relation edge: first relation renders a line, later ones quad paths.
  QVector<const c4::C4Primitive*> relations;
  for (const auto& p : scene->primitives)
    if (p.role == QLatin1String("relation")) relations.append(&p);
  const QJsonArray lineEls = matches.value(QStringLiteral("line")).toArray();
  qsizetype lineCount = 0;
  for (const auto& p : relations)
    if (p->kind == c4::C4PrimitiveKind::Line) ++lineCount;
  require(lineEls.size() == lineCount, id + QStringLiteral("/line/count"));
  qsizetype lineIndex = 0;
  for (const auto& p : relations) {
    if (p->kind != c4::C4PrimitiveKind::Line) continue;
    const QJsonObject el = lineEls.at(lineIndex).toObject();
    comparePaint("line", *p, el);
    compareBox("line", lineIndex, p->css,
               QRectF(QPointF(std::min(p->line.x1(), p->line.x2()),
                              std::min(p->line.y1(), p->line.y2())),
                      QSizeF(std::abs(p->line.dx()), std::abs(p->line.dy()))),
               el);
    ++lineIndex;
  }
  // Arrowhead/arrowend marker paths carry no fill attribute and inherit the
  // root fill (textColor), not the line stroke.
  for (const QJsonValue& value : matches.value(QStringLiteral("path")).toArray()) {
    const QJsonObject el = value.toObject();
    if (el.value(QStringLiteral("parentTag")).toString() != QLatin1String("marker"))
      continue;
    if (!relations.isEmpty()) {
      sameColor(relations.first()->markerFill, computed(el, "fill"),
                id + QStringLiteral("/marker/fill"));
      break;
    }
  }

  // Person image.
  QVector<const c4::C4Primitive*> images;
  for (const auto& p : scene->primitives)
    if (p.kind == c4::C4PrimitiveKind::Image) images.append(&p);
  const QJsonArray imageEls =
      matches.value(QStringLiteral("image")).toArray();
  require(imageEls.size() == images.size(),
          id + QStringLiteral("/image/count"));
  for (qsizetype i = 0; i < images.size(); ++i) {
    const auto& primitive = *images.at(i);
    const QJsonObject el = imageEls.at(i).toObject();
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/image/%1/visible").arg(i));
    near(effectiveOpacityOf(primitive.css), elementOpacity(el),
         id + QStringLiteral("/image/%1/opacity").arg(i), 0.01);
    compareBox("image", i, primitive.css, primitive.rect, el);
  }

  // Texts in document order — stereotype, label, description per shape,
  // boundary label/type, relation label/technology, title — matching the
  // builder's emission order. Font overrides only ever repaint (inline
  // styles beat non-important rules; !important beats them).
  QVector<const c4::C4Primitive*> texts;
  for (const auto& p : scene->primitives)
    if (p.kind == c4::C4PrimitiveKind::Text) texts.append(&p);
  const QJsonArray textEls = matches.value(QStringLiteral("text")).toArray();
  require(textEls.size() == texts.size(), id + QStringLiteral("/text/count"));
  const auto weightNumber = [](const QString& value) {
    const QString lower = value.trimmed().toLower();
    if (lower == QLatin1String("bold") || lower == QLatin1String("bolder"))
      return QStringLiteral("700");
    bool ok = false;
    const int number = lower.toInt(&ok);
    if (ok && number >= 700) return QStringLiteral("700");
    if (ok && number >= 100 && number <= 600 && number % 100 == 0)
      return QString::number(number);
    return QStringLiteral("400");
  };
  for (qsizetype i = 0; i < texts.size(); ++i) {
    const auto& primitive = *texts.at(i);
    const QJsonObject el = textEls.at(i).toObject();
    require(primitive.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/text/%1/content").arg(i));
    sameColor(effective(primitive.css.fill, primitive.fill),
              computed(el, "fill"), id + QStringLiteral("/text/%1/fill").arg(i));
    const qreal fontSize = primitive.css.fontSize >= 0.0
                               ? primitive.css.fontSize
                               : primitive.fontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/text/%1/fontSize").arg(i), 0.001);
    const QString weightValue = !primitive.css.fontWeight.trimmed().isEmpty()
                                    ? primitive.css.fontWeight
                                    : primitive.fontWeight;
    require(weightNumber(weightValue) == computed(el, "fontWeight"),
            id + QStringLiteral("/text/%1/fontWeight").arg(i));
    const QString fontStyle = !primitive.css.fontStyle.trimmed().isEmpty()
                                  ? primitive.css.fontStyle.trimmed().toLower()
                                  : (primitive.italic
                                         ? QStringLiteral("italic")
                                         : QStringLiteral("normal"));
    require(fontStyle == computed(el, "fontStyle"),
            id + QStringLiteral("/text/%1/fontStyle").arg(i));
    require(primitive.css.visible == browserDisplayed(el),
            id + QStringLiteral("/text/%1/visible").arg(i));
    near(effectiveOpacityOf(primitive.css), elementOpacity(el),
         id + QStringLiteral("/text/%1/opacity").arg(i), 0.01);
  }
}

void compareGantt(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<gantt::GanttScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const qreal diagonal = std::hypot(scene->bounds.width(), scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto lengths = editor::pieCssLengthContext(
      scene->style.fontFamily, scene->style.rootFontSize);
  const auto strokeWidthOf = [&](const gantt::GanttElementCss& css, qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, QString::number(base) + QStringLiteral("px")),
        lengths, diagonal);
  };
  const auto opacityOf = [](const gantt::GanttElementCss& css, qreal base) {
    return css.opacity >= 0.0 ? css.opacity : base;
  };
  // Own display:none collapses the Chrome bbox to 0x0; ancestor-only hiding
  // keeps the geometry (gantt's viewBox is fixed, so nothing relayouts).
  const auto compareBox = [&](const char* part, qsizetype index,
                              const gantt::GanttElementCss& css,
                              const QRectF& geometry, const QJsonObject& el) {
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    const QString label = QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(index);
    const qreal factor = css.measures ? 1.0 : 0.0;
    near(geometry.x() * factor, bbox.value(QStringLiteral("x")).toDouble(),
         id + label + QStringLiteral("x"));
    near(geometry.y() * factor, bbox.value(QStringLiteral("y")).toDouble(),
         id + label + QStringLiteral("y"));
    near(geometry.width() * factor, bbox.value(QStringLiteral("width")).toDouble(),
         id + label + QStringLiteral("width"));
    near(geometry.height() * factor, bbox.value(QStringLiteral("height")).toDouble(),
         id + label + QStringLiteral("height"));
  };
  const auto compareRectPaint = [&](const char* part, qsizetype index,
                                    const gantt::GanttRectGeometry& rect,
                                    const QJsonObject& el) {
    sameColor(effective(rect.css.fill, rect.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    sameColor(effective(rect.css.stroke, rect.stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(part)).arg(index));
    near(strokeWidthOf(rect.css, rect.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/strokeWidth").arg(QLatin1String(part)).arg(index),
         0.001);
    require(rect.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(rect.css, rect.opacity), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };
  const auto compareLinePaint = [&](const char* part, qsizetype index,
                                    const gantt::GanttLineGeometry& line,
                                    const QJsonObject& el) {
    sameColor(effective(line.css.stroke, line.stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(part)).arg(index));
    near(strokeWidthOf(line.css, line.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/strokeWidth").arg(QLatin1String(part)).arg(index),
         0.001);
    require(line.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(line.css, line.opacity), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };

  // The todayMarker statement is off in every fixture case, and no vertical
  // markers exist: both class surfaces stay empty.
  require(matches.value(QStringLiteral(".today")).toArray().size() == 0,
          id + QStringLiteral("/today/off"));
  require(matches.value(QStringLiteral(".vert")).toArray().size() == 0,
          id + QStringLiteral("/vert/absent"));
  require(matches.value(QStringLiteral(".grid .tick")).toArray().size() ==
              scene->gridLines.size(),
          id + QStringLiteral("/tick/count"));

  // rects in document order: excludes, section rows, tasks.
  const QJsonArray rectEls = matches.value(QStringLiteral("rect")).toArray();
  require(rectEls.size() == scene->excludes.size() + scene->sections.size() +
                                scene->tasks.size(),
          id + QStringLiteral("/rect/count"));
  const auto classTokens = [](const QString& value) {
    return value.split(QRegularExpression(QStringLiteral("\\s+")),
                       Qt::SkipEmptyParts);
  };
  qsizetype rectIndex = 0;
  for (qsizetype i = 0; i < scene->excludes.size(); ++i, ++rectIndex) {
    const QJsonObject el = rectEls.at(rectIndex).toObject();
    require(classTokens(el.value(QStringLiteral("class")).toString()) ==
                classTokens(scene->excludes.at(i).cssClass),
            id + QStringLiteral("/exclude/%1/class").arg(i));
    compareRectPaint("exclude", i, scene->excludes.at(i), el);
    compareBox("exclude", i, scene->excludes.at(i).css,
               scene->excludes.at(i).rect, el);
  }
  for (qsizetype i = 0; i < scene->sections.size(); ++i, ++rectIndex) {
    const QJsonObject el = rectEls.at(rectIndex).toObject();
    require(classTokens(el.value(QStringLiteral("class")).toString()) ==
                classTokens(scene->sections.at(i).cssClass),
            id + QStringLiteral("/section/%1/class").arg(i));
    compareRectPaint("section", i, scene->sections.at(i), el);
    compareBox("section", i, scene->sections.at(i).css,
               scene->sections.at(i).rect, el);
  }
  for (qsizetype i = 0; i < scene->tasks.size(); ++i, ++rectIndex) {
    const QJsonObject el = rectEls.at(rectIndex).toObject();
    require(classTokens(el.value(QStringLiteral("class")).toString()) ==
                classTokens(scene->tasks.at(i).cssClass),
            id + QStringLiteral("/task/%1/class").arg(i));
    compareRectPaint("task", i, scene->tasks.at(i), el);
    compareBox("task", i, scene->tasks.at(i).css, scene->tasks.at(i).rect, el);
  }

  // grid tick lines: the local y2 attr carries the tick extent; the tick
  // group transform carries the position.
  const QJsonArray lineEls = matches.value(QStringLiteral("line")).toArray();
  require(lineEls.size() == scene->gridLines.size(),
          id + QStringLiteral("/line/count"));
  static const QRegularExpression translate(
      QStringLiteral(R"(^translate\(([^,\s]+)[,\s]+[^\)]*\)$)"));
  for (qsizetype i = 0; i < scene->gridLines.size(); ++i) {
    const gantt::GanttLineGeometry& line = scene->gridLines.at(i);
    const QJsonObject el = lineEls.at(i).toObject();
    compareLinePaint("line", i, line, el);
    near(line.line.y2() - line.line.y1(),
         el.value(QStringLiteral("attributes")).toObject()
             .value(QStringLiteral("y2")).toString().toDouble(),
         id + QStringLiteral("/line/%1/y2").arg(i), 0.001);
    const QJsonObject tickEl =
        matches.value(QStringLiteral(".grid .tick")).toArray().at(i).toObject();
    const auto match = translate.match(tickEl.value(QStringLiteral("attributes"))
                                           .toObject()
                                           .value(QStringLiteral("transform"))
                                           .toString());
    require(match.hasMatch(), id + QStringLiteral("/tick/%1/transform").arg(i));
    near(line.line.x1() - scene->config.leftPadding,
         match.captured(1).toDouble(),
         id + QStringLiteral("/tick/%1/x").arg(i), 0.001);
  }

  // texts in document order: grid labels, task labels, section titles, title.
  const QJsonArray textEls = matches.value(QStringLiteral("text")).toArray();
  require(textEls.size() == scene->gridLabels.size() + scene->taskLabels.size() +
                                scene->sectionLabels.size() + 1,
          id + QStringLiteral("/text/count"));
  qsizetype textIndex = 0;
  // Chrome text ink metrics (getBBox width / getComputedTextLength) drift
  // from QFontMetricsF advances by up to ~1px at themeCSS font sizes.
  const auto compareTextPaint = [&](const char* part, qsizetype index,
                                    const gantt::GanttTextGeometry& text,
                                    const QJsonObject& el) {
    sameColor(effective(text.css.fill, text.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    const qreal fontSize = text.css.fontSize >= 0.0 ? text.css.fontSize
                                                    : text.fontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/fontSize").arg(QLatin1String(part)).arg(index),
         0.001);
    const QString fontStyle = !text.css.fontStyle.trimmed().isEmpty()
                                  ? text.css.fontStyle.trimmed().toLower()
                                  : (text.italic ? QStringLiteral("italic")
                                                 : QStringLiteral("normal"));
    require(fontStyle == computed(el, "fontStyle"),
            id + QStringLiteral("/%1/%2/fontStyle").arg(QLatin1String(part)).arg(index));
    require(text.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(text.css, text.opacity), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };
  for (qsizetype i = 0; i < scene->gridLabels.size(); ++i, ++textIndex) {
    const gantt::GanttTextGeometry& label = scene->gridLabels.at(i);
    const QJsonObject el = textEls.at(textIndex).toObject();
    require(el.value(QStringLiteral("class")).toString().isEmpty(),
            id + QStringLiteral("/grid/%1/classless").arg(i));
    require(label.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/grid/%1/text").arg(i));
    compareTextPaint("grid", i, label, el);
    // The label x comes from the tick group transform (no own x attr), and
    // its baseline sits at k*spacing (3px) + dy(1em of the resolved size).
    const QJsonObject tickEl =
        matches.value(QStringLiteral(".grid .tick")).toArray().at(i).toObject();
    const auto match = translate.match(tickEl.value(QStringLiteral("attributes"))
                                           .toObject()
                                           .value(QStringLiteral("transform"))
                                           .toString());
    near(label.position.x() - scene->config.leftPadding,
         match.captured(1).toDouble(),
         id + QStringLiteral("/grid/%1/x").arg(i), 0.001);
    const qreal resolved = label.css.fontSize >= 0.0 ? label.css.fontSize : 10.0;
    near(label.position.y() - (scene->bounds.height() - 50.0) - 3.0, resolved,
         id + QStringLiteral("/grid/%1/baseline").arg(i), 0.001);
  }
  // The task-label class carries the classless getBBox measurement as its
  // width-N token; compare it numerically (Chrome ink vs Qt advances).
  static const QRegularExpression widthToken(
      QStringLiteral(R"(width-([0-9.eE+-]+))"));
  for (qsizetype i = 0; i < scene->taskLabels.size(); ++i, ++textIndex) {
    const gantt::GanttTextGeometry& label = scene->taskLabels.at(i);
    const QJsonObject el = textEls.at(textIndex).toObject();
    const QString fixtureClass =
        el.value(QStringLiteral("class")).toString();
    const auto fixtureWidth = widthToken.match(fixtureClass);
    const auto nativeWidth = widthToken.match(label.cssClass);
    require(fixtureWidth.hasMatch() == nativeWidth.hasMatch(),
            id + QStringLiteral("/taskText/%1/width-token").arg(i));
    if (nativeWidth.hasMatch()) {
      near(nativeWidth.captured(1).toDouble(), fixtureWidth.captured(1).toDouble(),
           id + QStringLiteral("/taskText/%1/width").arg(i), 1.6);
    }
    const auto stripWidth = [](QString value) {
      value.remove(widthToken);
      return value.split(QRegularExpression(QStringLiteral("\\s+")),
                         Qt::SkipEmptyParts).join(QLatin1Char(' '));
    };
    require(stripWidth(fixtureClass) == stripWidth(label.cssClass),
            id + QStringLiteral("/taskText/%1/class").arg(i));
    require(label.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/taskText/%1/text").arg(i));
    compareTextPaint("taskText", i, label, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(label.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/taskText/%1/x").arg(i));
    near(label.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/taskText/%1/y").arg(i));
  }
  for (qsizetype i = 0; i < scene->sectionLabels.size(); ++i, ++textIndex) {
    const gantt::GanttTextGeometry& label = scene->sectionLabels.at(i);
    const QJsonObject el = textEls.at(textIndex).toObject();
    require(classTokens(el.value(QStringLiteral("class")).toString()) ==
                classTokens(label.cssClass),
            id + QStringLiteral("/sectionTitle/%1/class").arg(i));
    const QString nativeText = label.lines.isEmpty() ? label.text
                                                     : label.lines.join(QString());
    require(nativeText == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/sectionTitle/%1/text").arg(i));
    compareTextPaint("sectionTitle", i, label, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(label.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/sectionTitle/%1/x").arg(i));
    near(label.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/sectionTitle/%1/y").arg(i));
  }
  {
    const gantt::GanttTextGeometry& label = scene->titleGeometry;
    const QJsonObject el = textEls.at(textIndex).toObject();
    require(classTokens(el.value(QStringLiteral("class")).toString()) ==
                classTokens(label.cssClass),
            id + QStringLiteral("/title/class"));
    require(label.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/title/text"));
    compareTextPaint("title", 0, label, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(label.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/title/x"));
    near(label.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/title/y"));
  }

  // The d3 domain path stays stroke-width 0 and inherits the currentColor
  // chain like the ticks.
  const QJsonArray pathEls = matches.value(QStringLiteral("path")).toArray();
  require(pathEls.size() == 1, id + QStringLiteral("/domain/count"));
  compareLinePaint("domain", 0, scene->gridDomain, pathEls.at(0).toObject());
}

void compareArchitecture(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene =
      renderScene<architecture::ArchitectureScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  // The viewBox is the measurement-feedback lock: it embeds the CSS-measured
  // label extents (paint 20px/root-inherit 19px widen the union by 8-11px).
  // The 0.2px tolerance covers the residual Qt/Chromium shaper difference
  // (≤1/128 per label — the design-advance measurer above), three orders
  // below any real feedback delta.
  {
    const QStringList fixtureBox =
        fixture.value(QStringLiteral("viewBox")).toString().split(QLatin1Char(' '));
    const QStringList nativeBox = scene->viewBoxAttribute.split(QLatin1Char(' '));
    require(fixtureBox.size() == 4 && nativeBox.size() == 4,
            id + QStringLiteral("/viewBox/shape"));
    for (int i = 0; i < 4; ++i)
      near(nativeBox.at(i).toDouble(), fixtureBox.at(i).toDouble(),
           id + QStringLiteral("/viewBox/%1").arg(i), 0.2);
  }
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto arr = [&](const QString& selector) -> QJsonArray {
    return matches.value(selector).toArray();
  };
  const qreal configFontSize =
      editor::jsNumberValue(scene->config.fontSize);
  const auto lengths = editor::pieCssLengthContext(scene->style.fontFamily,
                                                   configFontSize);
  const qreal diagonal = std::hypot(scene->bounds.width(),
                                    scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto swOf = [&](const QString& value, const QString& base) {
    return editor::cssStrokeWidthPx(effective(value, base), lengths, diagonal);
  };
  // Weight normalization: base rules store keywords, the browser reports
  // numeric computed values.
  const auto weightOf = [](const QString& raw, bool bold) {
    const QString weight = raw.trimmed().toLower();
    if (weight == QLatin1String("bold") || weight == QLatin1String("bolder"))
      return QStringLiteral("700");
    if (weight.isEmpty() || weight == QLatin1String("normal"))
      return bold ? QStringLiteral("700") : QStringLiteral("400");
    return raw.trimmed();
  };

  // Structural counts double-check the emission alignment.
  int arrowCount = 0;
  int edgeLabelCount = 0;
  for (const auto& edge : scene->edges) {
    if (!edge.arrows.isEmpty()) ++arrowCount;
    if (!edge.title.isEmpty()) ++edgeLabelCount;
  }
  int iconlessServices = 0;
  int serviceLabelCount = 0;
  for (const auto& node : scene->nodes)
    if (node.kind == architecture::ArchitectureNodeKind::Service) {
      if (node.icon.isEmpty() && node.iconText.isEmpty()) ++iconlessServices;
      if (!node.title.isEmpty()) ++serviceLabelCount;
    }
  const int groupLabelCount = int(scene->groups.size());
  require(arr(QStringLiteral(".edge")).size() == scene->edges.size(),
          id + QStringLiteral("/edge/count"));
  require(arr(QStringLiteral(".arrow")).size() == arrowCount,
          id + QStringLiteral("/arrow/count"));
  require(arr(QStringLiteral(".node-bkg")).size() ==
              iconlessServices + int(scene->groups.size()),
          id + QStringLiteral("/node-bkg/count"));
  require(arr(QStringLiteral("text")).size() ==
              edgeLabelCount + serviceLabelCount + groupLabelCount,
          id + QStringLiteral("/text/count"));

  // Edge lines: stroke, stroke-width, bbox extent, display chain.
  const QJsonArray edgeEls = arr(QStringLiteral(".edge"));
  for (qsizetype i = 0; i < scene->edges.size(); ++i) {
    const auto& edge = scene->edges.at(i);
    const QJsonObject el = edgeEls.at(i).toObject();
    const QString label =
        QStringLiteral("/edge/%1/").arg(i);
    sameColor(effective(edge.lineCss.stroke, scene->style.edgeColor),
              computed(el, "stroke"), id + label + QStringLiteral("stroke"));
    near(swOf(edge.lineCss.strokeWidth, scene->style.edgeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + label + QStringLiteral("strokeWidth"), 0.001);
    require(edge.lineCss.visible == browserDisplayed(el),
            id + label + QStringLiteral("visible"));
    const QRectF polyline(edge.bounds);
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    near(polyline.width(), bbox.value(QStringLiteral("width")).toDouble(),
         id + label + QStringLiteral("bbox-width"), 0.5);
  }

  // Arrow polygons: fill + opacity + display chain.
  const QJsonArray arrowEls = arr(QStringLiteral(".arrow"));
  qsizetype arrowSlot = 0;
  for (const auto& edge : scene->edges) {
    if (edge.arrows.isEmpty()) continue;
    const QJsonObject el = arrowEls.at(arrowSlot).toObject();
    const QString label = QStringLiteral("/arrow/%1/").arg(arrowSlot);
    sameColor(effective(edge.arrowCss.fill, scene->style.arrowColor),
              computed(el, "fill"), id + label + QStringLiteral("fill"));
    near(edge.arrowCss.opacity >= 0.0 ? edge.arrowCss.opacity : 1.0,
         elementOpacity(el), id + label + QStringLiteral("opacity"), 0.01);
    require(edge.arrowCss.visible == browserDisplayed(el),
            id + label + QStringLiteral("visible"));
    ++arrowSlot;
  }

  // node-bkg: iconless service paths first, then group rects (DOM order).
  const QJsonArray bkgEls = arr(QStringLiteral(".node-bkg"));
  qsizetype bkgSlot = 0;
  const auto compareBkg = [&](const architecture::ArchitectureElementCss& css,
                              const QString& baseStroke,
                              const QString& baseWidth, const char* part) {
    const QJsonObject el = bkgEls.at(bkgSlot).toObject();
    const QString label =
        QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(bkgSlot);
    sameColor(effective(css.fill, QStringLiteral("none")),
              computed(el, "fill"), id + label + QStringLiteral("fill"));
    sameColor(effective(css.stroke, baseStroke),
              computed(el, "stroke"), id + label + QStringLiteral("stroke"));
    near(swOf(css.strokeWidth, baseWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + label + QStringLiteral("strokeWidth"), 0.001);
    require(css.visible == browserDisplayed(el),
            id + label + QStringLiteral("visible"));
    ++bkgSlot;
  };
  for (const auto& node : scene->nodes)
    if (node.kind == architecture::ArchitectureNodeKind::Service &&
        node.icon.isEmpty() && node.iconText.isEmpty())
      compareBkg(node.nodeBkgCss, scene->style.groupBorderColor,
                 scene->style.groupBorderWidth, "nodebkg");
  for (const auto& group : scene->groups)
    compareBkg(group.rectCss, scene->style.groupBorderColor,
               scene->style.groupBorderWidth, "groupbkg");

  // Labels in document order: edge titles, service titles, group titles.
  const QJsonArray textEls = arr(QStringLiteral("text"));
  qsizetype textSlot = 0;
  const auto compareLabel = [&](const architecture::ArchitectureElementCss& css,
                                const char* part, qsizetype index) {
    const QJsonObject el = textEls.at(textSlot).toObject();
    const QString label =
        QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(index);
    sameColor(effective(css.fill, scene->style.textColor),
              computed(el, "fill"), id + label + QStringLiteral("fill"));
    near(css.fontSize >= 0.0 ? css.fontSize : configFontSize,
         computed(el, "fontSize").chopped(2).toDouble(),
         id + label + QStringLiteral("fontSize"), 0.01);
    require(weightOf(css.fontWeight, false) ==
                 computed(el, "fontWeight").trimmed(),
             id + label + QStringLiteral("fontWeight"));
    require(css.visible == browserDisplayed(el),
            id + label + QStringLiteral("visible"));
    ++textSlot;
  };
  for (const auto& edge : scene->edges)
    if (!edge.title.isEmpty()) compareLabel(edge.labelCss, "edgelabel", 0);
  for (const auto& node : scene->nodes)
    if (node.kind == architecture::ArchitectureNodeKind::Service &&
        !node.title.isEmpty())
      compareLabel(node.labelCss, "nodelabel", textSlot);
  for (qsizetype k = 0; k < scene->groups.size(); ++k)
    if (!scene->groups.at(k).title.isEmpty())
      compareLabel(scene->groups.at(k).labelCss, "grouplabel", k);
}

void compareRailroad(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<railroad::RailroadScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  // The viewBox embeds the probe-measured layout: the root-inherit `text` tag
  // rule beats the probe's font presentation attrs and resizes the whole
  // diagram (687.7x140 -> 756.9x150). Exact string lock — the native measures
  // with the harfBuzz/Chromium-parity path.
  // The 0.2px tolerance covers the residual measurement quantization
  // (native ceil/64 vs Chromium LayoutUnit, ~1e-4 here), far below the
  // root-inherit feedback delta (~70px).
  {
    const QStringList fixtureBox =
        fixture.value(QStringLiteral("viewBox")).toString().split(QLatin1Char(' '));
    require(fixtureBox.size() == 4, id + QStringLiteral("/viewBox/shape"));
    near(scene->bounds.width(), fixtureBox.at(2).toDouble(),
         id + QStringLiteral("/viewBox/width"), 0.2);
    near(scene->bounds.height(), fixtureBox.at(3).toDouble(),
         id + QStringLiteral("/viewBox/height"), 0.2);
    near(scene->bounds.x(), fixtureBox.at(0).toDouble(),
         id + QStringLiteral("/viewBox/x"), 0.2);
    near(scene->bounds.y(), fixtureBox.at(1).toDouble(),
         id + QStringLiteral("/viewBox/y"), 0.2);
  }
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto arr = [&](const QString& selector) -> QJsonArray {
    return matches.value(selector).toArray();
  };
  const qreal configFontSize = scene->config.fontSize;
  const auto lengths =
      editor::pieCssLengthContext(scene->config.fontFamily, configFontSize);
  const qreal diagonal = std::hypot(scene->bounds.width(),
                                    scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto prims = [&](railroad::RailroadPrimitiveKind kind,
                         const QString& cssClass) {
    QVector<const railroad::RailroadPrimitive*> out;
    for (const auto& p : scene->primitives)
      if (p.kind == kind && p.cssClass == cssClass) out.append(&p);
    return out;
  };
  const auto weightOf = [](const QString& raw, bool bold) {
    const QString weight = raw.trimmed().toLower();
    if (weight == QLatin1String("bold") || weight == QLatin1String("bolder"))
      return QStringLiteral("700");
    if (weight.isEmpty() || weight == QLatin1String("normal"))
      return bold ? QStringLiteral("700") : QStringLiteral("400");
    return raw.trimmed();
  };

  // Leaf shapes: rect/ellipse fill+stroke+sw+display; labels: fill+font.
  const auto compareShapes = [&](const QString& selector,
                                 railroad::RailroadPrimitiveKind kind,
                                 const QString& cssClass) {
    const QVector<const railroad::RailroadPrimitive*> native =
        prims(kind, cssClass);
    const QJsonArray els = arr(selector);
    require(els.size() == native.size(),
            id + QStringLiteral("/") + selector + QStringLiteral("/count"));
    for (int i = 0; i < native.size(); ++i) {
      const auto& p = *native.at(i);
      const QJsonObject el = els.at(i).toObject();
      const QString label =
          QStringLiteral("/") + selector + QStringLiteral("/%1/").arg(i);
      sameColor(effective(p.css.fill, p.fill), computed(el, "fill"),
                id + label + QStringLiteral("fill"));
      if (!p.css.stroke.isEmpty() ||
          computed(el, "stroke") != QLatin1String("none")) {
        sameColor(effective(p.css.stroke, p.stroke), computed(el, "stroke"),
                  id + label + QStringLiteral("stroke"));
        const qreal sw = editor::cssStrokeWidthPx(
            effective(p.css.strokeWidth,
                      QString::number(p.strokeWidth) + QStringLiteral("px")),
            lengths, diagonal);
        near(sw, computed(el, "strokeWidth").chopped(2).toDouble(),
             id + label + QStringLiteral("strokeWidth"), 0.001);
      }
      require(p.css.visible == browserDisplayed(el),
              id + label + QStringLiteral("visible"));
    }
  };
  const auto compareLabels = [&](const QString& selector,
                                 const QString& cssClass) {
    const QVector<const railroad::RailroadPrimitive*> native =
        prims(railroad::RailroadPrimitiveKind::Text, cssClass);
    const QJsonArray els = arr(selector);
    require(els.size() == native.size(),
            id + QStringLiteral("/") + selector + QStringLiteral("/count"));
    for (int i = 0; i < native.size(); ++i) {
      const auto& p = *native.at(i);
      const QJsonObject el = els.at(i).toObject();
      const QString label =
          QStringLiteral("/") + selector + QStringLiteral("/%1/").arg(i);
      sameColor(effective(p.css.fill, p.fill), computed(el, "fill"),
                id + label + QStringLiteral("fill"));
      near(p.css.fontSize >= 0.0 ? p.css.fontSize : configFontSize,
           computed(el, "fontSize").chopped(2).toDouble(),
           id + label + QStringLiteral("fontSize"), 0.01);
      require(weightOf(p.css.fontWeight, p.bold) ==
                   computed(el, "fontWeight").trimmed(),
               id + label + QStringLiteral("fontWeight"));
      require(p.css.visible == browserDisplayed(el),
              id + label + QStringLiteral("visible"));
    }
  };
  compareShapes(QStringLiteral(".railroad-terminal rect"),
                railroad::RailroadPrimitiveKind::Rect,
                QStringLiteral("railroad-terminal"));
  compareLabels(QStringLiteral(".railroad-terminal text"),
                QStringLiteral("railroad-terminal"));
  compareShapes(QStringLiteral(".railroad-nonterminal rect"),
                railroad::RailroadPrimitiveKind::Rect,
                QStringLiteral("railroad-nonterminal"));
  compareLabels(QStringLiteral(".railroad-nonterminal text"),
                QStringLiteral("railroad-nonterminal"));
  compareShapes(QStringLiteral(".railroad-special rect"),
                railroad::RailroadPrimitiveKind::Rect,
                QStringLiteral("railroad-special"));
  compareLabels(QStringLiteral(".railroad-special text"),
                QStringLiteral("railroad-special"));
  compareShapes(QStringLiteral(".railroad-line"),
                railroad::RailroadPrimitiveKind::Path,
                QStringLiteral("railroad-line"));
  compareShapes(QStringLiteral(".railroad-start circle"),
                railroad::RailroadPrimitiveKind::Circle,
                QStringLiteral("railroad-start"));
  compareShapes(QStringLiteral(".railroad-end circle"),
                railroad::RailroadPrimitiveKind::Circle,
                QStringLiteral("railroad-end"));
  compareLabels(QStringLiteral(".railroad-rule-name"),
                QStringLiteral("railroad-rule-name"));
}

// Wardley is themeCSS-INERT: its draw() opens with svg.selectAll("*").remove(),
// which destroys the <style> element createUserStyles injected before draw, so
// neither the base sheet nor user themeCSS ever reaches a wardley element. The
// inert probe carries a sheet that would recolor most surfaces; this comparator
// locks the bare presentation-attribute rendering for both cases. The overlay
// fills (#666/#ccc/#eee/#fff) and the anchor label fill (#000) are the smoking
// gun: had the base `.wardley-node circle` / `.wardley-node-label` rules
// applied, every overlay would resolve to componentFill and the anchor label to
// componentLabelColor. A future overlay wiring would diverge from this oracle.
void compareWardley(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<wardley::WardleyScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  require(fixture.value(QStringLiteral("viewBox")).toString() ==
              scene->viewBoxAttribute,
          id + QStringLiteral("/viewBox"));
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto oracle = [&](const QString& selector, int index = 0) -> QJsonObject {
    const QJsonArray arr = matches.value(selector).toArray();
    return (index >= 0 && index < arr.size()) ? arr.at(index).toObject()
                                              : QJsonObject();
  };
  const auto findPrim = [&](const auto& predicate) -> const wardley::WardleyPrimitive* {
    for (const auto& p : scene->primitives)
      if (predicate(p)) return &p;
    return nullptr;
  };
  const auto countPrim = [&](const auto& predicate) -> int {
    int n = 0;
    for (const auto& p : scene->primitives)
      if (predicate(p)) ++n;
    return n;
  };
  const auto bboxOf = [](const QJsonObject& el) -> QJsonObject {
    return el.value(QStringLiteral("bbox")).toObject();
  };

  // Structural counts double-check the emission alignment.
  require(matches.value(QStringLiteral(".wardley-node circle")).toArray().size() ==
              countPrim([](const wardley::WardleyPrimitive& p) {
                return p.type == wardley::WardleyPrimitiveType::Circle &&
                       p.parentClass.contains(QStringLiteral("wardley-node"));
              }),
          id + QStringLiteral("/node-circle/count"));
  require(matches.value(QStringLiteral(".wardley-node-label")).toArray().size() ==
              countPrim([](const wardley::WardleyPrimitive& p) {
                return p.type == wardley::WardleyPrimitiveType::Text &&
                       p.role == QStringLiteral("wardley-node-label");
              }),
          id + QStringLiteral("/node-label/count"));
  require(matches.value(QStringLiteral(".wardley-link")).toArray().size() ==
              countPrim([](const wardley::WardleyPrimitive& p) {
                return p.type == wardley::WardleyPrimitiveType::Line &&
                       p.role.contains(QStringLiteral("wardley-link"));
              }),
          id + QStringLiteral("/link/count"));
  require(matches.value(QStringLiteral(".wardley-trend")).toArray().size() ==
              countPrim([](const wardley::WardleyPrimitive& p) {
                return p.type == wardley::WardleyPrimitiveType::Line &&
                       p.role == QStringLiteral("wardley-trend");
              }),
          id + QStringLiteral("/trend/count"));
  require(matches.value(QStringLiteral(".wardley-annotations-box text"))
                  .toArray()
                  .size() ==
              countPrim([](const wardley::WardleyPrimitive& p) {
                return p.type == wardley::WardleyPrimitiveType::Text &&
                       p.parentClass ==
                           QStringLiteral("wardley-annotations-box");
              }),
          id + QStringLiteral("/annbox-text/count"));

  // Background rect: fill + geometry (covers the whole viewBox).
  const wardley::WardleyPrimitive* bg =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.role == QStringLiteral("wardley-background");
      });
  require(bg != nullptr, id + QStringLiteral("/background/exists"));
  sameColor(bg->fill, computed(oracle(".wardley-background"), "fill"),
            id + QStringLiteral("/background/fill"));
  near(bg->rect.width(),
       bboxOf(oracle(".wardley-background")).value("width").toDouble(),
       id + QStringLiteral("/background/width"), 0.5);
  near(bg->rect.height(),
       bboxOf(oracle(".wardley-background")).value("height").toDouble(),
       id + QStringLiteral("/background/height"), 0.5);

  // Overlay circles — INERTNESS LOCK: bare presentation fills survive because
  // the `.wardley-node circle` rule never applies.
  const auto overlayFill = [&](const QString& role, const QString& selector) {
    const wardley::WardleyPrimitive* p =
        findPrim([&](const wardley::WardleyPrimitive& x) { return x.role == role; });
    require(p != nullptr, id + QStringLiteral("/") + role + "/exists");
    sameColor(p->fill, computed(oracle(selector), "fill"),
              id + QStringLiteral("/") + selector + "/fill");
  };
  overlayFill(QStringLiteral("wardley-outsource-overlay"),
              QStringLiteral(".wardley-outsource-overlay"));
  overlayFill(QStringLiteral("wardley-buy-overlay"),
              QStringLiteral(".wardley-buy-overlay"));
  overlayFill(QStringLiteral("wardley-build-overlay"),
              QStringLiteral(".wardley-build-overlay"));
  overlayFill(QStringLiteral("wardley-market-overlay"),
              QStringLiteral(".wardley-market-overlay"));

  // Anchor node label — INERTNESS LOCK: stays #000 (presentation) instead of
  // resolving to componentLabelColor via `.wardley-node-label`.
  const wardley::WardleyPrimitive* anchorLabel =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.role == QStringLiteral("wardley-node-label") &&
               p.parentClass.contains(QStringLiteral("anchor"));
      });
  require(anchorLabel != nullptr, id + QStringLiteral("/anchor-label/exists"));
  sameColor(anchorLabel->fill, computed(oracle(".wardley-node-label"), "fill"),
            id + QStringLiteral("/anchor-label/fill"));

  // Link + trend strokes (presentation attributes).
  const wardley::WardleyPrimitive* link =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.type == wardley::WardleyPrimitiveType::Line &&
               p.role == QStringLiteral("wardley-link");
      });
  require(link != nullptr, id + QStringLiteral("/link/exists"));
  sameColor(link->stroke, computed(oracle(".wardley-link"), "stroke"),
            id + QStringLiteral("/link/stroke"));
  const wardley::WardleyPrimitive* trend =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.role == QStringLiteral("wardley-trend");
      });
  require(trend != nullptr, id + QStringLiteral("/trend/exists"));
  sameColor(trend->stroke, computed(oracle(".wardley-trend"), "stroke"),
            id + QStringLiteral("/trend/stroke"));

  // Annotation marker circle.
  const wardley::WardleyPrimitive* annCircle =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.type == wardley::WardleyPrimitiveType::Circle &&
               p.parentClass == QStringLiteral("wardley-annotation");
      });
  require(annCircle != nullptr, id + QStringLiteral("/ann-circle/exists"));
  sameColor(annCircle->fill,
            computed(oracle(".wardley-annotation circle"), "fill"),
            id + QStringLiteral("/ann-circle/fill"));
  sameColor(annCircle->stroke,
            computed(oracle(".wardley-annotation circle"), "stroke"),
            id + QStringLiteral("/ann-circle/stroke"));

  // Annotations box: rect fill/stroke + width (the measurement feedback), and
  // the first text fill.
  const wardley::WardleyPrimitive* annBox =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.type == wardley::WardleyPrimitiveType::Rect &&
               p.parentClass == QStringLiteral("wardley-annotations-box");
      });
  require(annBox != nullptr, id + QStringLiteral("/annbox/exists"));
  sameColor(annBox->fill,
            computed(oracle(".wardley-annotations-box rect"), "fill"),
            id + QStringLiteral("/annbox/fill"));
  sameColor(annBox->stroke,
            computed(oracle(".wardley-annotations-box rect"), "stroke"),
            id + QStringLiteral("/annbox/stroke"));
  near(annBox->rect.width(),
       bboxOf(oracle(".wardley-annotations-box rect")).value("width").toDouble(),
       id + QStringLiteral("/annbox/width"), 1.5);
  const wardley::WardleyPrimitive* annBoxText =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.type == wardley::WardleyPrimitiveType::Text &&
               p.parentClass == QStringLiteral("wardley-annotations-box");
      });
  require(annBoxText != nullptr, id + QStringLiteral("/annbox-text/exists"));
  sameColor(annBoxText->fill,
            computed(oracle(".wardley-annotations-box text"), "fill"),
            id + QStringLiteral("/annbox-text/fill"));

  // Title fill.
  const wardley::WardleyPrimitive* title =
      findPrim([](const wardley::WardleyPrimitive& p) {
        return p.role == QStringLiteral("wardley-title");
      });
  require(title != nullptr, id + QStringLiteral("/title/exists"));
  sameColor(title->fill, computed(oracle(".wardley-title"), "fill"),
            id + QStringLiteral("/title/fill"));
}

void compareCynefin(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<cynefin::CynefinScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  require(fixture.value(QStringLiteral("viewBox")).toString() ==
              scene->viewBoxAttribute,
          id + QStringLiteral("/viewBox"));
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const qreal diagonal = std::hypot(scene->bounds.width(), scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto lengths = editor::pieCssLengthContext(
      scene->style.fontFamily, scene->style.rootFontSize);
  const auto strokeWidthOf = [&](const cynefin::CynefinElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, QString::number(base) + QStringLiteral("px")),
        lengths, diagonal);
  };
  const auto opacityOf = [](const cynefin::CynefinElementCss& css, qreal base) {
    return css.opacity >= 0.0 ? css.opacity : base;
  };
  // Document-order selector counts double-check the emission alignment.
  require(matches.value(QStringLiteral(".cynefinDomain")).toArray().size() == 4,
          id + QStringLiteral("/domain/count"));
  require(matches.value(QStringLiteral(".cynefinBoundary")).toArray().size() == 2,
          id + QStringLiteral("/boundary/count"));
  require(matches.value(QStringLiteral(".cynefinCliff")).toArray().size() == 1,
          id + QStringLiteral("/cliff/count"));
  require(matches.value(QStringLiteral(".cynefinDomainLabel")).toArray().size() ==
              scene->labels.size(),
          id + QStringLiteral("/label/count"));
  require(matches.value(QStringLiteral(".cynefinSubtitle")).toArray().size() ==
              scene->subtitles.size(),
          id + QStringLiteral("/subtitle/count"));
  require(matches.value(QStringLiteral(".cynefinItem")).toArray().size() +
              matches.value(QStringLiteral(".cynefinItemOverflow")).toArray()
                  .size() ==
              scene->items.size(),
          id + QStringLiteral("/item/count"));
  require(matches.value(QStringLiteral(".cynefinItemText")).toArray().size() ==
              scene->items.size(),
          id + QStringLiteral("/item-text/count"));
  require(matches.value(QStringLiteral(".cynefinArrowLine")).toArray().size() ==
              scene->arrows.size(),
          id + QStringLiteral("/arrow/count"));

  const auto compareRectPaint = [&](const char* part, qsizetype index,
                                    const cynefin::CynefinRectGeometry& rect,
                                    const QJsonObject& el) {
    sameColor(effective(rect.css.fill, rect.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    sameColor(effective(rect.css.stroke, rect.stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(part)).arg(index));
    if (!rect.css.stroke.isEmpty() ||
        computed(el, "stroke") != QLatin1String("none"))
      near(strokeWidthOf(rect.css, rect.strokeWidth),
           computed(el, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/strokeWidth").arg(QLatin1String(part)).arg(index),
           0.001);
    const qreal fillOpacity = rect.css.fillOpacity >= 0.0
                                  ? rect.css.fillOpacity
                                  : rect.fillOpacity;
    near(fillOpacity, elementFillOpacity(el),
         id + QStringLiteral("/%1/%2/fillOpacity").arg(QLatin1String(part)).arg(index),
         0.01);
    require(rect.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(rect.css, 1.0), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };
  const auto compareBox = [&](const char* part, qsizetype index,
                              const cynefin::CynefinElementCss& css,
                              const QRectF& geometry, const QJsonObject& el,
                              qreal tolerance) {
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    const QString label = QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(index);
    const qreal factor = css.measures ? 1.0 : 0.0;
    near(geometry.x() * factor, bbox.value(QStringLiteral("x")).toDouble(),
         id + label + QStringLiteral("x"), tolerance);
    near(geometry.y() * factor, bbox.value(QStringLiteral("y")).toDouble(),
         id + label + QStringLiteral("y"), tolerance);
    near(geometry.width() * factor, bbox.value(QStringLiteral("width")).toDouble(),
         id + label + QStringLiteral("width"), tolerance);
    near(geometry.height() * factor, bbox.value(QStringLiteral("height")).toDouble(),
         id + label + QStringLiteral("height"), tolerance);
  };

  const QJsonArray domainEls =
      matches.value(QStringLiteral(".cynefinDomain")).toArray();
  for (qsizetype i = 0; i < scene->backgrounds.size(); ++i) {
    const auto& bg = scene->backgrounds.at(i);
    const QJsonObject el = domainEls.at(i).toObject();
    compareRectPaint("domain", i, bg, el);
    compareBox("domain", i, bg.css, bg.rect, el, 0.51);
  }

  // The fold/horizontal/cliff paths are seeded-random but upstream-deterministic;
  // their geometry is verified through the pixel oracles, so here only the
  // paint surface is locked.
  const auto comparePathPaint = [&](const char* part, qsizetype index,
                                    const cynefin::CynefinPathGeometry& path,
                                    const QJsonObject& el) {
    sameColor(effective(path.css.stroke, path.stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(part)).arg(index));
    near(strokeWidthOf(path.css, path.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/strokeWidth").arg(QLatin1String(part)).arg(index),
         0.001);
    require(path.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(path.css, 1.0), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };
  const QJsonArray boundaryEls =
      matches.value(QStringLiteral(".cynefinBoundary")).toArray();
  for (qsizetype i = 0; i < boundaryEls.size(); ++i)
    comparePathPaint("boundary", i, scene->boundaries.at(i), boundaryEls.at(i).toObject());
  comparePathPaint("cliff", 0, scene->boundaries.at(2),
                   matches.value(QStringLiteral(".cynefinCliff"))
                       .toArray().at(0).toObject());
  const QJsonObject confusionEl =
      matches.value(QStringLiteral(".cynefinConfusion"))
          .toArray().at(0).toObject();
  sameColor(effective(scene->confusion.css.fill, scene->confusion.fill),
            computed(confusionEl, "fill"),
            id + QStringLiteral("/confusion/fill"));
  near(scene->confusion.css.fillOpacity >= 0.0
           ? scene->confusion.css.fillOpacity
           : scene->confusion.fillOpacity,
       elementFillOpacity(confusionEl),
       id + QStringLiteral("/confusion/fillOpacity"), 0.01);
  comparePathPaint("confusion", 0, scene->confusion, confusionEl);
  compareBox("confusion", 0, scene->confusion.css,
             scene->confusion.path.boundingRect(), confusionEl, 0.51);

  const auto compareTextPaint = [&](const char* part, qsizetype index,
                                    const cynefin::CynefinTextGeometry& text,
                                    const QJsonObject& el) {
    sameColor(effective(text.css.fill, text.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    const qreal fontSize = text.css.fontSize >= 0.0 ? text.css.fontSize
                                                    : text.fontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/fontSize").arg(QLatin1String(part)).arg(index),
         0.001);
    const QString rawWeight = text.css.fontWeight.trimmed().toLower();
    const QString weight = rawWeight == QLatin1String("bold") ||
                                   rawWeight == QLatin1String("bolder")
                               ? QStringLiteral("700")
                               : (rawWeight == QLatin1String("normal") ||
                                          rawWeight.isEmpty()
                                      ? QString::number(text.bold ? 700 : 400)
                                      : rawWeight);
    require(weight == computed(el, "fontWeight"),
            id + QStringLiteral("/%1/%2/fontWeight").arg(QLatin1String(part)).arg(index));
    const QString fontStyle = text.css.fontStyle.trimmed().isEmpty()
                                  ? (text.italic ? QStringLiteral("italic")
                                                 : QStringLiteral("normal"))
                                  : text.css.fontStyle.trimmed().toLower();
    require(fontStyle == computed(el, "fontStyle"),
            id + QStringLiteral("/%1/%2/fontStyle").arg(QLatin1String(part)).arg(index));
    require(text.css.visible == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(text.css, 1.0), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };

  const QJsonArray labelEls =
      matches.value(QStringLiteral(".cynefinDomainLabel")).toArray();
  for (qsizetype i = 0; i < scene->labels.size(); ++i) {
    const auto& label = scene->labels.at(i);
    const QJsonObject el = labelEls.at(i).toObject();
    require(label.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/label/%1/text").arg(i));
    compareTextPaint("label", i, label, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(label.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/label/%1/x").arg(i));
    near(label.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/label/%1/y").arg(i));
  }
  const QJsonArray subtitleEls =
      matches.value(QStringLiteral(".cynefinSubtitle")).toArray();
  for (qsizetype i = 0; i < scene->subtitles.size(); ++i) {
    const auto& sub = scene->subtitles.at(i);
    const QJsonObject el = subtitleEls.at(i).toObject();
    compareTextPaint("subtitle", i, sub, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(sub.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/subtitle/%1/x").arg(i));
    near(sub.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/subtitle/%1/y").arg(i));
  }

  // Items: the badge rect carries the classed getBBox as its width. The
  // overflow badge trails the confusion items; the text x attr is half the
  // badge width, and own display:none zeroes the text bbox.
  const QJsonArray itemEls = matches.value(QStringLiteral(".cynefinItem")).toArray();
  const QJsonArray overflowEls =
      matches.value(QStringLiteral(".cynefinItemOverflow")).toArray();
  const auto itemEl = [&](qsizetype i) {
    return i < itemEls.size()
        ? itemEls.at(i).toObject()
        : overflowEls.at(i - itemEls.size()).toObject();
  };
  for (qsizetype i = 0; i < scene->items.size(); ++i) {
    const auto& item = scene->items.at(i);
    const QJsonObject el = itemEl(i);
    require(el.value(QStringLiteral("class")).toString() ==
              (item.overflow ? QStringLiteral("cynefinItemOverflow")
                             : QStringLiteral("cynefinItem")),
            id + QStringLiteral("/item/%1/class").arg(i));
    compareRectPaint("item", i, item.rect, el);
    compareBox("item", i, item.rect.css, item.rect.rect, el, 1.6);
  }
  const QJsonArray itemTextEls =
      matches.value(QStringLiteral(".cynefinItemText")).toArray();
  for (qsizetype i = 0; i < scene->items.size(); ++i) {
    const auto& item = scene->items.at(i);
    const QJsonObject el = itemTextEls.at(i).toObject();
    require(item.text.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/item-text/%1/text").arg(i));
    compareTextPaint("item-text", i, item.text, el);
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(item.text.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/item-text/%1/x").arg(i));
    near(item.text.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/item-text/%1/y").arg(i));
    // Ink width (Chrome getBBox vs Qt advances); own display:none collapses
    // the bbox to 0x0 while the native text bounds keep the geometry.
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    const qreal factor = item.text.css.measures ? 1.0 : 0.0;
    near(item.text.bounds.width() * factor,
         bbox.value(QStringLiteral("width")).toDouble(),
         id + QStringLiteral("/item-text/%1/width").arg(i), 1.6);
  }

  const QJsonArray arrowLineEls =
      matches.value(QStringLiteral(".cynefinArrowLine")).toArray();
  for (qsizetype i = 0; i < scene->arrows.size(); ++i) {
    const auto& arrow = scene->arrows.at(i);
    const QJsonObject el = arrowLineEls.at(i).toObject();
    sameColor(effective(arrow.css.stroke, arrow.stroke), computed(el, "stroke"),
              id + QStringLiteral("/arrow/%1/stroke").arg(i));
    near(strokeWidthOf(arrow.css, arrow.strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/arrow/%1/strokeWidth").arg(i), 0.001);
    require(arrow.css.visible == browserDisplayed(el),
            id + QStringLiteral("/arrow/%1/visible").arg(i));
    near(opacityOf(arrow.css, 1.0), elementOpacity(el),
         id + QStringLiteral("/arrow/%1/opacity").arg(i), 0.01);
    if (!arrow.label.text.isEmpty()) {
      const QJsonObject labelEl =
          matches.value(QStringLiteral(".cynefinArrowLabel")).toArray().at(i).toObject();
      compareTextPaint("arrow-label", i, arrow.label, labelEl);
      const QJsonObject attrs = labelEl.value(QStringLiteral("attributes")).toObject();
      near(arrow.label.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
           id + QStringLiteral("/arrow-label/%1/x").arg(i));
      near(arrow.label.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
           id + QStringLiteral("/arrow-label/%1/y").arg(i));
    }
  }
  const QJsonArray arrowHeadEls =
      matches.value(QStringLiteral(".cynefinArrowHead")).toArray();
  for (qsizetype i = 0; i < arrowHeadEls.size(); ++i) {
    const QJsonObject el = arrowHeadEls.at(i).toObject();
    sameColor(scene->arrowHeadCss.fill.isEmpty()
                  ? scene->arrows.at(i).stroke
                  : scene->arrowHeadCss.fill,
              computed(el, "fill"), id + QStringLiteral("/arrowhead/fill"));
    require(scene->arrowHeadCss.visible == browserDisplayed(el),
            id + QStringLiteral("/arrowhead/visible"));
  }

  const QJsonObject titleEl =
      matches.value(QStringLiteral(".cynefinTitle")).toArray().at(0).toObject();
  require(scene->title.text == titleEl.value(QStringLiteral("text")).toString(),
          id + QStringLiteral("/title/text"));
  compareTextPaint("title", 0, scene->title, titleEl);
  const QJsonObject titleAttrs = titleEl.value(QStringLiteral("attributes")).toObject();
  near(scene->title.position.x(), titleAttrs.value(QStringLiteral("x")).toString().toDouble(),
       id + QStringLiteral("/title/x"));
  near(scene->title.position.y(), titleAttrs.value(QStringLiteral("y")).toString().toDouble(),
       id + QStringLiteral("/title/y"));
}

void compareEventModeling(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<eventmodeling::EventModelingScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const qreal diagonal = std::hypot(scene->bounds.width(), scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene->style.fontFamily),
      scene->style.rootFontSize);
  const auto strokeWidthOf = [&](const eventmodeling::EventModelingElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, QString::number(base) + QStringLiteral("px")),
        lengths, diagonal);
  };
  // Own display:none collapses the Chrome bbox to 0x0; the full-width
  // swimlane backgrounds keep the union constant across every case.
  const auto compareBox = [&](const char* part, qsizetype index,
                              const eventmodeling::EventModelingElementCss& css,
                              const QRectF& geometry, const QJsonObject& el) {
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    const QString label = QStringLiteral("/%1/%2/").arg(QLatin1String(part)).arg(index);
    const qreal factor = css.measures ? 1.0 : 0.0;
    near(geometry.x() * factor, bbox.value(QStringLiteral("x")).toDouble(),
         id + label + QStringLiteral("x"));
    near(geometry.y() * factor, bbox.value(QStringLiteral("y")).toDouble(),
         id + label + QStringLiteral("y"));
    near(geometry.width() * factor, bbox.value(QStringLiteral("width")).toDouble(),
         id + label + QStringLiteral("width"));
    near(geometry.height() * factor, bbox.value(QStringLiteral("height")).toDouble(),
         id + label + QStringLiteral("height"));
  };

  // rects in document order: swimlane backgrounds, then boxes.
  const QJsonArray rectEls = matches.value(QStringLiteral("rect")).toArray();
  require(rectEls.size() == scene->swimlanes.size() + scene->boxes.size(),
          id + QStringLiteral("/rect/count"));
  for (qsizetype i = 0; i < scene->swimlanes.size(); ++i) {
    const QJsonObject el = rectEls.at(i).toObject();
    const auto& lane = scene->swimlanes.at(i);
    sameColor(effective(lane.rectCss.fill, scene->style.swimlaneFill),
              computed(el, "fill"), id + QStringLiteral("/lane/%1/fill").arg(i));
    sameColor(effective(lane.rectCss.stroke, scene->style.swimlaneStroke),
              computed(el, "stroke"), id + QStringLiteral("/lane/%1/stroke").arg(i));
    near(strokeWidthOf(lane.rectCss, 1.0),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/lane/%1/strokeWidth").arg(i), 0.001);
    require(lane.rectCss.visible == browserDisplayed(el),
            id + QStringLiteral("/lane/%1/visible").arg(i));
    near(lane.rectCss.opacity >= 0.0 ? lane.rectCss.opacity : 1.0,
         elementOpacity(el), id + QStringLiteral("/lane/%1/opacity").arg(i), 0.01);
    compareBox("lane", i, lane.rectCss, lane.rect, el);
  }
  for (qsizetype i = 0; i < scene->boxes.size(); ++i) {
    const QJsonObject el = rectEls.at(scene->swimlanes.size() + i).toObject();
    const auto& box = scene->boxes.at(i);
    sameColor(effective(box.rectCss.fill, box.fill), computed(el, "fill"),
              id + QStringLiteral("/box/%1/fill").arg(i));
    sameColor(effective(box.rectCss.stroke, box.stroke), computed(el, "stroke"),
              id + QStringLiteral("/box/%1/stroke").arg(i));
    near(strokeWidthOf(box.rectCss, 1.0),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/box/%1/strokeWidth").arg(i), 0.001);
    require(box.rectCss.visible == browserDisplayed(el),
            id + QStringLiteral("/box/%1/visible").arg(i));
    near(box.rectCss.opacity >= 0.0 ? box.rectCss.opacity : 1.0,
         elementOpacity(el), id + QStringLiteral("/box/%1/opacity").arg(i), 0.01);
    compareBox("box", i, box.rectCss, box.rect, el);
  }

  // Swimlane titles: font-weight presentation attribute, fill inherited from
  // the root (no attribute, no base sheet).
  const QJsonArray textEls = matches.value(QStringLiteral("text")).toArray();
  require(textEls.size() == scene->swimlanes.size(),
          id + QStringLiteral("/text/count"));
  for (qsizetype i = 0; i < scene->swimlanes.size(); ++i) {
    const QJsonObject el = textEls.at(i).toObject();
    const auto& lane = scene->swimlanes.at(i);
    require(lane.label == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/laneText/%1/text").arg(i));
    sameColor(effective(lane.textCss.fill, scene->style.textColor),
              computed(el, "fill"), id + QStringLiteral("/laneText/%1/fill").arg(i));
    const qreal fontSize = lane.textCss.fontSize >= 0.0
                               ? lane.textCss.fontSize
                               : scene->style.rootFontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/laneText/%1/fontSize").arg(i), 0.001);
    const QString weight = lane.textCss.fontWeight.trimmed().isEmpty()
                               ? QStringLiteral("700")
                               : lane.textCss.fontWeight;
    const int weightNumber = weight.compare(QStringLiteral("bold"),
                                            Qt::CaseInsensitive) == 0
                                 ? 700
                                 : weight.toInt();
    require(QString::number(weightNumber) == computed(el, "fontWeight"),
            id + QStringLiteral("/laneText/%1/fontWeight").arg(i));
    require(lane.textCss.visible == browserDisplayed(el),
            id + QStringLiteral("/laneText/%1/visible").arg(i));
    const QJsonObject attrs = el.value(QStringLiteral("attributes")).toObject();
    near(lane.labelPosition.x(),
         attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/laneText/%1/x").arg(i));
    near(lane.labelPosition.y(),
         attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/laneText/%1/y").arg(i));
  }

  // Box labels: foreignObject spans with `color` semantics starting at the
  // initial black.
  const QJsonArray spanEls =
      matches.value(QStringLiteral(".em-box span")).toArray();
  require(spanEls.size() == scene->boxes.size(),
          id + QStringLiteral("/span/count"));
  for (qsizetype i = 0; i < scene->boxes.size(); ++i) {
    const QJsonObject el = spanEls.at(i).toObject();
    const auto& box = scene->boxes.at(i);
    sameColor(effective(box.labelCss.color, QStringLiteral("black")),
              computed(el, "color"),
              id + QStringLiteral("/span/%1/color").arg(i));
    const qreal fontSize = box.labelCss.fontSize >= 0.0
                               ? box.labelCss.fontSize
                               : scene->style.rootFontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/span/%1/fontSize").arg(i), 0.001);
    require(box.labelCss.visible == browserDisplayed(el),
            id + QStringLiteral("/span/%1/visible").arg(i));
    near(box.labelCss.opacity >= 0.0 ? box.labelCss.opacity : 1.0,
         elementOpacity(el), id + QStringLiteral("/span/%1/opacity").arg(i),
         0.01);
  }

  // Relations: straight paths with a shared arrowhead marker.
  const QJsonArray pathEls = matches.value(QStringLiteral("path")).toArray();
  require(pathEls.size() == scene->relations.size(),
          id + QStringLiteral("/path/count"));
  for (qsizetype i = 0; i < scene->relations.size(); ++i) {
    const QJsonObject el = pathEls.at(i).toObject();
    const auto& relation = scene->relations.at(i);
    sameColor(effective(relation.css.stroke, relation.stroke),
              computed(el, "stroke"), id + QStringLiteral("/path/%1/stroke").arg(i));
    near(strokeWidthOf(relation.css, 1.0),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/path/%1/strokeWidth").arg(i), 0.001);
    sameColor(relation.css.fill.isEmpty() ? QStringLiteral("none")
                                          : relation.css.fill,
              computed(el, "fill"), id + QStringLiteral("/path/%1/fill").arg(i));
    require(relation.css.visible == browserDisplayed(el),
            id + QStringLiteral("/path/%1/visible").arg(i));
    compareBox("path", i, relation.css,
               QRectF(QPointF(std::min(relation.line.x1(), relation.line.x2()),
                              std::min(relation.line.y1(), relation.line.y2())),
                      QSizeF(std::abs(relation.line.dx()),
                             std::abs(relation.line.dy()))),
               el);
  }
  const QJsonArray markerEls =
      matches.value(QStringLiteral("marker polygon")).toArray();
  require(markerEls.size() == 1, id + QStringLiteral("/marker/count"));
  sameColor(effective(scene->markerCss.fill, scene->style.arrowhead),
            computed(markerEls.at(0).toObject(), "fill"),
            id + QStringLiteral("/marker/fill"));
  require(scene->markerCss.visible ==
              browserDisplayed(markerEls.at(0).toObject()),
          id + QStringLiteral("/marker/visible"));
}

void compareTreemap(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<treemap::TreemapScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const qreal diagonal = std::hypot(scene->bounds.width(), scene->bounds.height()) /
                         std::sqrt(2.0);
  const auto lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene->style.fontFamily),
      scene->style.rootFontSize);
  const auto strokeWidthOf = [&](const treemap::TreemapElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, QString::number(base) + QStringLiteral("px")),
        lengths, diagonal);
  };
  // The final svg.getBBox + diagramPadding sizes the viewBox; display:none
  // subtrees drop out of it (the title ink box and any group hiding move it).
  const QStringList viewBoxParts = fixture.value(QStringLiteral("viewBox"))
                                       .toString()
                                       .split(QRegularExpression(QStringLiteral("\\s+")));
  require(viewBoxParts.size() == 4, id + QStringLiteral("/viewBox"));
  near(scene->bounds.x(), viewBoxParts.at(0).toDouble(), id + QStringLiteral("/viewBox/x"));
  near(scene->bounds.y(), viewBoxParts.at(1).toDouble(), id + QStringLiteral("/viewBox/y"));
  near(scene->bounds.width(), viewBoxParts.at(2).toDouble(), id + QStringLiteral("/viewBox/w"));
  near(scene->bounds.height(), viewBoxParts.at(3).toDouble(), id + QStringLiteral("/viewBox/h"));

  const auto opacityOf = [](const treemap::TreemapElementCss& css) {
    return css.opacity >= 0.0 ? css.opacity : 1.0;
  };
  const auto compareRectPaint = [&](const char* part, qsizetype index,
                                    const treemap::TreemapElementCss& css,
                                    const QString& fill, qreal fillOpacity,
                                    const QString& stroke, qreal strokeWidth,
                                    qreal strokeOpacity, bool baselineVisible,
                                    const QJsonObject& el) {
    sameColor(effective(css.fill, fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    sameColor(effective(css.stroke, stroke), computed(el, "stroke"),
              id + QStringLiteral("/%1/%2/stroke").arg(QLatin1String(part)).arg(index));
    near(strokeWidthOf(css, strokeWidth),
         computed(el, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/%1/%2/sw").arg(QLatin1String(part)).arg(index),
         0.001);
    near(css.fillOpacity >= 0.0 ? css.fillOpacity : fillOpacity,
         computed(el, "fillOpacity").toDouble(),
         id + QStringLiteral("/%1/%2/fillOpacity").arg(QLatin1String(part)).arg(index),
         0.01);
    near(css.strokeOpacity >= 0.0 ? css.strokeOpacity : strokeOpacity,
         computed(el, "strokeOpacity").toDouble(),
         id + QStringLiteral("/%1/%2/strokeOpacity").arg(QLatin1String(part)).arg(index),
         0.01);
    require((css.visible && baselineVisible) == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(opacityOf(css), elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };
  const auto compareTextPaint = [&](const char* part, qsizetype index,
                                    const treemap::TreemapTextGeometry& text,
                                    const QJsonObject& el) {
    sameColor(effective(text.css.fill, text.fill), computed(el, "fill"),
              id + QStringLiteral("/%1/%2/fill").arg(QLatin1String(part)).arg(index));
    require(text.text == el.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/%1/%2/text").arg(QLatin1String(part)).arg(index));
    // Depth-0 section labels carry only "display: none" inline (upstream
    // skips the label style string for them), so they resolve at the root
    // font size — skip the font lock for hidden texts.
    if (browserDisplayed(el)) {
      const qreal fontSize = text.css.fontSize >= 0.0 ? text.css.fontSize
                                                      : text.fontSize;
      near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/fontSize").arg(QLatin1String(part)).arg(index),
           0.001);
    }
    require((text.visible && text.css.visible) == browserDisplayed(el),
            id + QStringLiteral("/%1/%2/visible").arg(QLatin1String(part)).arg(index));
    near(text.css.opacity >= 0.0 ? text.css.opacity : 1.0, elementOpacity(el),
         id + QStringLiteral("/%1/%2/opacity").arg(QLatin1String(part)).arg(index),
         0.01);
  };

  // Section rects in document order; the fixture array interleaves each
  // section's [header, clipPath rect, body] triple — clipPath rects are not
  // rendered and carry inherited garbage, so filter by parent tag.
  const QJsonArray sectionRectEls =
      matches.value(QStringLiteral(".treemapSection rect")).toArray();
  QVector<QJsonObject> sectionBodyEls;
  QVector<QJsonObject> sectionHeaderEls;
  for (const QJsonValue& value : sectionRectEls) {
    const QJsonObject el = value.toObject();
    if (el.value(QStringLiteral("parentTag")).toString() == QLatin1String("clipPath"))
      continue;
    if (el.value(QStringLiteral("class")).toString() ==
        QLatin1String("treemapSectionHeader"))
      sectionHeaderEls.append(el);
    else
      sectionBodyEls.append(el);
  }
  require(sectionBodyEls.size() == scene->sections.size(),
          id + QStringLiteral("/section/count"));
  require(sectionHeaderEls.size() == scene->sections.size(),
          id + QStringLiteral("/header/count"));
  for (qsizetype i = 0; i < scene->sections.size(); ++i) {
    const auto& section = scene->sections.at(i);
    const QJsonObject body = sectionBodyEls.at(i);
    compareRectPaint("section", i, section.rectCss, section.fill,
                     section.fillOpacity, section.stroke, section.strokeWidth,
                     section.strokeOpacity, section.depth > 0, body);
    // Rect bboxes are local to the translated group; own display:none
    // (depth-0 inline) collapses them to 0x0.
    const QJsonObject bbox = body.value(QStringLiteral("bbox")).toObject();
    const bool collapsed = section.depth == 0 || !section.rectCss.measures;
    near(collapsed ? 0.0 : section.rect.width(),
         bbox.value(QStringLiteral("width")).toDouble(),
         id + QStringLiteral("/section/%1/w").arg(i));
    near(collapsed ? 0.0 : section.rect.height(),
         bbox.value(QStringLiteral("height")).toDouble(),
         id + QStringLiteral("/section/%1/h").arg(i));
  }
  const QJsonArray sectionLabelEls =
      matches.value(QStringLiteral(".treemapSectionLabel")).toArray();
  const QJsonArray sectionValueEls =
      matches.value(QStringLiteral(".treemapSectionValue")).toArray();
  require(sectionLabelEls.size() == scene->sections.size(),
          id + QStringLiteral("/sectionLabel/count"));
  require(sectionValueEls.size() == scene->sections.size(),
          id + QStringLiteral("/sectionValue/count"));
  for (qsizetype i = 0; i < scene->sections.size(); ++i) {
    compareTextPaint("sectionLabel", i, scene->sections.at(i).label,
                     sectionLabelEls.at(i).toObject());
    compareTextPaint("sectionValue", i, scene->sections.at(i).value,
                     sectionValueEls.at(i).toObject());
  }

  const QJsonArray leafEls = matches.value(QStringLiteral(".treemapLeaf")).toArray();
  const QJsonArray leafLabelEls =
      matches.value(QStringLiteral(".treemapLabel")).toArray();
  const QJsonArray leafValueEls =
      matches.value(QStringLiteral(".treemapValue")).toArray();
  require(leafEls.size() == scene->leaves.size(),
          id + QStringLiteral("/leaf/count"));
  require(leafLabelEls.size() == scene->leaves.size(),
          id + QStringLiteral("/leafLabel/count"));
  require(leafValueEls.size() == scene->leaves.size(),
          id + QStringLiteral("/leafValue/count"));
  for (qsizetype i = 0; i < scene->leaves.size(); ++i) {
    const auto& leaf = scene->leaves.at(i);
    const QJsonObject el = leafEls.at(i).toObject();
    compareRectPaint("leaf", i, leaf.rectCss, leaf.fill, leaf.fillOpacity,
                     leaf.stroke, leaf.strokeWidth, 1.0, true, el);
    const QJsonObject bbox = el.value(QStringLiteral("bbox")).toObject();
    near(leaf.rect.width(), bbox.value(QStringLiteral("width")).toDouble(),
         id + QStringLiteral("/leaf/%1/w").arg(i));
    near(leaf.rect.height(), bbox.value(QStringLiteral("height")).toDouble(),
         id + QStringLiteral("/leaf/%1/h").arg(i));
    compareTextPaint("leafLabel", i, leaf.label, leafLabelEls.at(i).toObject());
    compareTextPaint("leafValue", i, leaf.value, leafValueEls.at(i).toObject());
  }

  const QJsonArray titleEls =
      matches.value(QStringLiteral(".treemapTitle")).toArray();
  require(titleEls.size() == (scene->title.text.isEmpty() ? 0 : 1),
          id + QStringLiteral("/title/count"));
  if (!scene->title.text.isEmpty()) {
    const QJsonObject el = titleEls.at(0).toObject();
    sameColor(effective(scene->title.css.fill, scene->title.fill),
              computed(el, "fill"), id + QStringLiteral("/title/fill"));
    const qreal fontSize = scene->title.css.fontSize >= 0.0
                               ? scene->title.css.fontSize
                               : scene->title.fontSize;
    near(fontSize, computed(el, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/title/fontSize"), 0.001);
    near(scene->title.css.opacity >= 0.0 ? scene->title.css.opacity : 1.0,
         elementOpacity(el), id + QStringLiteral("/title/opacity"), 0.01);
    require(scene->title.css.visible == browserDisplayed(el),
            id + QStringLiteral("/title/visible"));
  }
}

void compareKanban(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<kanban::KanbanScene>(fixture, entry);
  // Chrome quantizes the laid-out svg to 1/64px (LayoutUnit floor) while the
  // native naturalSize ceils the exact fractional bounds; the two integers
  // agree under ceil quantization because ceil(floor64(h)) == ceil(h).
  const QJsonObject kanbanClient =
      fixture.value(QStringLiteral("client")).toObject();
  const QColor kanbanRoot = color::resolveSvgPaint(
      scene->style.textColor, color::SvgPaintKind::Fill, QColor(Qt::black))
                                .color;
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto strokeWidthOf = [&](const kanban::KanbanElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth,
                  QString::number(base) + QStringLiteral("px")),
        editor::pieCssLengthContext(scene->style.fontFamily,
                                    scene->style.fontSize),
        std::hypot(scene->bounds.width(), scene->bounds.height()) /
            std::sqrt(2.0));
  };

  // Section boxes: `.sections rect` records in scene.sections order.
  const QJsonArray sectionRects =
      fixture.value(QStringLiteral("matches")).toObject()
          .value(QStringLiteral(".sections rect")).toArray();
  require(sectionRects.size() == scene->sections.size(),
          id + QStringLiteral("/section/count"));
  for (qsizetype i = 0; i < scene->sections.size(); ++i) {
    const kanban::KanbanSectionGeometry& section = scene->sections.at(i);
    const QJsonObject box = sectionRects.at(i).toObject();
    const kanban::KanbanElementCss& css = section.boxCss;
    sameColor(effective(css.fill, section.fill), computed(box, "fill"),
              id + QStringLiteral("/section/%1/fill").arg(i));
    sameColor(effective(css.stroke, section.stroke), computed(box, "stroke"),
              id + QStringLiteral("/section/%1/stroke").arg(i));
    near(strokeWidthOf(css, section.strokeWidth),
         computed(box, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/section/%1/strokeWidth").arg(i), 0.001);
    require(css.visible == browserDisplayed(box),
            id + QStringLiteral("/section/%1/visible").arg(i));
    if (css.hasBox) {
      const QJsonObject client =
          box.value(QStringLiteral("client")).toObject();
      near(section.shapeBounds.height(),
           client.value(QStringLiteral("height")).toDouble(),
           id + QStringLiteral("/section/%1/height").arg(i));
    }
  }

  // Labels: html spans in document order — section labels first, then each
  // item's title/ticket/assigned.
  const QJsonArray spans =
      fixture.value(QStringLiteral("matches")).toObject()
          .value(QStringLiteral("span")).toArray();
  QVector<const kanban::KanbanLabelGeometry*> labels;
  for (const kanban::KanbanSectionGeometry& section : scene->sections)
    labels.append(&section.label);
  for (const kanban::KanbanItemGeometry& item : scene->items) {
    labels.append(&item.title);
    labels.append(&item.ticket);
    labels.append(&item.assigned);
  }
  require(spans.size() == labels.size(),
          id + QStringLiteral("/span/count"));
  for (qsizetype i = 0; i < labels.size(); ++i) {
    const kanban::KanbanLabelGeometry* label = labels.at(i);
    const kanban::KanbanElementCss& css = label->css;
    const QJsonObject span = spans.at(i).toObject();
    const QColor color(color::resolveSvgPaint(
        effective(css.color, label->fill), color::SvgPaintKind::Text,
        kanbanRoot).color);
    sameColor(color, computed(span, "color"),
              id + QStringLiteral("/span/%1/color").arg(i));
    near(label->fontSize, computed(span, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/span/%1/fontSize").arg(i), 0.001);
    const QString weight = css.fontWeight.isEmpty()
                               ? QStringLiteral("400")
                               : css.fontWeight;
    require(weight == computed(span, "fontWeight"),
            id + QStringLiteral("/span/%1/fontWeight").arg(i));
    require(css.visible == browserDisplayed(span),
            id + QStringLiteral("/span/%1/visible").arg(i));
  }

  // Item cards: `.items rect` lists the label-container first and three
  // zero-sized label background rects per card; all four share the `.node
  // rect` selector surface.
  const QJsonArray itemRects =
      fixture.value(QStringLiteral("matches")).toObject()
          .value(QStringLiteral(".items rect")).toArray();
  require(itemRects.size() == scene->items.size() * 4,
          id + QStringLiteral("/item-rect/count"));
  for (qsizetype i = 0; i < scene->items.size(); ++i) {
    const kanban::KanbanItemGeometry& item = scene->items.at(i);
    const kanban::KanbanElementCss& css = item.boxCss;
    for (int k = 0; k < 4; ++k) {
      const QJsonObject box = itemRects.at(i * 4 + k).toObject();
      sameColor(effective(css.fill, item.fill), computed(box, "fill"),
                id + QStringLiteral("/item/%1/%2/fill").arg(i).arg(k));
      sameColor(effective(css.stroke, item.stroke), computed(box, "stroke"),
                id + QStringLiteral("/item/%1/%2/stroke").arg(i).arg(k));
    }
    const QJsonObject box = itemRects.at(i * 4).toObject();
    near(strokeWidthOf(css, item.strokeWidth),
         computed(box, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/item/%1/strokeWidth").arg(i), 0.001);
    require(css.visible == browserDisplayed(box),
            id + QStringLiteral("/item/%1/visible").arg(i));
  }

  // The priority strike line of the single high-priority card.
  const QJsonArray priorityLines =
      fixture.value(QStringLiteral("matches")).toObject()
          .value(QStringLiteral(".items line")).toArray();
  const kanban::KanbanItemGeometry* priorityItem = nullptr;
  for (const kanban::KanbanItemGeometry& item : scene->items)
    if (item.priorityVisible) priorityItem = &item;
  require(priorityLines.size() == (priorityItem ? 1 : 0),
          id + QStringLiteral("/priority/count"));
  if (priorityItem) {
    const QJsonObject line = priorityLines.at(0).toObject();
    sameColor(effective(priorityItem->priorityCss.stroke,
                        priorityItem->priorityStroke),
              computed(line, "stroke"),
              id + QStringLiteral("/priority/stroke"));
    near(strokeWidthOf(priorityItem->priorityCss,
                       priorityItem->priorityStrokeWidth),
         computed(line, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/priority/strokeWidth"), 0.001);
    require(priorityItem->priorityCss.visible == browserDisplayed(line),
            id + QStringLiteral("/priority/visible"));
  }

  near(entry.naturalSize.width(),
       std::ceil(kanbanClient.value(QStringLiteral("width")).toDouble()),
       id + QStringLiteral("/client/width"));
  near(entry.naturalSize.height(),
       std::ceil(kanbanClient.value(QStringLiteral("height")).toDouble()),
       id + QStringLiteral("/client/height"));
}

void compareTimeline(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<timeline::TimelineScene>(fixture, entry);
  const QJsonObject clientSize = fixture.value(QStringLiteral("client")).toObject();
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  // The root svg paints textColor; the timeline DOM never lets a rule reach
  // the root itself, so this is also the marker path's inherited fill.
  const QColor root = color::resolveSvgPaint(
                          scene->style.textColor, color::SvgPaintKind::Fill,
                          QColor(Qt::black)).color;
  const QColor inherited = root;
  const auto effective = [](const QString& value, const QString& base) {
    return value.isEmpty() ? base : value;
  };
  const auto colorOf = [](const timeline::TimelineElementCss& css) {
    const QString raw = css.color.trimmed();
    return color::isParsableColor(raw) ? color::toQColor(raw)
                                       : QColor(Qt::black);
  };
  const auto strokeWidthOf = [&](const timeline::TimelineElementCss& css,
                                 qreal base) {
    return editor::cssStrokeWidthPx(
        effective(css.strokeWidth, QString::number(base) + QStringLiteral("px")),
        editor::pieCssLengthContext(scene->style.fontFamily,
                                    scene->style.fontSize),
        std::hypot(scene->bounds.width(), scene->bounds.height()) /
            std::sqrt(2.0));
  };
  const auto assertStroke = [&](const timeline::TimelinePaintState& paint,
                                const QJsonObject& element,
                                const QString& what) {
    if (paint.none) {
      require(computed(element, "stroke") == QLatin1String("none"),
              id + QStringLiteral("/") + what + QStringLiteral("/stroke-none"));
    } else {
      sameColor(paint.color, computed(element, "stroke"),
                id + QStringLiteral("/") + what + QStringLiteral("/stroke"));
    }
  };

  // Node boxes: `.node-bkg` matches in scene.nodes order (sections, tasks and
  // events interleaved the way the renderer appends them).
  const QJsonArray boxes = matches.value(QStringLiteral(".node-bkg")).toArray();
  const QJsonArray nodeTexts =
      matches.value(QStringLiteral(".timeline-node text")).toArray();
  const QJsonArray dividers =
      matches.value(QStringLiteral(".timeline-node line")).toArray();
  require(boxes.size() == scene->nodes.size() &&
              nodeTexts.size() == scene->nodes.size() &&
              dividers.size() == scene->nodes.size(),
          id + QStringLiteral("/node/count"));
  for (qsizetype i = 0; i < scene->nodes.size(); ++i) {
    const timeline::TimelineNodeGeometry& node = scene->nodes.at(i);
    const QJsonObject box = boxes.at(i).toObject();
    // Geometry: the client height maps 1:1 to the native node height, and
    // relative y offsets track the font-driven re-measure coupling. A
    // display:none box reports a zero client rect while the native layout
    // keeps its numbers.
    const QJsonObject boxClient =
        box.value(QStringLiteral("client")).toObject();
    if (node.boxCss.hasBox) {
      const QJsonObject firstClient =
          boxes.at(0).toObject().value(QStringLiteral("client")).toObject();
      near(node.height, boxClient.value(QStringLiteral("height")).toDouble(),
           id + QStringLiteral("/node/%1/height").arg(i));
      near(node.position.y() - scene->nodes.first().position.y(),
           boxClient.value(QStringLiteral("y")).toDouble() -
               firstClient.value(QStringLiteral("y")).toDouble(),
           id + QStringLiteral("/node/%1/y").arg(i));
    }
    const timeline::TimelinePaintState fill = timeline::timelineElementFill(
        effective(node.boxCss.fill, node.fill), root, colorOf(node.boxCss));
    if (fill.none) {
      require(computed(box, "fill") == QLatin1String("none"),
              id + QStringLiteral("/node/%1/fill-none").arg(i));
    } else {
      sameColor(fill.color, computed(box, "fill"),
                id + QStringLiteral("/node/%1/fill").arg(i));
    }
    const timeline::TimelinePaintState stroke =
        !node.boxCss.stroke.isEmpty()
            ? timeline::timelineLineStroke(node.boxCss.stroke,
                                           QColor(Qt::black), inherited)
            : color::resolveSvgPaint(node.stroke,
                                     color::SvgPaintKind::Stroke, inherited);
    assertStroke(stroke, box, QStringLiteral("node/%1").arg(i));
    near(strokeWidthOf(node.boxCss, node.strokeWidth),
         computed(box, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/node/%1/strokeWidth").arg(i), 0.001);
    require(node.boxCss.visible == browserDisplayed(box),
            id + QStringLiteral("/node/%1/box-visible").arg(i));

    const QJsonObject text = nodeTexts.at(i).toObject();
    const timeline::TimelinePaintState textFill = timeline::timelineElementFill(
        effective(node.textCss.fill, node.textFill), root, colorOf(node.textCss));
    if (textFill.none) {
      require(computed(text, "fill") == QLatin1String("none"),
              id + QStringLiteral("/text/%1/fill-none").arg(i));
    } else {
      sameColor(textFill.color, computed(text, "fill"),
                id + QStringLiteral("/text/%1/fill").arg(i));
    }
    near(node.textCss.fontSize >= 0.0 ? node.textCss.fontSize
                                      : scene->style.fontSize,
         computed(text, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/text/%1/fontSize").arg(i), 0.001);
    const QString weight = node.textCss.fontWeight.isEmpty()
                               ? QStringLiteral("400")
                               : node.textCss.fontWeight;
    require(weight == computed(text, "fontWeight"),
            id + QStringLiteral("/text/%1/fontWeight").arg(i));
    require(node.textCss.visible == browserDisplayed(text),
            id + QStringLiteral("/text/%1/visible").arg(i));

    const QJsonObject divider = dividers.at(i).toObject();
    assertStroke(timeline::timelineLineStroke(
                     effective(node.dividerCss.stroke, node.dividerStroke),
                     QColor(Qt::black), inherited),
                 divider, QStringLiteral("divider/%1").arg(i));
    near(strokeWidthOf(node.dividerCss, node.dividerWidth),
         computed(divider, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/divider/%1/strokeWidth").arg(i), 0.001);
    require(node.dividerCss.visible == browserDisplayed(divider),
            id + QStringLiteral("/divider/%1/visible").arg(i));
  }

  // Connector lines and the axis share `.lineWrapper line`; both scene.lines
  // and the DOM enumerate connectors first, the axis last.
  const QJsonArray lines =
      matches.value(QStringLiteral(".lineWrapper line")).toArray();
  require(lines.size() == scene->lines.size(),
          id + QStringLiteral("/line/count"));
  for (qsizetype i = 0; i < scene->lines.size(); ++i) {
    const timeline::TimelineLineGeometry& line = scene->lines.at(i);
    const QJsonObject element = lines.at(i).toObject();
    assertStroke(timeline::timelineLineStroke(
                     effective(line.css.stroke, line.stroke),
                     QColor(Qt::black), inherited),
                 element, QStringLiteral("line/%1").arg(i));
    near(strokeWidthOf(line.css, line.strokeWidth),
         computed(element, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/line/%1/strokeWidth").arg(i), 0.001);
    require(line.css.visible == browserDisplayed(element),
            id + QStringLiteral("/line/%1/visible").arg(i));
  }

  // Title: the classless svg-level text record.
  const QJsonObject title = [&]() {
    for (const QJsonValue& value :
         matches.value(QStringLiteral("text")).toArray()) {
      const QJsonObject element = value.toObject();
      if (element.value(QStringLiteral("class")).toString().isEmpty() &&
          element.value(QStringLiteral("parentTag")).toString() ==
              QLatin1String("svg"))
        return element;
    }
    return QJsonObject();
  }();
  require(!title.isEmpty(), id + QStringLiteral("/title/missing"));
  const timeline::TimelinePaintState titleFill = timeline::timelineElementFill(
      effective(scene->titleGeometry.css.fill, scene->titleGeometry.fill),
      root, colorOf(scene->titleGeometry.css));
  require(!titleFill.none, id + QStringLiteral("/title/fill-none"));
  sameColor(titleFill.color, computed(title, "fill"),
            id + QStringLiteral("/title/fill"));
  near(scene->titleGeometry.fontSize,
       computed(title, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/title/fontSize"), 0.51);
  require(scene->titleGeometry.css.visible == browserDisplayed(title),
          id + QStringLiteral("/title/visible"));

  // Chrome quantizes the laid-out svg to 1/64px (LayoutUnit floor) while the
  // native naturalSize ceils the exact fractional bounds; the two integers
  // agree under ceil quantization because ceil(floor64(h)) == ceil(h).
  near(entry.naturalSize.width(),
       std::ceil(clientSize.value(QStringLiteral("width")).toDouble()),
       id + QStringLiteral("/client/width"));
  near(entry.naturalSize.height(),
       std::ceil(clientSize.value(QStringLiteral("height")).toDouble()),
       id + QStringLiteral("/client/height"));

  // The arrowhead path carries no fill attribute: it paints the root fill.
  const QJsonObject marker = matches.value(QStringLiteral("path"))
                                 .toArray().first().toObject();
  require(marker.value(QStringLiteral("parentTag")).toString() ==
              QLatin1String("marker"),
          id + QStringLiteral("/marker/first-path"));
  sameColor(root, computed(marker, "fill"),
            id + QStringLiteral("/marker/fill"));
}

void compareMindmap(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<mindmap::MindmapScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  QHash<QString, const mindmap::MindmapNodeGeometry*> nodes;
  for (const auto& node : scene->nodes)
    nodes.insert(QString::number(node.id), &node);
  for (const QString& selector : {QStringLiteral(".node circle"),
                                  QStringLiteral(".node path")}) {
    for (const QJsonValue& value : matches.value(selector).toArray()) {
      const QJsonObject shape = value.toObject();
      const QString nodeId = mindmapSemanticId(
          shape.value(QStringLiteral("ownerNodeId")).toString());
      const auto* node = nodes.value(nodeId, nullptr);
      require(node, id + QStringLiteral(": missing mindmap node ") + nodeId);
      sameColor(node->fill, computed(shape, "fill"),
                id + QStringLiteral("/") + nodeId + QStringLiteral("/fill"));
      sameColor(node->stroke, computed(shape, "stroke"),
                id + QStringLiteral("/") + nodeId + QStringLiteral("/stroke"));
      near(node->strokeWidth,
           computed(shape, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/") + nodeId + QStringLiteral("/strokeWidth"),
           0.001);
    }
  }
  for (const QJsonValue& value : matches.value(QStringLiteral(".nodeLabel")).toArray()) {
    const QJsonObject label = value.toObject();
    const QString nodeId = mindmapSemanticId(
        label.value(QStringLiteral("ownerNodeId")).toString());
    const auto* node = nodes.value(nodeId, nullptr);
    require(node, id + QStringLiteral(": missing mindmap label ") + nodeId);
    sameColor(node->label.fill, computed(label, "color"),
              id + QStringLiteral("/") + nodeId + QStringLiteral("/label/color"));
    near(node->label.fontSize,
         computed(label, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/") + nodeId + QStringLiteral("/label/fontSize"),
         0.001);
  }
}

void compareInfo(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<info::InfoScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject text = fixture.value(QStringLiteral("matches")).toObject()
                               .value(QStringLiteral(".version"))
                               .toArray().first().toObject();
  const bool browserVisible =
      computed(text, "display") != QLatin1String("none") &&
      computed(text, "visibility") != QLatin1String("hidden") &&
      computed(text, "visibility") != QLatin1String("collapse");
  require(scene->style.textVisible == browserVisible,
          id + QStringLiteral("/text/visible"));
  sameColor(scene->style.textColor, computed(text, "fill"),
            id + QStringLiteral("/text/fill"));
  near(scene->style.fontSize,
       computed(text, "fontSize").chopped(2).toDouble(),
       id + QStringLiteral("/text/fontSize"), 0.001);
  require(QString::number(int(scene->style.fontWeight)) ==
              QString::number(computed(text, "fontWeight").toInt()),
          id + QStringLiteral("/text/fontWeight"));
  near(scene->style.opacity, computed(text, "opacity").toDouble(),
       id + QStringLiteral("/text/opacity"), 0.001);
}

void compareQuadrant(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<quadrant::QuadrantScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonArray rects = matches.value(QStringLiteral(".quadrant rect")).toArray();
  const QJsonArray quadrantTexts = matches.value(QStringLiteral(".quadrant text")).toArray();
  require(rects.size() == scene->quadrants.size() &&
              quadrantTexts.size() == scene->quadrants.size(),
          id + QStringLiteral(": quadrant count"));
  for (qsizetype i = 0; i < scene->quadrants.size(); ++i) {
    const auto& q = scene->quadrants.at(i);
    const QJsonObject rect = rects.at(i).toObject();
    const QJsonObject text = quadrantTexts.at(i).toObject();
    sameColor(q.fill, computed(rect, "fill"),
              id + QStringLiteral("/quadrant/%1/fill").arg(i));
    require(q.shapeVisible ==
                (computed(rect, "display") != QLatin1String("none") &&
                 computed(rect, "visibility") != QLatin1String("hidden")),
            id + QStringLiteral("/quadrant/%1/visible").arg(i));
    sameColor(q.textFill, computed(text, "fill"),
              id + QStringLiteral("/quadrant/%1/text/fill").arg(i));
    near(q.textFontSize, computed(text, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/quadrant/%1/text/fontSize").arg(i), 0.001);
  }
  const QJsonArray lines = matches.value(QStringLiteral(".border line")).toArray();
  require(lines.size() == scene->borders.size(), id + QStringLiteral(": border count"));
  for (qsizetype i = 0; i < scene->borders.size(); ++i) {
    sameColor(scene->borders.at(i).strokeFill,
              computed(lines.at(i).toObject(), "stroke"),
              id + QStringLiteral("/border/%1/stroke").arg(i));
    near(scene->borders.at(i).strokeWidth,
         computed(lines.at(i).toObject(), "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/border/%1/strokeWidth").arg(i), 0.001);
  }
  const QJsonArray circles = matches.value(QStringLiteral(".data-point circle")).toArray();
  const QJsonArray pointTexts = matches.value(QStringLiteral(".data-point text")).toArray();
  require(circles.size() == scene->points.size() &&
              pointTexts.size() == scene->points.size(),
          id + QStringLiteral(": point count"));
  for (qsizetype i = 0; i < scene->points.size(); ++i) {
    const auto& point = scene->points.at(i);
    const QJsonObject circle = circles.at(i).toObject();
    const QJsonObject text = pointTexts.at(i).toObject();
    sameColor(point.fill, computed(circle, "fill"),
              id + QStringLiteral("/point/%1/fill").arg(i));
    sameColor(point.stroke, computed(circle, "stroke"),
              id + QStringLiteral("/point/%1/stroke").arg(i));
    near(point.strokeWidth, computed(circle, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/point/%1/strokeWidth").arg(i), 0.001);
    sameColor(point.textFill, computed(text, "fill"),
              id + QStringLiteral("/point/%1/text/fill").arg(i));
    near(point.textFontSize, computed(text, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/point/%1/text/fontSize").arg(i), 0.001);
  }
  const QJsonArray labels = matches.value(QStringLiteral(".labels text")).toArray();
  require(labels.size() == scene->axisLabels.size(), id + QStringLiteral(": label count"));
  for (qsizetype i = 0; i < scene->axisLabels.size(); ++i) {
    sameColor(scene->axisLabels.at(i).fill,
              computed(labels.at(i).toObject(), "fill"),
              id + QStringLiteral("/label/%1/fill").arg(i));
    near(scene->axisLabels.at(i).fontSize,
         computed(labels.at(i).toObject(), "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/fontSize").arg(i), 0.001);
  }
  const QJsonArray titles = matches.value(QStringLiteral(".title text")).toArray();
  if (!titles.isEmpty()) {
    const QJsonObject title = titles.first().toObject();
    sameColor(scene->titleFill, computed(title, "fill"), id + QStringLiteral("/title/fill"));
    near(scene->titleFontSizeCfg,
         computed(title, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/title/fontSize"), 0.001);
  }
}

void compareRadar(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<radar::RadarScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonArray rings = matches.value(QStringLiteral(".radarGraticule")).toArray();
  require(rings.size() == scene->graticules.size(), id + QStringLiteral(": ring count"));
  for (qsizetype i = 0; i < scene->graticules.size(); ++i) {
    const auto& ring = scene->graticules.at(i);
    const QJsonObject element = rings.at(i).toObject();
    sameColor(ring.fill, computed(element, "fill"),
              id + QStringLiteral("/ring/%1/fill").arg(i));
    sameColor(ring.stroke, computed(element, "stroke"),
              id + QStringLiteral("/ring/%1/stroke").arg(i));
    near(ring.strokeWidth, computed(element, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/ring/%1/strokeWidth").arg(i), 0.001);
  }
  const QJsonArray lines = matches.value(QStringLiteral(".radarAxisLine")).toArray();
  const QJsonArray labels = matches.value(QStringLiteral(".radarAxisLabel")).toArray();
  require(lines.size() == scene->axes.size() && labels.size() == scene->axes.size(),
          id + QStringLiteral(": axis count"));
  for (qsizetype i = 0; i < scene->axes.size(); ++i) {
    const auto& axis = scene->axes.at(i);
    sameColor(axis.lineStroke, computed(lines.at(i).toObject(), "stroke"),
              id + QStringLiteral("/axis/%1/stroke").arg(i));
    near(axis.lineStrokeWidth,
         computed(lines.at(i).toObject(), "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/axis/%1/strokeWidth").arg(i), 0.001);
    sameColor(axis.labelFill, computed(labels.at(i).toObject(), "fill"),
              id + QStringLiteral("/axis/%1/label/fill").arg(i));
    near(axis.labelFontSize,
         computed(labels.at(i).toObject(), "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/axis/%1/label/fontSize").arg(i), 0.001);
  }
  const QJsonArray curves = matches.value(QStringLiteral("[class^=radarCurve-]")).toArray();
  require(curves.size() == scene->curves.size(), id + QStringLiteral(": curve count"));
  for (qsizetype i = 0; i < scene->curves.size(); ++i) {
    const auto& curve = scene->curves.at(i);
    const QJsonObject element = curves.at(i).toObject();
    sameColor(curve.fill, computed(element, "fill"),
              id + QStringLiteral("/curve/%1/fill").arg(i));
    sameColor(curve.stroke, computed(element, "stroke"),
              id + QStringLiteral("/curve/%1/stroke").arg(i));
    near(curve.strokeWidth, computed(element, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/curve/%1/strokeWidth").arg(i), 0.001);
    require(curve.visible ==
                (computed(element, "display") != QLatin1String("none") &&
                 computed(element, "visibility") != QLatin1String("hidden")),
            id + QStringLiteral("/curve/%1/visible").arg(i));
  }
  const QJsonArray boxes = matches.value(QStringLiteral("[class^=radarLegendBox-]")).toArray();
  const QJsonArray legendTexts = matches.value(QStringLiteral(".radarLegendText")).toArray();
  require(boxes.size() == scene->legends.size() &&
              legendTexts.size() == scene->legends.size(),
          id + QStringLiteral(": legend count"));
  for (qsizetype i = 0; i < scene->legends.size(); ++i) {
    const auto& legend = scene->legends.at(i);
    sameColor(legend.boxFill, computed(boxes.at(i).toObject(), "fill"),
              id + QStringLiteral("/legend/%1/box/fill").arg(i));
    sameColor(legend.textFill, computed(legendTexts.at(i).toObject(), "fill"),
              id + QStringLiteral("/legend/%1/text/fill").arg(i));
    near(legend.textFontSize,
         computed(legendTexts.at(i).toObject(), "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/legend/%1/text/fontSize").arg(i), 0.001);
  }
  const QJsonArray titles = matches.value(QStringLiteral(".radarTitle")).toArray();
  if (!titles.isEmpty()) {
    const QJsonObject title = titles.first().toObject();
    sameColor(scene->titleFill, computed(title, "fill"), id + QStringLiteral("/title/fill"));
    near(scene->titleFontSize, computed(title, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/title/fontSize"), 0.001);
  }
}

template <typename T>
QVector<const T*> xyGroup(const QVector<T>& values, const QString& group) {
  QVector<const T*> result;
  for (const T& value : values)
    if (value.group == group) result.append(&value);
  return result;
}

void compareXYChart(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<xychart::XYChartScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonObject background =
      matches.value(QStringLiteral(".background")).toArray().first().toObject();
  sameColor(scene->style.backgroundColor, computed(background, "fill"),
            id + QStringLiteral("/background/fill"));
  near(scene->style.backgroundOpacity,
       computed(background, "opacity").toDouble() *
           computed(background, "fillOpacity").toDouble(),
       id + QStringLiteral("/background/opacity"), 0.001);

  const auto compareRects = [&](const QString& selector,
                                const QString& group) {
    const QJsonArray expected = matches.value(selector).toArray();
    const auto actual = xyGroup(scene->rects, group);
    require(actual.size() == expected.size(), id + QLatin1Char('/') + selector);
    for (qsizetype i = 0; i < actual.size(); ++i) {
      const QJsonObject value = expected.at(i).toObject();
      sameColor(actual.at(i)->fill, computed(value, "fill"),
                id + QStringLiteral("/%1/%2/fill").arg(selector).arg(i));
      sameColor(actual.at(i)->stroke, computed(value, "stroke"),
                id + QStringLiteral("/%1/%2/stroke").arg(selector).arg(i));
      near(actual.at(i)->strokeWidth,
           computed(value, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/strokeWidth").arg(selector).arg(i), 0.001);
      near(actual.at(i)->fillOpacity,
           computed(value, "opacity").toDouble() *
               computed(value, "fillOpacity").toDouble(),
           id + QStringLiteral("/%1/%2/fillOpacity").arg(selector).arg(i), 0.001);
    }
  };
  const auto comparePaths = [&](const QString& selector,
                                const QString& group) {
    const QJsonArray expected = matches.value(selector).toArray();
    const auto actual = xyGroup(scene->paths, group);
    require(actual.size() == expected.size(), id + QLatin1Char('/') + selector);
    for (qsizetype i = 0; i < actual.size(); ++i) {
      const QJsonObject value = expected.at(i).toObject();
      sameColor(actual.at(i)->stroke, computed(value, "stroke"),
                id + QStringLiteral("/%1/%2/stroke").arg(selector).arg(i));
      near(actual.at(i)->strokeWidth,
           computed(value, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/strokeWidth").arg(selector).arg(i), 0.001);
    }
  };
  const auto compareTexts = [&](const QString& selector,
                                const QString& group) {
    const QJsonArray expected = matches.value(selector).toArray();
    const auto actual = xyGroup(scene->texts, group);
    require(actual.size() == expected.size(), id + QLatin1Char('/') + selector);
    for (qsizetype i = 0; i < actual.size(); ++i) {
      const QJsonObject value = expected.at(i).toObject();
      sameColor(actual.at(i)->fill, computed(value, "fill"),
                id + QStringLiteral("/%1/%2/fill").arg(selector).arg(i));
      near(actual.at(i)->fontSize,
           computed(value, "fontSize").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/fontSize").arg(selector).arg(i), 0.001);
      require(actual.at(i)->visible ==
                  (computed(value, "display") != QLatin1String("none") &&
                   computed(value, "visibility") != QLatin1String("hidden") &&
                   computed(value, "visibility") != QLatin1String("collapse")),
              id + QStringLiteral("/%1/%2/visible").arg(selector).arg(i));
    }
  };

  if (id == QLatin1String("xychart-paint")) {
    compareRects(QStringLiteral(".bar-plot-0 rect"),
                 QStringLiteral("plot/bar-plot-0"));
    compareTexts(QStringLiteral(".bar-plot-0 text"),
                 QStringLiteral("plot/bar-plot-0"));
    comparePaths(QStringLiteral(".line-plot-1 path"),
                 QStringLiteral("plot/line-plot-1"));
    compareTexts(QStringLiteral(".line-plot-1 .labels text"),
                 QStringLiteral("plot/line-plot-1/labels"));
    compareTexts(QStringLiteral(".bottom-axis .label text"),
                 QStringLiteral("x-axis/label"));
    comparePaths(QStringLiteral(".bottom-axis .axis-line path"),
                 QStringLiteral("x-axis/axis-line"));
    compareTexts(QStringLiteral(".left-axis .title text"),
                 QStringLiteral("y-axis/title"));
    comparePaths(QStringLiteral(".left-axis .ticks path"),
                 QStringLiteral("y-axis/ticks"));
    compareTexts(QStringLiteral(".chart-title text"),
                 QStringLiteral("chart-title"));
  } else {
    for (const auto& rect : scene->rects)
      require(!rect.visible, id + QStringLiteral("/plot/rect/hidden"));
    for (const auto& path : scene->paths)
      if (path.group.startsWith(QLatin1String("plot/")))
        require(!path.visible, id + QStringLiteral("/plot/path/hidden"));
    compareTexts(QStringLiteral(".bottom-axis .label text"),
                 QStringLiteral("x-axis/label"));
  }
}

void compareSankey(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<sankey::SankeyScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonArray rects = matches.value(QStringLiteral(".node rect")).toArray();
  require(rects.size() == scene->nodes.size(), id + QStringLiteral("/nodes/count"));
  for (qsizetype i = 0; i < scene->nodes.size(); ++i) {
    const auto& node = scene->nodes.at(i);
    const QJsonObject rect = rects.at(i).toObject();
    sameColor(node.color, computed(rect, "fill"),
              id + QStringLiteral("/node/%1/fill").arg(i));
    sameColor(node.stroke, computed(rect, "stroke"),
              id + QStringLiteral("/node/%1/stroke").arg(i));
    near(node.strokeWidth,
         computed(rect, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/node/%1/strokeWidth").arg(i), 0.001);
  }
  const QJsonArray texts = matches.value(QStringLiteral(".node-labels text")).toArray();
  require(texts.size() == scene->labels.size(), id + QStringLiteral("/labels/count"));
  for (qsizetype i = 0; i < scene->labels.size(); ++i) {
    const auto& label = scene->labels.at(i);
    const QJsonObject text = texts.at(i).toObject();
    sameColor(label.fill, computed(text, "fill"),
              id + QStringLiteral("/label/%1/fill").arg(i));
    sameColor(label.stroke, computed(text, "stroke"),
              id + QStringLiteral("/label/%1/stroke").arg(i));
    near(label.strokeWidth,
         computed(text, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/strokeWidth").arg(i), 0.001);
    near(label.fontSize, computed(text, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/fontSize").arg(i), 0.001);
    require(label.visible ==
                (computed(text, "display") != QLatin1String("none") &&
                 computed(text, "visibility") != QLatin1String("hidden") &&
                 computed(text, "visibility") != QLatin1String("collapse")),
            id + QStringLiteral("/label/%1/visible").arg(i));
  }
  const QJsonArray links = matches.value(QStringLiteral(".link path")).toArray();
  require(links.size() == scene->links.size(), id + QStringLiteral("/links/count"));
  for (qsizetype i = 0; i < scene->links.size(); ++i) {
    const auto& link = scene->links.at(i);
    const QJsonObject path = links.at(i).toObject();
    if (!computed(path, "stroke").startsWith(QLatin1String("url(")))
      sameColor(link.stroke, computed(path, "stroke"),
                id + QStringLiteral("/link/%1/stroke").arg(i));
    near(link.width, computed(path, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/link/%1/strokeWidth").arg(i), 0.001);
    near(link.opacity,
         computed(path, "opacity").toDouble() *
             computed(path, "strokeOpacity").toDouble(),
         id + QStringLiteral("/link/%1/opacity").arg(i), 0.001);
  }
  const QJsonObject browserBox = fixture.value(QStringLiteral("bbox")).toObject();
  near(scene->bounds.x(), browserBox.value(QStringLiteral("x")).toDouble(),
       id + QStringLiteral("/bbox/x"), 0.1);
  near(scene->bounds.width(), browserBox.value(QStringLiteral("width")).toDouble(),
       id + QStringLiteral("/bbox/width"), 0.1);
}

void compareTreeView(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<treeview::TreeViewScene>(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonArray labels =
      matches.value(QStringLiteral(".treeView-node-label")).toArray();
  require(labels.size() == scene->nodes.size(),
          id + QStringLiteral("/labels/count"));
  for (qsizetype i = 0; i < scene->nodes.size(); ++i) {
    const auto& node = scene->nodes.at(i);
    const QJsonObject label = labels.at(i).toObject();
    sameColor(node.label.fill, computed(label, "fill"),
              id + QStringLiteral("/label/%1/fill").arg(i));
    near(node.label.fontSize,
         computed(label, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/fontSize").arg(i), 0.001);
    require(int(node.label.fontWeight) ==
                int(editor::cssFontWeightToQt(
                    QJsonValue(computed(label, "fontWeight")), QFont::Normal)),
            id + QStringLiteral("/label/%1/fontWeight").arg(i));
    const bool displayed = computed(label, "display") != QLatin1String("none") &&
                           computed(label, "visibility") != QLatin1String("hidden") &&
                           computed(label, "visibility") != QLatin1String("collapse");
    require(node.label.visible == displayed,
            id + QStringLiteral("/label/%1/visible").arg(i));
    const bool hasBox = computed(label, "display") != QLatin1String("none");
    require(node.label.hasBox == hasBox,
            id + QStringLiteral("/label/%1/hasBox").arg(i));
    const QJsonObject bbox = label.value(QStringLiteral("bbox")).toObject();
    near(node.label.layoutWidth,
         bbox.value(QStringLiteral("width")).toDouble(),
         id + QStringLiteral("/label/%1/width").arg(i), 0.2);
  }

  const QJsonArray descriptions =
      matches.value(QStringLiteral(".treeView-node-description")).toArray();
  qsizetype descriptionIndex = 0;
  for (const auto& node : scene->nodes) {
    if (!node.hasDescription) continue;
    require(descriptionIndex < descriptions.size(),
            id + QStringLiteral("/descriptions/count"));
    const QJsonObject description = descriptions.at(descriptionIndex++).toObject();
    sameColor(node.description.fill, computed(description, "fill"),
              id + QStringLiteral("/description/fill"));
    near(node.description.fontSize,
         computed(description, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/description/fontSize"), 0.001);
    require(node.description.italic ==
                (computed(description, "fontStyle") != QLatin1String("normal")),
            id + QStringLiteral("/description/fontStyle"));
    require(node.description.hasBox ==
                (computed(description, "display") != QLatin1String("none")),
            id + QStringLiteral("/description/hasBox"));
  }
  require(descriptionIndex == descriptions.size(),
          id + QStringLiteral("/descriptions/count"));

  const QJsonArray lines =
      matches.value(QStringLiteral(".treeView-node-line")).toArray();
  require(lines.size() == scene->lines.size(), id + QStringLiteral("/lines/count"));
  for (qsizetype i = 0; i < scene->lines.size(); ++i) {
    const auto& line = scene->lines.at(i);
    const QJsonObject expected = lines.at(i).toObject();
    sameColor(line.stroke, computed(expected, "stroke"),
              id + QStringLiteral("/line/%1/stroke").arg(i));
    near(line.strokeWidth,
         computed(expected, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/line/%1/strokeWidth").arg(i), 0.001);
    near(line.opacity, computed(expected, "opacity").toDouble(),
         id + QStringLiteral("/line/%1/opacity").arg(i), 0.001);
    require(line.visible ==
                (computed(expected, "visibility") != QLatin1String("hidden") &&
                 computed(expected, "visibility") != QLatin1String("collapse") &&
                 computed(expected, "display") != QLatin1String("none")),
            id + QStringLiteral("/line/%1/visible").arg(i));
  }
  const QJsonArray highlights =
      matches.value(QStringLiteral(".treeView-highlight-bg")).toArray();
  if (!highlights.isEmpty()) {
    const auto it = std::find_if(scene->nodes.cbegin(), scene->nodes.cend(),
                                 [](const auto& node) { return node.highlighted; });
    require(it != scene->nodes.cend(), id + QStringLiteral("/highlight/native"));
    const QJsonObject expected = highlights.at(0).toObject();
    sameColor(it->highlightFill, computed(expected, "fill"),
              id + QStringLiteral("/highlight/fill"));
    sameColor(it->highlightStroke, computed(expected, "stroke"),
              id + QStringLiteral("/highlight/stroke"));
    near(it->highlightStrokeWidth,
         computed(expected, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/highlight/strokeWidth"), 0.001);
    near(it->highlightFillOpacity,
         computed(expected, "fillOpacity").toDouble(),
         id + QStringLiteral("/highlight/fillOpacity"), 0.001);
  }
  compareRoundedClient(fixture, entry);
}

void compareBlock(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<block::BlockScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  const QJsonArray groups = matches.value(QStringLiteral(".node")).toArray();
  const QJsonArray labels = matches.value(QStringLiteral(".node .label")).toArray();
  require(groups.size() == scene->flow.nodes.size(), id + QStringLiteral("/nodes/count"));
  require(labels.size() == scene->flow.nodes.size(), id + QStringLiteral("/labels/count"));
  for (qsizetype i = 0; i < scene->flow.nodes.size(); ++i) {
    const auto& node = scene->flow.nodes.at(i);
    const QJsonObject group = groups.at(i).toObject();
    const QJsonObject label = labels.at(i).toObject();
    require(node.visible ==
                (computed(group, "display") != QLatin1String("none") &&
                 computed(group, "visibility") != QLatin1String("hidden") &&
                 computed(group, "visibility") != QLatin1String("collapse")),
            id + QStringLiteral("/node/%1/visible").arg(i));
    sameColor(node.label.color, computed(label, "color"),
              id + QStringLiteral("/label/%1/color").arg(i));
    require(node.label.fontSize == computed(label, "fontSize"),
            id + QStringLiteral("/label/%1/fontSize").arg(i));
    require(int(editor::cssFontWeightToQt(QJsonValue(node.label.fontWeight),
                                          QFont::Normal)) ==
                int(editor::cssFontWeightToQt(
                    QJsonValue(computed(label, "fontWeight")), QFont::Normal)),
            id + QStringLiteral("/label/%1/fontWeight").arg(i));
  }
  const QJsonArray rects = matches.value(QStringLiteral(".node rect")).toArray();
  require(rects.size() >= scene->flow.nodes.size(), id + QStringLiteral("/rects/count"));
  for (qsizetype i = 0; i < scene->flow.nodes.size(); ++i) {
    const auto& node = scene->flow.nodes.at(i);
    const QJsonObject rect = rects.at(i * 2).toObject();
    sameColor(node.fill, computed(rect, "fill"),
              id + QStringLiteral("/node/%1/fill").arg(i));
    sameColor(node.stroke, computed(rect, "stroke"),
              id + QStringLiteral("/node/%1/stroke").arg(i));
    require(node.strokeWidth == computed(rect, "strokeWidth"),
            id + QStringLiteral("/node/%1/strokeWidth").arg(i));
  }
  const QJsonArray paths = matches.value(QStringLiteral(".flowchart-link")).toArray();
  require(paths.size() == scene->flow.edges.size(), id + QStringLiteral("/edges/count"));
  for (qsizetype i = 0; i < scene->flow.edges.size(); ++i) {
    const auto& edge = scene->flow.edges.at(i);
    const QJsonObject expected = paths.at(i).toObject();
    sameColor(edge.stroke, computed(expected, "stroke"),
              id + QStringLiteral("/edge/%1/stroke").arg(i));
    require(edge.strokeWidth == computed(expected, "strokeWidth"),
            id + QStringLiteral("/edge/%1/strokeWidth").arg(i));
    require(edge.visible ==
                (computed(expected, "display") != QLatin1String("none") &&
                 computed(expected, "visibility") != QLatin1String("hidden") &&
                 computed(expected, "visibility") != QLatin1String("collapse")),
            id + QStringLiteral("/edge/%1/visible").arg(i));
  }
}

bool browserDisplayed(const QJsonObject& element) {
  if (element.contains(QStringLiteral("ancestorDisplayed")))
    return element.value(QStringLiteral("ancestorDisplayed")).toBool();
  return computed(element, "display") != QLatin1String("none") &&
         computed(element, "visibility") != QLatin1String("hidden") &&
         computed(element, "visibility") != QLatin1String("collapse");
}

void compareVenn(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<venn::VennScene>(fixture, entry);
  compareRoundedClient(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();

  const QJsonArray titles = matches.value(QStringLiteral(".venn-title")).toArray();
  if (!titles.isEmpty()) {
    const QJsonObject title = titles.first().toObject();
    sameColor(scene->titleText.fill, computed(title, "fill"),
              id + QStringLiteral("/title/fill"));
    near(scene->titleText.fontSize,
         computed(title, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/title/fontSize"), 0.001);
    require(int(scene->titleText.fontWeight) ==
                int(editor::cssFontWeightToQt(
                    QJsonValue(computed(title, "fontWeight")), QFont::Normal)),
            id + QStringLiteral("/title/fontWeight"));
    require(scene->titleText.visible == browserDisplayed(title),
            id + QStringLiteral("/title/visible"));
  }

  QVector<const venn::VennAreaGeometry*> circles;
  QVector<const venn::VennAreaGeometry*> intersections;
  for (const auto& area : scene->areas)
    (area.circle ? circles : intersections).append(&area);
  auto compareAreas = [&](const QVector<const venn::VennAreaGeometry*>& areas,
                          const QString& prefix) {
    const QJsonArray groups = matches.value(prefix).toArray();
    const QJsonArray paths =
        matches.value(prefix + QStringLiteral(" path")).toArray();
    const QJsonArray labels =
        matches.value(prefix + QStringLiteral(" text")).toArray();
    require(paths.size() == areas.size(),
            id + prefix + QStringLiteral("/paths/count"));
    require(labels.size() == areas.size(),
            id + prefix + QStringLiteral("/labels/count"));
    require(groups.size() == areas.size(),
            id + prefix + QStringLiteral("/groups/count"));
    for (qsizetype i = 0; i < areas.size(); ++i) {
      const auto& area = *areas.at(i);
      const QJsonObject group = groups.at(i).toObject();
      const QJsonObject path = paths.at(i).toObject();
      const QJsonObject label = labels.at(i).toObject();
      sameColor(area.fill, computed(path, "fill"),
                id + prefix + QStringLiteral("/%1/fill").arg(i));
      sameColor(area.stroke, computed(path, "stroke"),
                id + prefix + QStringLiteral("/%1/stroke").arg(i));
      near(area.strokeWidth,
           computed(path, "strokeWidth").chopped(2).toDouble(),
           id + prefix + QStringLiteral("/%1/strokeWidth").arg(i), 0.001);
      near(area.fillOpacity,
           computed(path, "fillOpacity").toDouble() *
               computed(path, "opacity").toDouble(),
           id + prefix + QStringLiteral("/%1/fillOpacity").arg(i), 0.001);
      require(area.pathVisible ==
                  (browserDisplayed(group) && browserDisplayed(path)),
              id + prefix + QStringLiteral("/%1/pathVisible").arg(i));
      require(area.pathHasBox ==
                  (computed(group, "display") != QLatin1String("none") &&
                   computed(path, "display") != QLatin1String("none")),
              id + prefix + QStringLiteral("/%1/pathHasBox").arg(i));

      sameColor(area.label.fill, computed(label, "fill"),
                id + prefix + QStringLiteral("/%1/labelFill").arg(i));
      near(area.label.fontSize,
           computed(label, "fontSize").chopped(2).toDouble(),
           id + prefix + QStringLiteral("/%1/labelFontSize").arg(i), 0.001);
      require(int(area.label.fontWeight) ==
                  int(editor::cssFontWeightToQt(
                      QJsonValue(computed(label, "fontWeight")), QFont::Normal)),
              id + prefix + QStringLiteral("/%1/labelWeight").arg(i));
      require(area.label.visible ==
                  (browserDisplayed(group) && browserDisplayed(label)),
              id + prefix + QStringLiteral("/%1/labelVisible").arg(i));
      near(area.label.opacity,
           computed(label, "fillOpacity").toDouble() *
               computed(label, "opacity").toDouble(),
           id + prefix + QStringLiteral("/%1/labelOpacity").arg(i), 0.001);
    }
  };
  compareAreas(circles, QStringLiteral(".venn-circle"));
  compareAreas(intersections, QStringLiteral(".venn-intersection"));

  const QJsonArray textNodes =
      matches.value(QStringLiteral(".venn-text-node")).toArray();
  require(textNodes.size() == scene->textNodes.size(),
          id + QStringLiteral("/textNodes/count"));
  for (qsizetype i = 0; i < scene->textNodes.size(); ++i) {
    const auto& node = scene->textNodes.at(i);
    const QJsonObject expected = textNodes.at(i).toObject();
    sameColor(node.color, computed(expected, "color"),
              id + QStringLiteral("/textNode/%1/color").arg(i));
    near(node.fontSize,
         computed(expected, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/textNode/%1/fontSize").arg(i), 0.001);
    require(int(node.fontWeight) ==
                int(editor::cssFontWeightToQt(
                    QJsonValue(computed(expected, "fontWeight")), QFont::Normal)),
            id + QStringLiteral("/textNode/%1/fontWeight").arg(i));
    require(node.fontStyle ==
                (computed(expected, "fontStyle") == QLatin1String("italic")
                     ? QFont::StyleItalic
                     : computed(expected, "fontStyle").startsWith(
                           QLatin1String("oblique"))
                           ? QFont::StyleOblique
                           : QFont::StyleNormal),
            id + QStringLiteral("/textNode/%1/fontStyle").arg(i));
    require(node.visible == browserDisplayed(expected),
            id + QStringLiteral("/textNode/%1/visible").arg(i));
    require(node.hasBox ==
                (computed(expected, "display") != QLatin1String("none")),
            id + QStringLiteral("/textNode/%1/hasBox").arg(i));
    near(node.opacity, computed(expected, "opacity").toDouble(),
         id + QStringLiteral("/textNode/%1/opacity").arg(i), 0.001);
  }
}

void compareSwimlane(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<flowscene::FlowScene>(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();

  const QJsonArray titles = matches.value(QStringLiteral(".swimlane-title")).toArray();
  const QJsonArray bodies = matches.value(QStringLiteral(".swimlane-body")).toArray();
  require(titles.size() == scene->clusters.size(), id + QStringLiteral("/titles/count"));
  require(bodies.size() == scene->clusters.size(), id + QStringLiteral("/bodies/count"));
  auto compareLaneRect = [&](const QJsonObject& expected, bool title) {
    const QString clusterId =
        expected.value(QStringLiteral("ownerClusterId")).toString();
    const auto* cluster = clusterById(*scene, clusterId);
    require(cluster, id + QStringLiteral("/cluster/") + clusterId);
    const QString fill = title ? cluster->swimlaneTitleFill
                               : cluster->swimlaneBodyFill;
    const QString stroke = title ? cluster->swimlaneTitleStroke
                                 : cluster->swimlaneBodyStroke;
    const QString width = title ? cluster->swimlaneTitleStrokeWidth
                                : cluster->swimlaneBodyStrokeWidth;
    const bool visible = title ? cluster->swimlaneTitleVisible
                               : cluster->swimlaneBodyVisible;
    sameColor(fill, computed(expected, "fill"),
              id + QStringLiteral("/cluster/") + clusterId +
                  (title ? QStringLiteral("/titleFill")
                         : QStringLiteral("/bodyFill")));
    sameColor(stroke, computed(expected, "stroke"),
              id + QStringLiteral("/cluster/") + clusterId +
                  (title ? QStringLiteral("/titleStroke")
                         : QStringLiteral("/bodyStroke")));
    require(width == computed(expected, "strokeWidth"),
            id + QStringLiteral("/cluster/") + clusterId +
                QStringLiteral("/strokeWidth"));
    require(visible == browserDisplayed(expected),
            id + QStringLiteral("/cluster/") + clusterId +
                QStringLiteral("/visible"));
  };
  for (const QJsonValue& value : titles) compareLaneRect(value.toObject(), true);
  for (const QJsonValue& value : bodies) compareLaneRect(value.toObject(), false);

  QJsonArray laneLabels =
      matches.value(QStringLiteral(".swimlane-label span")).toArray();
  if (laneLabels.isEmpty())
    laneLabels = matches.value(QStringLiteral(".swimlane-label text")).toArray();
  require(laneLabels.size() == scene->clusters.size(),
          id + QStringLiteral("/laneLabels/count"));
  for (const QJsonValue& value : laneLabels) {
    const QJsonObject expected = value.toObject();
    const QString clusterId =
        expected.value(QStringLiteral("ownerClusterId")).toString();
    const auto* cluster = clusterById(*scene, clusterId);
    require(cluster, id + QStringLiteral("/laneLabel/") + clusterId);
    sameColor(cluster->label.color, computed(expected, "color"),
              id + QStringLiteral("/laneLabel/") + clusterId +
                  QStringLiteral("/color"));
    require(cluster->label.fontSize == computed(expected, "fontSize"),
            id + QStringLiteral("/laneLabel/") + clusterId +
                QStringLiteral("/fontSize"));
    require(cluster->label.visible == browserDisplayed(expected),
            id + QStringLiteral("/laneLabel/") + clusterId +
                QStringLiteral("/visible"));
  }

  const QJsonArray nodeGroups = matches.value(QStringLiteral(".node")).toArray();
  QJsonArray nodeRects;
  for (const QJsonValue& value :
       matches.value(QStringLiteral(".node rect")).toArray()) {
    const QString classes =
        value.toObject().value(QStringLiteral("class")).toString();
    if (classes.split(QLatin1Char(' '), Qt::SkipEmptyParts)
            .contains(QStringLiteral("basic")))
      nodeRects.append(value);
  }
  const QJsonArray nodeLabels =
      matches.value(QStringLiteral(".node .label")).toArray();
  require(nodeGroups.size() == scene->nodes.size(), id + QStringLiteral("/nodes/count"));
  require(nodeRects.size() == scene->nodes.size(), id + QStringLiteral("/rects/count"));
  for (const QJsonValue& value : nodeGroups) {
    const QJsonObject group = value.toObject();
    const QString nodeId = semanticNodeId(group.value(QStringLiteral("id")).toString());
    const auto* node = nodeById(*scene, nodeId);
    require(node, id + QStringLiteral("/node/") + nodeId);
    require(node->visible == browserDisplayed(group),
            id + QStringLiteral("/node/") + nodeId + QStringLiteral("/visible"));
  }
  for (const QJsonValue& value : nodeRects) {
    const QJsonObject expected = value.toObject();
    const QString nodeId = semanticNodeId(
        expected.value(QStringLiteral("ownerNodeId")).toString());
    const auto* node = nodeById(*scene, nodeId);
    require(node, id + QStringLiteral("/rect/") + nodeId);
    sameColor(node->fill, computed(expected, "fill"),
              id + QStringLiteral("/rect/") + nodeId + QStringLiteral("/fill"));
    const QJsonObject bbox = expected.value(QStringLiteral("bbox")).toObject();
    near(node->width, bbox.value(QStringLiteral("width")).toDouble(),
         id + QStringLiteral("/rect/") + nodeId + QStringLiteral("/width"), 0.1);
    near(node->height, bbox.value(QStringLiteral("height")).toDouble(),
         id + QStringLiteral("/rect/") + nodeId + QStringLiteral("/height"), 0.1);
  }
  for (const QJsonValue& value : nodeLabels) {
    const QJsonObject expected = value.toObject();
    const QString nodeId = semanticNodeId(
        expected.value(QStringLiteral("ownerNodeId")).toString());
    const auto* node = nodeById(*scene, nodeId);
    require(node, id + QStringLiteral("/label/") + nodeId);
    sameColor(node->label.color, computed(expected, "color"),
              id + QStringLiteral("/label/") + nodeId + QStringLiteral("/color"));
    require(node->label.fontSize == computed(expected, "fontSize"),
            id + QStringLiteral("/label/") + nodeId + QStringLiteral("/fontSize"));
  }
  const QJsonArray edgePaths =
      matches.value(QStringLiteral(".flowchart-link")).toArray();
  require(edgePaths.size() == scene->edges.size(), id + QStringLiteral("/edges/count"));
  for (qsizetype i = 0; i < scene->edges.size(); ++i) {
    const QJsonObject expected = edgePaths.at(i).toObject();
    sameColor(scene->edges.at(i).stroke, computed(expected, "stroke"),
              id + QStringLiteral("/edge/%1/stroke").arg(i));
    require(scene->edges.at(i).strokeWidth == computed(expected, "strokeWidth"),
            id + QStringLiteral("/edge/%1/strokeWidth").arg(i));
    require(scene->edges.at(i).visible == browserDisplayed(expected),
            id + QStringLiteral("/edge/%1/visible").arg(i));
  }
  compareRoundedClient(fixture, entry, 1.01);
}

void compareIshikawa(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<ishikawa::IshikawaScene>(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();

  const auto classContains = [](const QString& classes,
                                const QString& needle) {
    return classes.split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(needle);
  };
  const auto lines = [&](const QString& name) {
    QVector<const ishikawa::IshikawaLineGeometry*> result;
    for (const auto& value : scene->lines)
      if (classContains(value.className, name)) result.append(&value);
    return result;
  };
  const auto texts = [&](const QString& name) {
    QVector<const ishikawa::IshikawaTextGeometry*> result;
    for (const auto& value : scene->texts)
      if (classContains(value.className, name)) result.append(&value);
    return result;
  };
  const auto compareLines = [&](const QString& selector,
                                const QString& className) {
    const QJsonArray expected = matches.value(selector).toArray();
    const auto actual = lines(className);
    require(expected.size() == actual.size(), id + QLatin1Char('/') + selector);
    for (qsizetype i = 0; i < actual.size(); ++i) {
      const QJsonObject value = expected.at(i).toObject();
      sameColor(actual.at(i)->stroke, computed(value, "stroke"),
                id + QStringLiteral("/%1/%2/stroke").arg(selector).arg(i));
      near(actual.at(i)->strokeWidth,
           computed(value, "strokeWidth").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/strokeWidth").arg(selector).arg(i),
           0.001);
      require(actual.at(i)->visible == browserDisplayed(value),
              id + QStringLiteral("/%1/%2/visible").arg(selector).arg(i));
    }
  };
  compareLines(QStringLiteral(".ishikawa-spine"),
               QStringLiteral("ishikawa-spine"));
  compareLines(QStringLiteral(".ishikawa-branch"),
               QStringLiteral("ishikawa-branch"));
  compareLines(QStringLiteral(".ishikawa-sub-branch"),
               QStringLiteral("ishikawa-sub-branch"));

  const QJsonObject head = matches.value(QStringLiteral(".ishikawa-head"))
                               .toArray().first().toObject();
  require(!scene->paths.isEmpty(), id + QStringLiteral("/head/native"));
  sameColor(scene->paths.first().fill, computed(head, "fill"),
            id + QStringLiteral("/head/fill"));
  sameColor(scene->paths.first().stroke, computed(head, "stroke"),
            id + QStringLiteral("/head/stroke"));
  near(scene->paths.first().strokeWidth,
       computed(head, "strokeWidth").chopped(2).toDouble(),
       id + QStringLiteral("/head/strokeWidth"), 0.001);
  require(scene->paths.first().visible == browserDisplayed(head),
          id + QStringLiteral("/head/visible"));

  const QJsonArray boxes =
      matches.value(QStringLiteral(".ishikawa-label-box")).toArray();
  require(boxes.size() == scene->rects.size(), id + QStringLiteral("/boxes/count"));
  for (qsizetype i = 0; i < scene->rects.size(); ++i) {
    const QJsonObject value = boxes.at(i).toObject();
    const auto& actual = scene->rects.at(i);
    sameColor(actual.fill, computed(value, "fill"),
              id + QStringLiteral("/box/%1/fill").arg(i));
    sameColor(actual.stroke, computed(value, "stroke"),
              id + QStringLiteral("/box/%1/stroke").arg(i));
    near(actual.strokeWidth,
         computed(value, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/box/%1/strokeWidth").arg(i), 0.001);
    require(actual.visible == browserDisplayed(value),
            id + QStringLiteral("/box/%1/visible").arg(i));
  }

  const auto compareTexts = [&](const QString& selector,
                                const QString& className) {
    const QJsonArray expected = matches.value(selector).toArray();
    const auto actual = texts(className);
    require(expected.size() == actual.size(), id + QLatin1Char('/') + selector);
    for (qsizetype i = 0; i < actual.size(); ++i) {
      const QJsonObject value = expected.at(i).toObject();
      sameColor(actual.at(i)->fill, computed(value, "fill"),
                id + QStringLiteral("/%1/%2/fill").arg(selector).arg(i));
      near(actual.at(i)->fontSize,
           computed(value, "fontSize").chopped(2).toDouble(),
           id + QStringLiteral("/%1/%2/fontSize").arg(selector).arg(i), 0.001);
      require(actual.at(i)->visible == browserDisplayed(value),
              id + QStringLiteral("/%1/%2/visible").arg(selector).arg(i));
      require(actual.at(i)->hasBox ==
                  (computed(value, "display") != QLatin1String("none")),
              id + QStringLiteral("/%1/%2/hasBox").arg(selector).arg(i));
      require(actual.at(i)->rootHasBox ==
                  value.value(QStringLiteral("ancestorHasBox")).toBool(),
              id + QStringLiteral("/%1/%2/rootHasBox").arg(selector).arg(i));
    }
  };
  compareTexts(QStringLiteral(".ishikawa-head-label"),
               QStringLiteral("ishikawa-head-label"));
  compareTexts(QStringLiteral(".ishikawa-label.cause"),
               QStringLiteral("cause"));
  compareTexts(QStringLiteral(".ishikawa-label.align"),
               QStringLiteral("align"));
  compareTexts(QStringLiteral(".ishikawa-label.up"), QStringLiteral("up"));
  compareTexts(QStringLiteral(".ishikawa-label.down"), QStringLiteral("down"));

  const QJsonObject marker = matches.value(QStringLiteral(".ishikawa-arrow"))
                                 .toArray().first().toObject();
  sameColor(scene->style.markerFill, computed(marker, "fill"),
            id + QStringLiteral("/marker/fill"));
  require(scene->style.markerVisible == browserDisplayed(marker),
          id + QStringLiteral("/marker/visible"));
  compareRoundedClient(fixture, entry, 1.01);
}

QString requirementSemanticId(QString owner, const QString& fixtureId) {
  const QString prefix = QStringLiteral("theme-css-") + fixtureId +
                         QLatin1Char('-');
  return owner.startsWith(prefix) ? owner.mid(prefix.size()) : owner;
}

void compareRequirement(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene<requirement::RequirementScene>(fixture, entry);
  const QJsonObject matches = fixture.value(QStringLiteral("matches")).toObject();
  QHash<QString, const requirement::RequirementSceneNode*> nodes;
  for (const auto& node : scene->nodes) nodes.insert(node.id, &node);

  const QJsonArray boxes = matches.value(
      QStringLiteral(".node .label-container")).toArray();
  const QJsonArray boxPaths = matches.value(
      QStringLiteral(".node .label-container > path")).toArray();
  require(boxes.size() == scene->nodes.size(), id + QStringLiteral("/boxes/count"));
  require(boxPaths.size() == boxes.size() * 2,
          id + QStringLiteral("/box-paths/count"));
  for (qsizetype boxIndex = 0; boxIndex < boxes.size(); ++boxIndex) {
    const QJsonValue& raw = boxes.at(boxIndex);
    const QJsonObject expected = raw.toObject();
    const QJsonObject expectedFill = boxPaths.at(boxIndex * 2).toObject();
    const QJsonObject expectedStroke = boxPaths.at(boxIndex * 2 + 1).toObject();
    const QString nodeId = requirementSemanticId(
        expected.value(QStringLiteral("ownerNodeId")).toString(), id);
    const auto* node = nodes.value(nodeId, nullptr);
    require(node, id + QStringLiteral("/box/") + nodeId);
    sameColor(node->fillNone ? QStringLiteral("none") : node->fill,
              computed(expectedFill, "fill"),
              id + QStringLiteral("/box/") + nodeId + QStringLiteral("/fill"));
    // Computed stroke is compared UNGATED: the browser keeps the computed stroke
    // of the box's stroke path even when an ancestor is display:none / the path
    // is visibility:hidden (getComputedStyle is independent of painting). The
    // box's effective paint visibility is asserted separately via node->visible
    // below. outlineStroke carries the cascade-resolved computed color (and
    // "none" only when the stroke value itself is none), never muted to none by
    // a hidden ancestor. Mirrors the edge/marker fill/stroke handling here.
    sameColor(node->outlineStroke,
              computed(expectedStroke, "stroke"),
              id + QStringLiteral("/box/") + nodeId + QStringLiteral("/stroke"));
    near(node->strokeWidth,
         computed(expectedStroke, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/box/") + nodeId + QStringLiteral("/strokeWidth"),
         0.001);
    require(node->visible == browserDisplayed(expected),
            id + QStringLiteral("/box/") + nodeId + QStringLiteral("/visible"));
    const QJsonObject bbox = expected.value(QStringLiteral("bbox")).toObject();
    if (expected.value(QStringLiteral("ancestorHasBox")).toBool()) {
      near(node->size.width(), bbox.value(QStringLiteral("width")).toDouble(),
           id + QStringLiteral("/box/") + nodeId + QStringLiteral("/width"), 0.1);
      near(node->size.height(), bbox.value(QStringLiteral("height")).toDouble(),
           id + QStringLiteral("/box/") + nodeId + QStringLiteral("/height"), 0.1);
    }
  }

  QHash<QString, qsizetype> wrapperRowIndexes;
  for (const QJsonValue& raw : matches.value(QStringLiteral(".node .label")).toArray()) {
    const QJsonObject expected = raw.toObject();
    const QString nodeId = requirementSemanticId(
        expected.value(QStringLiteral("ownerNodeId")).toString(), id);
    const auto* node = nodes.value(nodeId, nullptr);
    if (!node) continue;
    const qsizetype rowIndex = wrapperRowIndexes.value(nodeId, 0);
    wrapperRowIndexes[nodeId] = rowIndex + 1;
    require(rowIndex < node->rows.size(),
            id + QStringLiteral("/row-wrapper/") + nodeId + QStringLiteral("/count"));
    const auto& row = node->rows.at(rowIndex);
    sameColor(row.wrapperComputed.color, computed(expected, "color"),
              id + QStringLiteral("/row-wrapper/") + nodeId +
                  QStringLiteral("/%1/color").arg(rowIndex));
    near(row.wrapperComputed.fontSize.chopped(2).toDouble(),
         computed(expected, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/row-wrapper/") + nodeId +
             QStringLiteral("/%1/fontSize").arg(rowIndex), 0.001);
    require(row.wrapperComputed.displayed == browserDisplayed(expected),
            id + QStringLiteral("/row-wrapper/") + nodeId +
                QStringLiteral("/%1/visible").arg(rowIndex));
  }

  QHash<QString, qsizetype> paintedRowIndexes;
  for (const QJsonValue& raw : matches.value(QStringLiteral(".nodeLabel")).toArray()) {
    const QJsonObject expected = raw.toObject();
    const QString nodeId = requirementSemanticId(
        expected.value(QStringLiteral("ownerNodeId")).toString(), id);
    const auto* node = nodes.value(nodeId, nullptr);
    if (!node) continue;
    const qsizetype rowIndex = paintedRowIndexes.value(nodeId, 0);
    paintedRowIndexes[nodeId] = rowIndex + 1;
    require(rowIndex < node->rows.size(),
            id + QStringLiteral("/row-painted/") + nodeId + QStringLiteral("/count"));
    const auto& row = node->rows.at(rowIndex);
    sameColor(row.color, computed(expected, "color"),
              id + QStringLiteral("/row-painted/") + nodeId +
                  QStringLiteral("/%1/color").arg(rowIndex));
    sameColor(row.paintedTextComputed.color, computed(expected, "color"),
              id + QStringLiteral("/row-painted/") + nodeId +
                  QStringLiteral("/%1/computed-color").arg(rowIndex));
    near(row.fontPixelSize,
         computed(expected, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/row-painted/") + nodeId +
             QStringLiteral("/%1/fontSize").arg(rowIndex), 0.001);
    require(row.visible == browserDisplayed(expected),
            id + QStringLiteral("/row-painted/") + nodeId +
                QStringLiteral("/%1/visible").arg(rowIndex));
    require(expected.value(QStringLiteral("parentTag")).toString() ==
                QLatin1String("DIV") &&
                expected.value(QStringLiteral("domPath")).toString().contains(
                    QStringLiteral("g.label > foreignobject > div > span")),
            id + QStringLiteral("/row-painted/") + nodeId +
                QStringLiteral("/%1/dom-path").arg(rowIndex));
  }
  require(matches.value(QStringLiteral(".label > span")).toArray().isEmpty(),
          id + QStringLiteral("/label-direct-span/count"));

  const QJsonArray dividers = matches.value(QStringLiteral(".divider")).toArray();
  for (const QJsonValue& raw : dividers) {
    const QJsonObject expected = raw.toObject();
    const QString nodeId = requirementSemanticId(
        expected.value(QStringLiteral("ownerNodeId")).toString(), id);
    const auto* node = nodes.value(nodeId, nullptr);
    require(node, id + QStringLiteral("/divider/") + nodeId);
    sameColor(node->dividerWrapperComputed.stroke,
              computed(expected, "stroke"),
              id + QStringLiteral("/divider-wrapper/") + nodeId + QStringLiteral("/stroke"));
    near(node->dividerWrapperComputed.strokeWidth.chopped(2).toDouble(),
         computed(expected, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/divider-wrapper/") + nodeId + QStringLiteral("/strokeWidth"),
         0.001);
    require(node->dividerWrapperComputed.displayed == browserDisplayed(expected),
            id + QStringLiteral("/divider-wrapper/") + nodeId + QStringLiteral("/visible"));
  }
  const QJsonArray dividerPaths = matches.value(
      QStringLiteral(".divider > path")).toArray();
  require(dividerPaths.size() == dividers.size(),
          id + QStringLiteral("/divider-path/count"));
  for (const QJsonValue& raw : dividerPaths) {
    const QJsonObject expected = raw.toObject();
    const QString nodeId = requirementSemanticId(
        expected.value(QStringLiteral("ownerNodeId")).toString(), id);
    const auto* node = nodes.value(nodeId, nullptr);
    require(node && node->dividerChildPaths.size() == 1,
            id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/count"));
    const auto& path = node->dividerChildPaths.first();
    sameColor(path.stroke, computed(expected, "stroke"),
              id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/stroke"));
    near(path.strokeWidth, computed(expected, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/strokeWidth"),
         0.001);
    near(path.effectiveStrokeOpacity,
         expected.value(QStringLiteral("effectiveOpacity")).toDouble() *
             computed(expected, "strokeOpacity").toDouble(),
         id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/opacity"),
         0.001);
    require(path.displayed == browserDisplayed(expected),
            id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/visible"));
    require(expected.value(QStringLiteral("parentTag")).toString() ==
                QLatin1String("g") &&
                expected.value(QStringLiteral("parentClass")).toString() ==
                QLatin1String("divider"),
            id + QStringLiteral("/divider-path/") + nodeId + QStringLiteral("/dom-parent"));
  }

  const QJsonArray paths = matches.value(QStringLiteral(".relationshipLine")).toArray();
  require(paths.size() == scene->edges.size(), id + QStringLiteral("/edges/count"));
  for (qsizetype index = 0; index < scene->edges.size(); ++index) {
    const auto& edge = scene->edges.at(index);
    const QJsonObject expected = paths.at(index).toObject();
    sameColor(edge.stroke, computed(expected, "stroke"),
              id + QStringLiteral("/edge/%1/stroke").arg(index));
    near(edge.strokeWidth,
         computed(expected, "strokeWidth").chopped(2).toDouble(),
         id + QStringLiteral("/edge/%1/strokeWidth").arg(index), 0.001);
    require(edge.visible == browserDisplayed(expected),
            id + QStringLiteral("/edge/%1/visible").arg(index));
  }
  const QJsonArray outerLabels = matches.value(
      QStringLiteral(".edgeLabels > .edgeLabel")).toArray();
  const QJsonArray labels = matches.value(
      QStringLiteral(".edgeLabel .label")).toArray();
  const QJsonArray containers = matches.value(
      QStringLiteral("div.labelBkg")).toArray();
  const QJsonArray spans = matches.value(
      QStringLiteral("span.edgeLabel")).toArray();
  require(outerLabels.size() == scene->edges.size() &&
              labels.size() == scene->edges.size() &&
              containers.size() == scene->edges.size() &&
              spans.size() == scene->edges.size(),
          id + QStringLiteral("/labels/count"));
  for (qsizetype index = 0; index < scene->edges.size(); ++index) {
    const auto& edge = scene->edges.at(index);
    const QJsonObject outer = outerLabels.at(index).toObject();
    const QJsonObject inner = labels.at(index).toObject();
    const QJsonObject container = containers.at(index).toObject();
    const QJsonObject span = spans.at(index).toObject();
    sameColor(edge.outerLabelComputed.color, computed(outer, "color"),
              id + QStringLiteral("/label/%1/outer-color").arg(index));
    sameColor(edge.innerLabelComputed.color, computed(inner, "color"),
              id + QStringLiteral("/label/%1/inner-color").arg(index));
    sameColor(edge.labelColor, computed(span, "color"),
              id + QStringLiteral("/label/%1/painted-color").arg(index));
    sameColor(edge.paintedSpanComputed.color, computed(span, "color"),
              id + QStringLiteral("/label/%1/span-color").arg(index));
    sameColor(edge.labelContainerBg.color, computed(container, "backgroundColor"),
              id + QStringLiteral("/label/%1/container-background").arg(index));
    sameColor(edge.labelTextBg.color, computed(span, "backgroundColor"),
              id + QStringLiteral("/label/%1/text-background").arg(index));
    near(requirement::requirementEffectiveFontSize(
             edge.labelTextStyle, scene->style.fontSize),
         computed(span, "fontSize").chopped(2).toDouble(),
         id + QStringLiteral("/label/%1/fontSize").arg(index), 0.001);
    require(edge.labelContainerBg.displayed == browserDisplayed(container),
            id + QStringLiteral("/label/%1/container-visible").arg(index));
    require(edge.labelTextBg.displayed == browserDisplayed(span) &&
                edge.paintedSpanComputed.displayed == browserDisplayed(span),
            id + QStringLiteral("/label/%1/span-visible").arg(index));
    near(edge.labelContainerBg.effectiveOpacity,
         container.value(QStringLiteral("effectiveOpacity")).toDouble(),
         id + QStringLiteral("/label/%1/container-opacity").arg(index), 0.001);
    near(edge.labelTextBg.effectiveOpacity,
         span.value(QStringLiteral("effectiveOpacity")).toDouble(),
         id + QStringLiteral("/label/%1/span-opacity").arg(index), 0.001);
    require(container.value(QStringLiteral("parentTag")).toString() ==
                QLatin1String("foreignObject") &&
                span.value(QStringLiteral("parentTag")).toString() ==
                QLatin1String("DIV") &&
                span.value(QStringLiteral("parentClass")).toString() ==
                QLatin1String("labelBkg"),
            id + QStringLiteral("/label/%1/dom-parent").arg(index));
  }
  const QJsonArray markers = matches.value(QStringLiteral("marker")).toArray();
  require(markers.size() == scene->markers.size(), id + QStringLiteral("/markers/count"));
  for (qsizetype index = 0; index < scene->markers.size(); ++index) {
    const QJsonObject expected = markers.at(index).toObject();
    const auto& marker = scene->markers.at(index);
    sameColor(marker.fill, computed(expected, "fill"),
              id + QStringLiteral("/marker/%1/fill").arg(index));
    sameColor(marker.stroke, computed(expected, "stroke"),
              id + QStringLiteral("/marker/%1/stroke").arg(index));
    require(marker.visible == browserDisplayed(expected),
            id + QStringLiteral("/marker/%1/visible").arg(index));
  }
  compareRoundedClient(fixture, entry, 1.01);
}

// Shared cascade-engine regression suite for visibility/display/opacity
// ancestry (CSS spec semantics, independent of any browser fixture):
//   - only ancestor `display:none` hard-suppresses a subtree;
//   - `visibility:hidden` is inherited and a child `visibility:visible`
//     declaration recovers rendering;
//   - ancestor opacity composes multiplicatively through the chain;
//   - inline-style visibility on a parent reaches children (the engine's
//     rule-only ancestor walk cannot see inline declarations).
void verifyVisibilityAncestry() {
  using csscascade::ElementInput;
  using csscascade::ElementStyle;
  auto element = [](const QString& key, const QString& parent,
                    const QString& tag, const QStringList& classes,
                    const QString& inlineStyle = QString()) {
    ElementInput input;
    input.key = key;
    input.parentKey = parent;
    input.tag = tag;
    input.classes = classes;
    input.inlineStyle = inlineStyle;
    return input;
  };
  const auto styleOf = [](const QHash<QString, ElementStyle>& css,
                          const QString& key) {
    const auto it = css.constFind(key);
    require(it != css.cend(), QStringLiteral("visibility-suite/") + key);
    return it.value();
  };
  // themeCSS is scoped under `#diagram-root` (Stylis root namespacing), so the
  // synthetic trees start with the SVG root element exactly like real adapters.
  const auto rootElement = [] {
    ElementInput input;
    input.key = QStringLiteral("svg");
    input.tag = QStringLiteral("svg");
    input.id = QStringLiteral("diagram-root");
    return input;
  };

  {
    // Parent rule visibility:hidden; child redeclares visible.
    QVector<ElementInput> elements;
    elements.append(rootElement());
    elements.append(element(QStringLiteral("g"), QStringLiteral("svg"),
                            QStringLiteral("g"), {QStringLiteral("node")}));
    elements.append(element(QStringLiteral("c"), QStringLiteral("g"),
                            QStringLiteral("rect"), {}));
    QHash<QString, ElementStyle> css = csscascade::resolveElements(
        QStringLiteral(".node{visibility:hidden;}"), elements);
    require(!styleOf(css, QStringLiteral("g")).displayed(),
            QStringLiteral("visibility-suite/parent-hidden"));
    require(!styleOf(css, QStringLiteral("c")).displayed(),
            QStringLiteral("visibility-suite/child-inherits-hidden"));
    css = csscascade::resolveElements(
        QStringLiteral(".node{visibility:hidden;}.node rect{visibility:visible;}"),
        elements);
    require(styleOf(css, QStringLiteral("c")).displayed(),
            QStringLiteral("visibility-suite/child-visible-recovers"));
  }
  {
    // Parent INLINE visibility:hidden (engine ancestor walk is rule-only, so
    // this must flow through the computed-value inheritance in project()).
    QVector<ElementInput> elements;
    elements.append(rootElement());
    elements.append(element(QStringLiteral("g"), QStringLiteral("svg"),
                            QStringLiteral("g"), {QStringLiteral("node")},
                            QStringLiteral("visibility:hidden")));
    elements.append(element(QStringLiteral("c"), QStringLiteral("g"),
                            QStringLiteral("rect"), {}));
    const QHash<QString, ElementStyle> css = csscascade::resolveElements(
        {}, elements);
    require(!styleOf(css, QStringLiteral("g")).displayed(),
            QStringLiteral("visibility-suite/parent-inline-hidden"));
    require(!styleOf(css, QStringLiteral("c")).displayed(),
            QStringLiteral("visibility-suite/child-inherits-inline-hidden"));
  }
  {
    // Ancestor display:none hard-suppresses even a child display:block, and
    // removes its box; a child cannot recover from display:none.
    QVector<ElementInput> elements;
    elements.append(rootElement());
    elements.append(element(QStringLiteral("g"), QStringLiteral("svg"),
                            QStringLiteral("g"), {QStringLiteral("node")}));
    elements.append(element(QStringLiteral("c"), QStringLiteral("g"),
                            QStringLiteral("rect"), {},
                            QStringLiteral("display:block")));
    const QHash<QString, ElementStyle> css = csscascade::resolveElements(
        QStringLiteral(".node{display:none;}"), elements);
    const ElementStyle child = styleOf(css, QStringLiteral("c"));
    require(!child.displayed(),
            QStringLiteral("visibility-suite/display-none-suppresses-child"));
    require(!child.hasBox(),
            QStringLiteral("visibility-suite/display-none-drops-box"));
    require(!child.ancestorRenderable,
            QStringLiteral("visibility-suite/display-none-ancestor-flag"));
  }
  {
    // visibility:hidden does NOT clear the box and does not mark the
    // ancestor chain unrenderable: the child keeps geometry.
    QVector<ElementInput> elements;
    elements.append(rootElement());
    elements.append(element(QStringLiteral("g"), QStringLiteral("svg"),
                            QStringLiteral("g"), {QStringLiteral("node")}));
    elements.append(element(QStringLiteral("c"), QStringLiteral("g"),
                            QStringLiteral("rect"), {}));
    const QHash<QString, ElementStyle> css = csscascade::resolveElements(
        QStringLiteral(".node{visibility:hidden;}"), elements);
    const ElementStyle child = styleOf(css, QStringLiteral("c"));
    require(child.ancestorRenderable && child.hasBox(),
            QStringLiteral("visibility-suite/hidden-keeps-box"));
  }
  {
    // Ancestor opacity composes down the chain (0.5 * 0.5 = 0.25), including
    // through an intermediate element with no opacity declaration.
    QVector<ElementInput> elements;
    elements.append(rootElement());
    elements.append(element(QStringLiteral("a"), QStringLiteral("svg"),
                            QStringLiteral("g"), {QStringLiteral("a")}));
    elements.append(element(QStringLiteral("b"), QStringLiteral("a"),
                            QStringLiteral("g"), {}));
    elements.append(element(QStringLiteral("c"), QStringLiteral("b"),
                            QStringLiteral("rect"), {},
                            QStringLiteral("opacity:0.5")));
    const QHash<QString, ElementStyle> css = csscascade::resolveElements(
        QStringLiteral(".a{opacity:0.5;}"), elements);
    near(styleOf(css, QStringLiteral("c")).effectiveOpacity, 0.25,
         QStringLiteral("visibility-suite/opacity-chain"), 0.001);
    near(styleOf(css, QStringLiteral("b")).effectiveOpacity, 0.5,
         QStringLiteral("visibility-suite/opacity-mid"), 0.001);
  }
}

void compareRequirementPaintedPixel(const QString& fixturePath) {
  const QDir fixtureDir = QFileInfo(fixturePath).dir();
  const QString manifestPath = fixtureDir.filePath(
      QStringLiteral("theme-css-pixel/manifest.json"));
  QFile manifestFile(manifestPath);
  require(manifestFile.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open themeCSS pixel manifest"));
  const QByteArray bytes = manifestFile.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("ede4fc44591ef6786e9df833044d2e3b917a5de473118c2f1c31876259cd649d"),
          QStringLiteral("themeCSS pixel manifest hash drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("bb759226e2b0cded8d0ae5d83f75aa59a5074239383c722358fa712759d4d76f"),
          QStringLiteral("themeCSS pixel fixture drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 1, QStringLiteral("themeCSS pixel case count"));
  const QJsonObject fixture = cases.first().toObject();
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString referencePath = QFileInfo(manifestPath).dir().filePath(
      fixture.value(QStringLiteral("file")).toString());
  require(fileSha(referencePath) ==
              fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
          id + QStringLiteral("/browser-png-hash"));
  const QImage reference(referencePath);
  const QImage native = decodePngDataUrl(
      editor::MermaidRenderCache::renderMermaidSourceToPng(
          fixture.value(QStringLiteral("source")).toString(), 1.0).dataUrl);
  require(!reference.isNull() && !native.isNull(), id + QStringLiteral("/decode"));
  require(native.size() == reference.size(),
          QStringLiteral("%1: native %2x%3 != browser %4x%5")
              .arg(id).arg(native.width()).arg(native.height())
              .arg(reference.width()).arg(reference.height()));
  const qreal iou = alphaIou(native, reference);
  const qreal rgba = rgbaSimilarity(native, reference);
  std::fprintf(stderr, "%s alphaIoU=%.6f rgba=%.6f\n",
               qPrintable(id), iou, rgba);
  require(iou >= 0.72, id + QStringLiteral("/alpha-IoU"));
  require(rgba >= 0.80, id + QStringLiteral("/RGBA"));
  const QJsonObject roiValue = fixture.value(QStringLiteral("roi")).toObject();
  const QRect roi(roiValue.value(QStringLiteral("x")).toInt(),
                  roiValue.value(QStringLiteral("y")).toInt(),
                  roiValue.value(QStringLiteral("width")).toInt(),
                  roiValue.value(QStringLiteral("height")).toInt());
  const QColor expectedRoi = averageRoi(reference, roi);
  const QColor nativeRoi = averageRoi(native, roi);
  const int roiDifference = std::abs(expectedRoi.red() - nativeRoi.red()) +
      std::abs(expectedRoi.green() - nativeRoi.green()) +
      std::abs(expectedRoi.blue() - nativeRoi.blue()) +
      std::abs(expectedRoi.alpha() - nativeRoi.alpha());
  require(roiDifference <= 48,
          QStringLiteral("%1/painted-background-ROI: %2 != %3")
              .arg(id, nativeRoi.name(QColor::HexArgb),
                   expectedRoi.name(QColor::HexArgb)));
}

}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) and macOS (SF/Helvetica) resolve different faces
  // with different metrics. Bundled-font goldens are the eventual closure.
  qWarning("skipped on Linux/macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected mermaid-theme-css.json"));
  verifyVisibilityAncestry();
  const QString fixturePath = QString::fromLocal8Bit(argv[1]);
  QFile file(fixturePath);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open fixture"));
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("1115215d5177a102e3328ea6e84d5b08754a4933b9772f5b91a4639fd28fee01"),
          QStringLiteral("themeCSS fixture file hash drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("262bb3f34871aa067493a40fd8894fc9cf4bd976d62d71205d99400aa0d3a11d"),
          QStringLiteral("themeCSS upstream provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 117, QStringLiteral("themeCSS case count drifted"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    if (fixture.value(QStringLiteral("id")).toString().startsWith(
            QStringLiteral("flow-")))
      compareFlow(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("sequence-")))
      compareSequence(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("packet-")))
      comparePacket(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("pie-")))
      comparePie(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("er-")))
      compareEr(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("class-")))
      compareClass(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("state-")))
      compareState(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("journey-")))
      compareJourney(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("timeline-")))
      compareTimeline(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("kanban-")))
      compareKanban(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("gitgraph-")))
      compareGitGraph(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("c4-")))
      compareC4(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("gantt-")))
      compareGantt(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("eventmodeling-")))
      compareEventModeling(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("treemap-")))
      compareTreemap(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("wardley-")))
      compareWardley(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("architecture-")))
      compareArchitecture(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("railroad-")))
      compareRailroad(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("cynefin-")))
      compareCynefin(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("mindmap-")))
      compareMindmap(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("info-")))
      compareInfo(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("quadrant-")))
      compareQuadrant(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("radar-")))
      compareRadar(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("xychart-")))
      compareXYChart(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("sankey-")))
      compareSankey(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("treeview-")))
      compareTreeView(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("block-")))
      compareBlock(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("venn-")))
      compareVenn(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("swimlane-")))
      compareSwimlane(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("ishikawa-")))
      compareIshikawa(fixture);
    else if (fixture.value(QStringLiteral("id")).toString().startsWith(
                 QStringLiteral("requirement-")))
      compareRequirement(fixture);
  }
  compareRequirementPaintedPixel(fixturePath);
  return 0;
}
