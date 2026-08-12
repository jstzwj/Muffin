#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/c4/C4Scene.h"
#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/kanban/KanbanScene.h"
#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/gitgraph/GitGraphScene.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/railroad/RailroadScene.h"
#include "mermaid/timeline/TimelineScene.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/treeview/TreeViewScene.h"
#include "mermaid/venn/VennScene.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/treemap/TreemapScene.h"
#include "mermaid/wardley/WardleyScene.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>

using muffin::mermaid::editor::MermaidRenderCache;
using muffin::mermaid::editor::MermaidRenderEntry;
using muffin::mermaid::editor::MermaidRenderStatus;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QJsonObject loadObject(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open %1").arg(path));
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid JSON in %1: %2").arg(path, error.errorString()));
  return document.object();
}

QString fixtureDigest(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open %1 for digest validation").arg(path));
  const QByteArray source = file.readAll();
  QByteArray compact;
  compact.reserve(source.size());
  bool inString = false;
  bool escaped = false;
  for (const char character : source) {
    if (inString) {
      compact.append(character);
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == '"')
        inString = false;
    } else if (character == '"') {
      inString = true;
      compact.append(character);
    } else if (!std::isspace(static_cast<unsigned char>(character))) {
      compact.append(character);
    }
  }
  const QByteArray marker = QByteArrayLiteral(",\"fixtureSha256\":");
  const qsizetype markerOffset = compact.lastIndexOf(marker);
  require(markerOffset > 0 && compact.endsWith('}'),
          QStringLiteral("Config matrix digest field is malformed"));
  QByteArray canonical = compact.left(markerOffset);
  canonical.append('}');
  return QString::fromLatin1(
      QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

QSet<QString> stringSet(const QJsonArray& values) {
  QSet<QString> result;
  for (const QJsonValue& value : values) result.insert(value.toString());
  return result;
}

MermaidRenderEntry render(const QString& source) {
  MermaidRenderCache cache;
  return cache.getSync(MermaidRenderCache::makeKey(source), source);
}

QImage pngImage(const QString& source) {
  const QString dataUrl = MermaidRenderCache::renderMermaidSourceToPngDataUrl(
      source, 1.0);
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  require(comma > 0, QStringLiteral("Mermaid PNG data URL is malformed"));
  return QImage::fromData(
      QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
}

QPointF stateCenter(const MermaidRenderEntry& entry, const QString& id) {
  const auto* stateScene = dynamic_cast<const muffin::mermaid::state::StateScene*>(entry.scene.get());
  require(stateScene != nullptr,
          QStringLiteral("Missing state scene while looking for %1").arg(id));
  for (const auto& node : stateScene->nodes)
    if (node.id == id) return node.bounds.center();
  fail(QStringLiteral("Missing state node %1").arg(id));
}

QPointF erCenter(const MermaidRenderEntry& entry, const QString& id) {
  const auto* erScene = dynamic_cast<const muffin::mermaid::er::ErScene*>(entry.scene.get());
  require(erScene != nullptr,
          QStringLiteral("Missing ER scene while looking for %1").arg(id));
  for (const auto& entity : erScene->entities)
    if (entity.id == id) return entity.bounds.center();
  fail(QStringLiteral("Missing ER entity %1").arg(id));
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected config-effect fixture path"));
  const QString fixturePath = QString::fromLocal8Bit(argv[1]);
  const QJsonObject fixture = loadObject(fixturePath);
  require(fixture.value(QStringLiteral("fixtureSha256")).toString() ==
              fixtureDigest(fixturePath),
          QStringLiteral("Config matrix fixture digest drifted"));
  require(fixture.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Config matrix upstream version drifted"));

  const QSet<QString> expectedDimensions = {
      QStringLiteral("parsed"),     QStringLiteral("layout"),
      QStringLiteral("text"),       QStringLiteral("paint"),
      QStringLiteral("viewport"),   QStringLiteral("interaction"),
      QStringLiteral("export")};
  require(stringSet(fixture.value(QStringLiteral("dimensions")).toArray()) ==
              expectedDimensions,
          QStringLiteral("Config matrix dimensions drifted"));

  const QJsonArray entries = fixture.value(QStringLiteral("entries")).toArray();
  require(entries.size() == 532,
          QStringLiteral("Expected 532 classified config rows, found %1")
              .arg(entries.size()));
  QMap<QString, QJsonObject> byPath;
  QMap<QString, int> familyCounts;
  QMap<QString, int> statusCounts;
  QSet<QString> observedNativeDimensions;
  const QSet<QString> statuses = {
      QStringLiteral("parity"),       QStringLiteral("partial"),
      QStringLiteral("deferred"),     QStringLiteral("unsupported"),
      QStringLiteral("upstream-inert"), QStringLiteral("legacy-only"),
      QStringLiteral("api-only"),     QStringLiteral("security-fixed")};
  for (const QJsonValue& value : entries) {
    const QJsonObject entry = value.toObject();
    const QString path = entry.value(QStringLiteral("path")).toString();
    const QString status = entry.value(QStringLiteral("status")).toString();
    require(!path.isEmpty() && !byPath.contains(path),
            QStringLiteral("Duplicate or empty config matrix path: %1").arg(path));
    require(statuses.contains(status),
            QStringLiteral("Unknown status for %1: %2").arg(path, status));
    byPath.insert(path, entry);
    ++statusCounts[status];
    const QString family = entry.value(QStringLiteral("family")).toString();
    if (!family.isEmpty()) ++familyCounts[family];

    const QSet<QString> upstream =
        stringSet(entry.value(QStringLiteral("upstream")).toArray());
    const QSet<QString> native =
        stringSet(entry.value(QStringLiteral("native")).toArray());
    QSet<QString> unknownUpstream = upstream;
    QSet<QString> unknownNative = native;
    unknownUpstream.subtract(expectedDimensions);
    unknownNative.subtract(expectedDimensions);
    require(upstream.contains(QStringLiteral("parsed")) &&
                native.contains(QStringLiteral("parsed")) &&
                unknownUpstream.isEmpty() && unknownNative.isEmpty(),
            QStringLiteral("Invalid effects for %1").arg(path));
    observedNativeDimensions.unite(
        stringSet(entry.value(QStringLiteral("native")).toArray()));
    if (status == QLatin1String("parity")) {
      require(entry.value(QStringLiteral("upstream")).toArray() ==
                  entry.value(QStringLiteral("native")).toArray(),
              QStringLiteral("Parity row differs for %1").arg(path));
    } else if (status == QLatin1String("upstream-inert")) {
      require(entry.value(QStringLiteral("upstream")).toArray().size() == 1 &&
                  entry.value(QStringLiteral("native")).toArray().size() == 1,
              QStringLiteral("Inert row has a downstream effect: %1").arg(path));
    } else if (status == QLatin1String("deferred")) {
      require(entry.value(QStringLiteral("upstream")).toArray().size() > 1 &&
                  entry.value(QStringLiteral("native")).toArray().size() == 1,
              QStringLiteral("Deferred row is not explicit: %1").arg(path));
    }
  }
  require(familyCounts == QMap<QString, int>{{QStringLiteral("class"), 14},
                                             {QStringLiteral("architecture"), 11},
                                             {QStringLiteral("er"), 13},
                                             {QStringLiteral("eventmodeling"), 4},
                                             {QStringLiteral("flowchart"), 14},
                                             {QStringLiteral("swimlane"), 6},
                                             {QStringLiteral("gantt"), 17},
                                             {QStringLiteral("ishikawa"), 3},
                                             {QStringLiteral("journey"), 26},
                                             {QStringLiteral("kanban"), 5},
                                             {QStringLiteral("mindmap"), 5},
                                             {QStringLiteral("railroad"), 26},
                                             {QStringLiteral("block"), 3},
                                              {QStringLiteral("gitGraph"), 12},
                                              {QStringLiteral("c4"), 142},
                                             {QStringLiteral("pie"), 6},
                                             {QStringLiteral("packet"), 8},
                                             {QStringLiteral("quadrantChart"), 20},
                                             {QStringLiteral("radar"), 11},
                                             {QStringLiteral("requirement"), 11},
                                             {QStringLiteral("sequence"), 37},
                                             {QStringLiteral("state"), 22},
                                             {QStringLiteral("timeline"), 24},
                                             {QStringLiteral("treeView"), 10},
                                             {QStringLiteral("venn"), 6},
                                             {QStringLiteral("sankey"), 13},
                                             {QStringLiteral("treemap"), 11},
                                             {QStringLiteral("cynefin"), 8},
                                             {QStringLiteral("wardley-beta"), 10},
                                             {QStringLiteral("xyChart"), 13}},
          QStringLiteral("Family interface coverage drifted"));
  require(observedNativeDimensions == expectedDimensions,
          QStringLiteral("At least one native effect dimension has no evidence"));
  const QSet<QString> scopedFamilies = stringSet(
      fixture.value(QStringLiteral("scope")).toObject()
          .value(QStringLiteral("families")).toArray());
  for (const QString& path : {
           QStringLiteral("theme"), QStringLiteral("themeVariables.*"),
           QStringLiteral("fontFamily"), QStringLiteral("maxTextSize"),
           QStringLiteral("securityLevel"),
           QStringLiteral("deterministicIds"),
           QStringLiteral("deterministicIDSeed"),
           QStringLiteral("themeCSS")}) {
    QSet<QString> expectedScope = scopedFamilies;
    if (path == QLatin1String("fontFamily"))
      expectedScope.remove(QStringLiteral("wardley-beta"));
    require(stringSet(byPath.value(path)
                          .value(QStringLiteral("families")).toArray()) ==
                expectedScope,
            QStringLiteral("Global %1 scope must cover every native family")
                .arg(path));
  }

  const MermaidRenderEntry infoEntry = render(QStringLiteral(
      "%%{init: {\"fontFamily\":\"Noto Sans\",\"themeVariables\":{"
      "\"textColor\":\"#ff0000\"}}}%%\ninfo"));
  const auto* infoScene =
      dynamic_cast<const muffin::mermaid::info::InfoScene*>(
          infoEntry.scene.get());
  require(infoEntry.status == MermaidRenderStatus::Ready && infoScene &&
              infoScene->style.fontFamily == QLatin1String("Noto Sans") &&
              infoScene->style.textColor == QLatin1String("#ff0000") &&
              !infoEntry.metadata.svgEmitViewBox,
          QStringLiteral("Info shared font/theme/SVG config did not reach the scene"));
  const MermaidRenderEntry eventEntry = render(QStringLiteral(
      "%%{init: {\"eventmodeling\": {\"padding\": 80, "
      "\"useMaxWidth\": false, \"rowHeight\": 96}, "
      "\"themeVariables\": {\"emEventFill\": \"#123456\"}}}%%\n"
      "eventmodeling\ntf 1 evt Created"));
  const auto* eventScene =
      dynamic_cast<const muffin::mermaid::eventmodeling::EventModelingScene*>(
          eventEntry.scene.get());
  require(eventEntry.status == MermaidRenderStatus::Ready && eventScene &&
              eventScene->padding == 80.0 && !eventScene->useMaxWidth &&
              !eventEntry.metadata.svgUseMaxWidth &&
              eventScene->boxes.size() == 1 &&
              eventScene->boxes.first().fill == QLatin1String("#123456"),
          QStringLiteral("Event Modeling live config/theme did not reach the scene"));
  const QString ishikawaSource = QStringLiteral(
      "%%{init: {\"ishikawa\": {\"diagramPadding\": 80, "
      "\"useMaxWidth\": false}, \"look\": \"handDrawn\", "
      "\"handDrawnSeed\": 17}}%%\n"
      "ishikawa\nEffect\n  Cause A\n  Cause B");
  const MermaidRenderEntry ishikawaEntry = render(ishikawaSource);
  const auto* ishikawaScene =
      dynamic_cast<const muffin::mermaid::ishikawa::IshikawaScene*>(
          ishikawaEntry.scene.get());
  require(ishikawaEntry.status == MermaidRenderStatus::Ready &&
              ishikawaScene && ishikawaScene->padding == 80.0 &&
              !ishikawaScene->useMaxWidth &&
              !ishikawaEntry.metadata.svgUseMaxWidth &&
              ishikawaScene->style.look == QLatin1String("handDrawn") &&
              pngImage(ishikawaSource).size() == ishikawaEntry.naturalSize,
          QStringLiteral("Ishikawa live config did not reach scene/PNG export"));
  const QString vennSource = QStringLiteral(
      "%%{init: {\"venn\": {\"width\": 620, \"height\": 360, "
      "\"padding\": 20, \"useDebugLayout\": true, "
      "\"useMaxWidth\": false}, \"look\": \"handDrawn\", "
      "\"handDrawnSeed\": 17}}%%\n"
      "venn-beta\nset A: 10\nset B: 8\nunion A,B: 2\n"
      "text A note[\"Inside\"]");
  const MermaidRenderEntry vennEntry = render(vennSource);
  const auto* vennScene =
      dynamic_cast<const muffin::mermaid::venn::VennScene*>(
          vennEntry.scene.get());
  require(vennEntry.status == MermaidRenderStatus::Ready && vennScene &&
              vennScene->bounds.size() == QSizeF(620.0, 360.0) &&
              !vennScene->useMaxWidth && !vennEntry.metadata.svgUseMaxWidth &&
              vennScene->useDebugLayout && !vennScene->debugCircles.isEmpty() &&
              std::any_of(vennScene->areas.cbegin(), vennScene->areas.cend(),
                          [](const auto& area) { return area.rough; }) &&
              pngImage(vennSource).size() == vennEntry.naturalSize,
          QStringLiteral("Venn live config did not reach scene/PNG export"));
  const QString sankeySource = QStringLiteral(
      "%%{init: {\"fontFamily\":\"Noto Sans\",\"sankey\": {"
      "\"width\": 420, \"height\": 260, \"nodeWidth\": 24, "
      "\"nodePadding\": 4, \"nodeAlignment\": \"right\", "
      "\"showValues\": false, \"linkColor\": \"source\", "
      "\"labelStyle\": \"outlined\", \"useMaxWidth\": false, "
      "\"nodeColors\": {\"A\": \"#ff0000\"}}}}%%\n"
      "sankey-beta\nA,B,8\nB,C,5\nB,D,3");
  const MermaidRenderEntry sankeyEntry = render(sankeySource);
  const auto* sankeyScene =
      dynamic_cast<const muffin::mermaid::sankey::SankeyScene*>(
          sankeyEntry.scene.get());
  require(sankeyEntry.status == MermaidRenderStatus::Ready && sankeyScene &&
              sankeyScene->configuredWidth == 420.0 &&
              sankeyScene->configuredHeight == 260.0 &&
              !sankeyScene->useMaxWidth &&
              !sankeyEntry.metadata.svgUseMaxWidth &&
              sankeyScene->outlinedLabels &&
              sankeyScene->nodes.first().color == QLatin1String("#ff0000") &&
              sankeyScene->nodes.first().x1 - sankeyScene->nodes.first().x0 == 24.0 &&
              sankeyScene->links.first().stroke == QLatin1String("#ff0000") &&
              pngImage(sankeySource).size() == sankeyEntry.naturalSize,
          QStringLiteral("Sankey live config did not reach scene/PNG export"));
  const QString treemapSource = QStringLiteral(
      "%%{init: {\"fontFamily\":\"Noto Sans\",\"treemap\": {"
      "\"padding\": 3, \"diagramPadding\": 30, \"showValues\": false, "
      "\"nodeWidth\": 48, \"nodeHeight\": 28, \"useMaxWidth\": false, "
      "\"valueFormat\": \"$,.2f\"}, \"themeVariables\": {\"treemap\": {"
      "\"titleColor\": \"#ff0000\", \"titleFontSize\": \"24px\"}}}}%%\n"
      "treemap-beta\ntitle Revenue Map\n\"Root\"\n  \"A\": 1234.5\n  \"B\": 500");
  const MermaidRenderEntry treemapEntry = render(treemapSource);
  const auto* treemapScene =
      dynamic_cast<const muffin::mermaid::treemap::TreemapScene*>(
          treemapEntry.scene.get());
  const QSize treemapPngSize = pngImage(treemapSource).size();
  require(treemapEntry.status == MermaidRenderStatus::Ready && treemapScene &&
              treemapScene->configuredWidth == 480.0 &&
              treemapScene->configuredHeight == 310.0 &&
              !treemapScene->useMaxWidth &&
              !treemapEntry.metadata.svgUseMaxWidth &&
              treemapScene->title.fill == QLatin1String("#ff0000") &&
              treemapScene->title.fontSize == 24.0 &&
              !treemapScene->leaves.isEmpty() &&
              !treemapScene->leaves.first().value.visible &&
              treemapPngSize == treemapEntry.naturalSize,
          QStringLiteral("Treemap live config did not reach scene/PNG export: "
                         "status=%1 scene=%2 configured=%3x%4 useMax=%5 "
                         "metadataUseMax=%6 title=%7/%8 leaves=%9 valueVisible=%10 "
                         "png=%11x%12 natural=%13x%14")
              .arg(static_cast<int>(treemapEntry.status))
              .arg(treemapScene != nullptr)
              .arg(treemapScene ? treemapScene->configuredWidth : -1.0)
              .arg(treemapScene ? treemapScene->configuredHeight : -1.0)
              .arg(treemapScene ? treemapScene->useMaxWidth : true)
              .arg(treemapEntry.metadata.svgUseMaxWidth)
              .arg(treemapScene ? treemapScene->title.fill : QString())
              .arg(treemapScene ? treemapScene->title.fontSize : -1.0)
              .arg(treemapScene ? treemapScene->leaves.size() : -1)
              .arg(treemapScene && !treemapScene->leaves.isEmpty()
                       ? treemapScene->leaves.first().value.visible
                       : true)
              .arg(treemapPngSize.width())
              .arg(treemapPngSize.height())
              .arg(treemapEntry.naturalSize.width())
              .arg(treemapEntry.naturalSize.height()));
  const QString cynefinSource = QStringLiteral(
      "%%{init: {\"fontFamily\":\"Noto Sans\",\"cynefin\": {"
      "\"width\": 480, \"height\": 360, \"padding\": 12, "
      "\"useMaxWidth\": false, \"showDomainDescriptions\": false, "
      "\"boundaryAmplitude\": 0, \"seed\": 17}, "
      "\"themeVariables\": {\"cynefin\": {"
      "\"boundaryColor\": \"#ff0000\", \"domainFontSize\": 22}}}}%%\n"
      "cynefin-beta\nclear\n  \"Standardise\"");
  const MermaidRenderEntry cynefinEntry = render(cynefinSource);
  const QSize cynefinPngSize = pngImage(cynefinSource).size();
  const auto* cynefinScene =
      dynamic_cast<const muffin::mermaid::cynefin::CynefinScene*>(
          cynefinEntry.scene.get());
  require(cynefinEntry.status == MermaidRenderStatus::Ready && cynefinScene &&
              cynefinScene->configuredWidth == 480.0 &&
              cynefinScene->configuredHeight == 360.0 &&
              cynefinScene->configuredPadding == 12.0 &&
              !cynefinScene->useMaxWidth &&
              !cynefinEntry.metadata.svgUseMaxWidth &&
              cynefinScene->subtitles.isEmpty() &&
              !cynefinScene->boundaries.isEmpty() &&
              cynefinScene->boundaries.first().stroke == QLatin1String("#ff0000") &&
              !cynefinScene->labels.isEmpty() &&
              cynefinScene->labels.first().fontSize == 22.0 &&
              cynefinPngSize == cynefinEntry.naturalSize,
          QStringLiteral("Cynefin live config did not reach scene/PNG export: "
                         "status=%1 scene=%2 size=%3x%4 padding=%5 useMax=%6 "
                         "metadataUseMax=%7 subtitles=%8 boundaries=%9 "
                         "stroke=%10 labels=%11 font=%12 png=%13x%14 natural=%15x%16")
              .arg(static_cast<int>(cynefinEntry.status))
              .arg(cynefinScene != nullptr)
              .arg(cynefinScene ? cynefinScene->configuredWidth : -1.0)
              .arg(cynefinScene ? cynefinScene->configuredHeight : -1.0)
              .arg(cynefinScene ? cynefinScene->configuredPadding : -1.0)
              .arg(cynefinScene ? cynefinScene->useMaxWidth : true)
              .arg(cynefinEntry.metadata.svgUseMaxWidth)
              .arg(cynefinScene ? cynefinScene->subtitles.size() : -1)
              .arg(cynefinScene ? cynefinScene->boundaries.size() : -1)
              .arg(cynefinScene && !cynefinScene->boundaries.isEmpty()
                       ? cynefinScene->boundaries.first().stroke
                       : QString())
              .arg(cynefinScene ? cynefinScene->labels.size() : -1)
              .arg(cynefinScene && !cynefinScene->labels.isEmpty()
                       ? cynefinScene->labels.first().fontSize
                       : -1.0)
              .arg(cynefinPngSize.width())
              .arg(cynefinPngSize.height())
              .arg(cynefinEntry.naturalSize.width())
              .arg(cynefinEntry.naturalSize.height()));
  const QString cynefinFallbackSource =
      QString(cynefinSource).replace(
          QStringLiteral("\"fontFamily\":\"Noto Sans\""),
          QStringLiteral("\"fontFamily\":\"DefinitelyMissing, Noto Sans\""));
  const MermaidRenderEntry cynefinFallbackEntry = render(cynefinFallbackSource);
  const auto* cynefinFallbackScene =
      dynamic_cast<const muffin::mermaid::cynefin::CynefinScene*>(
          cynefinFallbackEntry.scene.get());
  require(cynefinFallbackEntry.status == MermaidRenderStatus::Ready &&
              cynefinFallbackScene && cynefinScene &&
              cynefinFallbackScene->labels.first().bounds ==
                  cynefinScene->labels.first().bounds &&
              cynefinFallbackEntry.naturalSize == cynefinEntry.naturalSize,
          QStringLiteral("Cynefin CSS font-family fallback list drifted"));

  const QString wardleySource = QStringLiteral(
      "%%{init: {\"wardley-beta\":{\"width\":480,\"height\":320,"
      "\"padding\":10,\"nodeRadius\":12,\"nodeLabelOffset\":20,"
      "\"axisFontSize\":22,\"labelFontSize\":18,\"showGrid\":true,"
      "\"useMaxWidth\":false},\"themeVariables\":{\"wardley\":{"
      "\"backgroundColor\":\"#010203\",\"axisColor\":\"#070809\","
      "\"componentFill\":\"#040506\",\"annotationFill\":\"#ff00ff\"}}}}%%\n"
      "wardley-beta\ncomponent A [0.5,0.5]");
  const MermaidRenderEntry wardleyEntry = render(wardleySource);
  const auto* wardleyScene =
      dynamic_cast<const muffin::mermaid::wardley::WardleyScene*>(
          wardleyEntry.scene.get());
  require(wardleyEntry.status == MermaidRenderStatus::Ready && wardleyScene &&
              wardleyScene->config.width == 900.0 &&
              wardleyScene->config.height == 600.0 &&
              wardleyScene->config.padding == 48.0 &&
              wardleyScene->config.nodeRadius == 6.0 &&
              !wardleyScene->config.showGrid && wardleyScene->useMaxWidth &&
              wardleyEntry.metadata.svgUseMaxWidth &&
              wardleyScene->style.backgroundColor == QLatin1String("#010203") &&
              wardleyScene->style.axisColor == QLatin1String("#070809") &&
              wardleyScene->style.componentFill == QLatin1String("#040506") &&
              wardleyScene->style.annotationFill == QLatin1String("white") &&
              wardleyEntry.naturalSize == QSize(900, 600) &&
              pngImage(wardleySource).size() == wardleyEntry.naturalSize,
          QStringLiteral("Wardley source-inert config/live theme projection drifted"));

  const QString architectureSource = QStringLiteral(
      "%%{init:{\"fontFamily\":\"Noto Sans\",\"architecture\":{"
      "\"useMaxWidth\":false,\"padding\":18,\"iconSize\":52,"
      "\"fontSize\":21,\"randomize\":false,\"nodeSeparation\":32,"
      "\"idealEdgeLengthMultiplier\":2.25,\"edgeElasticity\":0.2,"
      "\"numIter\":600,\"seed\":9},\"themeVariables\":{"
      "\"archEdgeColor\":\"#112233\","
      "\"archEdgeArrowColor\":\"#445566\","
      "\"archGroupBorderColor\":\"#778899\"}}}%%\n"
      "architecture-beta\nservice a(server)[A]\nservice b(database)[B]\n"
      "a:R --> L:b");
  const MermaidRenderEntry architectureEntry = render(architectureSource);
  const auto* architectureScene =
      dynamic_cast<const muffin::mermaid::architecture::ArchitectureScene*>(
          architectureEntry.scene.get());
  require(architectureEntry.status == MermaidRenderStatus::Ready &&
              architectureScene && !architectureScene->useMaxWidth &&
              !architectureEntry.metadata.svgUseMaxWidth &&
              architectureScene->config.padding == QJsonValue(18.0) &&
              architectureScene->config.iconSize == QJsonValue(52.0) &&
              architectureScene->config.fontSize == QJsonValue(21.0) &&
              architectureScene->config.nodeSeparation == QJsonValue(32.0) &&
              architectureScene->config.seed == QJsonValue(9.0) &&
              architectureScene->style.edgeColor == QLatin1String("#112233") &&
              architectureScene->style.arrowColor == QLatin1String("#445566") &&
              architectureScene->style.groupBorderColor ==
                  QLatin1String("#778899") &&
              pngImage(architectureSource).size() ==
                  architectureEntry.naturalSize,
          QStringLiteral("Architecture config/theme production projection drifted"));

  const QString c4Source = QStringLiteral(
      "%%{init:{\"fontFamily\":\"Noto Sans\",\"c4\":{"
      "\"useMaxWidth\":false,\"diagramMarginX\":24,"
      "\"diagramMarginY\":18,\"width\":180,\"height\":70,"
      "\"personFontFamily\":\"Noto Sans\",\"personFontSize\":20,"
      "\"personFontWeight\":\"bold\",\"person_bg_color\":\"#123456\","
      "\"person_border_color\":\"#654321\"}}}%%\n"
      "C4Context\nPerson(user, \"User\")");
  const MermaidRenderEntry c4Entry = render(c4Source);
  const auto* c4Scene =
      dynamic_cast<const muffin::mermaid::c4::C4Scene*>(c4Entry.scene.get());
  require(c4Entry.status == MermaidRenderStatus::Ready && c4Scene &&
              !c4Scene->useMaxWidth && !c4Entry.metadata.svgUseMaxWidth &&
              c4Scene->config.diagramMarginX == 24.0 &&
              c4Scene->config.diagramMarginY == 18.0 &&
              c4Scene->config.width == 180.0 &&
              c4Scene->config.height == 70.0 &&
              c4Scene->config.fonts.value(QStringLiteral("person")).size == 20.0 &&
              c4Scene->config.fonts.value(QStringLiteral("person")).weight ==
                  QLatin1String("bold") &&
              c4Scene->config.backgroundColors.value(QStringLiteral("person")) ==
                  QLatin1String("#123456") &&
              c4Scene->config.borderColors.value(QStringLiteral("person")) ==
                  QLatin1String("#654321") &&
              pngImage(c4Source).size() == c4Entry.naturalSize,
          QStringLiteral("C4 config production projection drifted"));

  const QString railroadSource = QStringLiteral(
      "%%{init: {\"railroad\": {\"useMaxWidth\": false, "
      "\"padding\": 24, \"fontSize\": 20, "
      "\"terminalFill\": \"#123456\", "
      "\"ruleNameColor\": \"#654321\"}}}%%\n"
      "railroad-ebnf-beta\nA = 'a';");
  const MermaidRenderEntry railroadEntry = render(railroadSource);
  const auto* railroadScene =
      dynamic_cast<const muffin::mermaid::railroad::RailroadScene*>(
          railroadEntry.scene.get());
  require(railroadEntry.status == MermaidRenderStatus::Ready &&
              railroadScene && !railroadScene->config.useMaxWidth &&
              !railroadEntry.metadata.svgUseMaxWidth &&
              railroadScene->config.padding == 24.0 &&
              railroadScene->config.fontSize == 20.0 &&
              railroadScene->config.terminalFill == QLatin1String("#123456") &&
              railroadScene->config.ruleNameColor == QLatin1String("#654321") &&
              pngImage(railroadSource).size() == railroadEntry.naturalSize,
          QStringLiteral("Railroad config production projection drifted"));

  const QJsonObject declaredSummary =
      fixture.value(QStringLiteral("summary")).toObject();
  for (auto it = statusCounts.cbegin(); it != statusCounts.cend(); ++it)
    require(declaredSummary.value(it.key()).toInt(-1) == it.value(),
            QStringLiteral("Summary count drifted for %1").arg(it.key()));

  require(byPath.value(QStringLiteral("flowchart.curve"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("parity") &&
              stringSet(byPath.value(QStringLiteral("flowchart.curve"))
                            .value(QStringLiteral("native")).toArray())
                  .contains(QStringLiteral("interaction")) &&
              stringSet(byPath.value(QStringLiteral("flowchart.curve"))
                            .value(QStringLiteral("families")).toArray()) ==
                  QSet<QString>{QStringLiteral("flowchart"),
                                QStringLiteral("swimlane")},
          QStringLiteral("Flowchart curve matrix row lost full native parity"));
  for (const QString& path : {
           QStringLiteral("elk.mergeEdges"),
           QStringLiteral("elk.nodePlacementStrategy"),
           QStringLiteral("elk.cycleBreakingStrategy"),
           QStringLiteral("elk.forceNodeModelOrder"),
           QStringLiteral("elk.considerModelOrder")}) {
    require(byPath.value(path).value(QStringLiteral("status")).toString() ==
                    QLatin1String("upstream-inert") &&
                stringSet(byPath.value(path)
                              .value(QStringLiteral("families")).toArray()) ==
                    QSet<QString>{QStringLiteral("flowchart")},
            QStringLiteral("ELK fallback config classification drifted: %1")
                .arg(path));
  }
  require(byPath.value(QStringLiteral("swimlane.useWidth"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("swimlane.useMaxWidth"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("swimlane.lineHops"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("parity") &&
              byPath.value(QStringLiteral("swimlane.ignoreCrossLaneEdges"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("parity") &&
              byPath.value(QStringLiteral("swimlane.optimizeRanksByCrossings"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("swimlane.automaticLaneOrdering"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("parity"),
          QStringLiteral("Swimlane config policy rows drifted"));
  require(byPath.value(QStringLiteral("sequence.forceMenus"))
                  .value(QStringLiteral("status")).toString() ==
              QLatin1String("parity") &&
              byPath.value(QStringLiteral("class.nodeSpacing"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("state.nodeSpacing"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("xyChart.width"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("xyChart.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("timeline.leftMargin"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("timeline.padding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("timeline.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("timeline.disableMulticolor"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("timeline.taskFontSize"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("packet.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("packet.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("packet.showBits"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("kanban.sectionWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("kanban.ticketBaseUrl"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("kanban.padding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("kanban.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("mindmap.padding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("mindmap.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("mindmap.maxNodeWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("mindmap.layoutAlgorithm"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("gitGraph.parallelCommits"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("gitGraph.nodeLabel"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("gitGraph.arrowMarkerAbsolute"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("c4.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("c4.c4ShapeInRow"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("c4.personFont"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("api-only") &&
              byPath.value(QStringLiteral("treeView.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("treeView.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("treeView.rowIndent"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("treeView.showIcons"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("eventmodeling.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("eventmodeling.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("eventmodeling.padding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("eventmodeling.rowHeight"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("ishikawa.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("ishikawa.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("ishikawa.diagramPadding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("venn.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("venn.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("venn.useDebugLayout"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("sankey.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("sankey.nodeAlignment"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("sankey.nodeColors"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("treemap.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("treemap.showValues"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("treemap.valueFormat"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("treemap.borderWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("cynefin.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("cynefin.showDomainDescriptions"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("cynefin.boundaryAmplitude"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("cynefin.seed"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("wardley-beta.width"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("wardley-beta.showGrid"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("wardley-beta.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("architecture.useWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("upstream-inert") &&
              byPath.value(QStringLiteral("architecture.useMaxWidth"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("architecture.padding"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("architecture.randomize"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity") &&
              byPath.value(QStringLiteral("markdownAutoWrap"))
                      .value(QStringLiteral("status")).toString() ==
                  QLatin1String("parity"),
          QStringLiteral("Critical config status rows drifted"));
  require(stringSet(byPath.value(QStringLiteral("mindmap.padding"))
                        .value(QStringLiteral("families")).toArray()) ==
              QSet<QString>{QStringLiteral("mindmap"), QStringLiteral("kanban")} &&
              stringSet(byPath.value(QStringLiteral("mindmap.useMaxWidth"))
                            .value(QStringLiteral("families")).toArray()) ==
                  QSet<QString>{QStringLiteral("mindmap"), QStringLiteral("kanban")} &&
              stringSet(byPath.value(QStringLiteral("markdownAutoWrap"))
                            .value(QStringLiteral("families")).toArray()) ==
                  QSet<QString>{QStringLiteral("mindmap"), QStringLiteral("kanban")},
          QStringLiteral("Mindmap/Kanban cross-family config scope drifted"));
  for (const QString& path : {
           QStringLiteral("deterministicIds"),
           QStringLiteral("deterministicIDSeed"),
           QStringLiteral("flowchart.useMaxWidth"),
           QStringLiteral("journey.useMaxWidth"),
           QStringLiteral("quadrantChart.useMaxWidth"),
           QStringLiteral("sequence.useMaxWidth"),
           QStringLiteral("state.useMaxWidth"),
           QStringLiteral("timeline.useMaxWidth"),
           QStringLiteral("packet.useMaxWidth"),
           QStringLiteral("mindmap.useMaxWidth"),
           QStringLiteral("c4.useMaxWidth"),
           QStringLiteral("treeView.useMaxWidth"),
           QStringLiteral("eventmodeling.useMaxWidth"),
           QStringLiteral("ishikawa.useMaxWidth"),
           QStringLiteral("venn.useMaxWidth"),
           QStringLiteral("sankey.useMaxWidth"),
           QStringLiteral("treemap.useMaxWidth"),
           QStringLiteral("cynefin.useMaxWidth"),
           QStringLiteral("architecture.useMaxWidth")}) {
    require(byPath.value(path).value(QStringLiteral("status")).toString() ==
                QLatin1String("parity") &&
                stringSet(byPath.value(path).value(QStringLiteral("native")).toArray())
                    .contains(QStringLiteral("export")),
            QStringLiteral("Native SVG config parity drifted for %1").arg(path));
  }
  for (const QString& path : {
           QStringLiteral("arrowMarkerAbsolute"),
           QStringLiteral("flowchart.arrowMarkerAbsolute"),
           QStringLiteral("sequence.arrowMarkerAbsolute"),
           QStringLiteral("class.arrowMarkerAbsolute"),
           QStringLiteral("state.arrowMarkerAbsolute")}) {
    require(byPath.value(path).value(QStringLiteral("status")).toString() ==
                QLatin1String("deferred"),
            QStringLiteral("SVG marker URL config was claimed without evidence: %1")
                .arg(path));
  }

  // Flowchart diagramPadding must affect both the editor viewport contract and
  // the one-shot PNG export, not merely survive preprocessing.
  const QString paddedFlow = QStringLiteral(
      "%%{init: {\"flowchart\": {\"diagramPadding\": 30}}}%%\n"
      "flowchart TB\nA --> B");
  const MermaidRenderEntry flow = render(paddedFlow);
  require(flow.status == MermaidRenderStatus::Ready && flow.scene &&
              flow.metadata.diagramPadding == 30.0 &&
              flow.naturalSize.width() ==
                  qCeil(flow.metadata.contentSize.width() + 60.0) &&
              flow.naturalSize.height() ==
                  qCeil(flow.metadata.contentSize.height() + 60.0),
          QStringLiteral("flowchart.diagramPadding did not reach viewport geometry"));
  const QImage flowPng = pngImage(paddedFlow);
  require(!flowPng.isNull() && flowPng.size() == flow.naturalSize,
          QStringLiteral("flowchart.diagramPadding did not reach PNG export"));

  // Rounded is Mermaid's custom path generator rather than a d3 curve. Cover
  // both config-level selection and per-edge metadata through the render cache.
  const QString roundedFlow = QStringLiteral(
      "%%{init: {\"flowchart\": {\"curve\": \"rounded\"}}}%%\n"
      "flowchart TB\nA[Start] --> B[Left]\nA --> C[Right]\n"
      "B --> D[End]\nC --> D\nA --> D");
  const MermaidRenderEntry rounded = render(roundedFlow);
  const auto* roundedScene = dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(rounded.scene.get());
  require(rounded.status == MermaidRenderStatus::Ready && rounded.scene && roundedScene,
          QStringLiteral("flowchart.curve rounded did not render"));
  bool sawRoundedCorner = false;
  for (const auto& edge : roundedScene->edges)
    sawRoundedCorner = sawRoundedCorner || edge.path.contains(QLatin1Char('Q'));
  require(sawRoundedCorner && !pngImage(roundedFlow).isNull(),
          QStringLiteral("flowchart.curve rounded did not reach scene/PNG paint"));

  const QString swimlaneBody = QStringLiteral(
      "swimlane-beta TB\n"
      "subgraph one[One]\n  A[Start] --> B[Done]\nend\n"
      "subgraph two[Two]\n  C[Review] --> D[Ship]\nend\n"
      "B --> C");
  const MermaidRenderEntry nativeSwimlane = render(swimlaneBody);
  const MermaidRenderEntry linearSwimlane = render(
      QStringLiteral("%%{init: {\"flowchart\": {\"curve\": \"linear\"}}}%%\n") +
      swimlaneBody);
  const MermaidRenderEntry dagreSwimlane = render(
      QStringLiteral("%%{init: {\"layout\": \"dagre\"}}%%\n") + swimlaneBody);
  const auto* nativeSwimlaneScene =
      dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(
          nativeSwimlane.scene.get());
  const auto* linearSwimlaneScene =
      dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(
          linearSwimlane.scene.get());
  const auto* dagreSwimlaneScene =
      dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(
          dagreSwimlane.scene.get());
  require(nativeSwimlane.status == MermaidRenderStatus::Ready &&
              linearSwimlane.status == MermaidRenderStatus::Ready &&
              dagreSwimlane.status == MermaidRenderStatus::Ready &&
              nativeSwimlaneScene && linearSwimlaneScene &&
              dagreSwimlaneScene &&
              std::any_of(nativeSwimlaneScene->clusters.cbegin(),
                          nativeSwimlaneScene->clusters.cend(),
                          [](const auto& cluster) { return cluster.swimlane; }) &&
              std::none_of(dagreSwimlaneScene->clusters.cbegin(),
                           dagreSwimlaneScene->clusters.cend(),
                           [](const auto& cluster) { return cluster.swimlane; }) &&
              std::none_of(linearSwimlaneScene->edges.cbegin(),
                           linearSwimlaneScene->edges.cend(),
                           [](const auto& edge) {
                             return edge.path.contains(QLatin1Char('Q')) ||
                                    edge.path.contains(QLatin1Char('C'));
                           }),
          QStringLiteral("Swimlane flowchart config or Dagre override drifted"));
  const MermaidRenderEntry unsupportedSwimlane = render(
      QStringLiteral("%%{init: {\"layout\": \"elk\"}}%%\n") + swimlaneBody);
  require(unsupportedSwimlane.status == MermaidRenderStatus::Unsupported &&
              unsupportedSwimlane.diagnostic.stage ==
                  QLatin1String("configuration") &&
              unsupportedSwimlane.diagnostic.code ==
                  QLatin1String("unsupported-layout-engine"),
          QStringLiteral("Swimlane unsupported layout must be explicit"));

  const QString edgeRoundedFlow = QStringLiteral(
      "flowchart LR\nA[Start] roundedEdge@--> B[Middle] --> C[End]\n"
      "roundedEdge@{ curve: rounded }");
  const MermaidRenderEntry edgeRounded = render(edgeRoundedFlow);
  const auto* edgeRoundedScene = dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(edgeRounded.scene.get());
  require(edgeRounded.status == MermaidRenderStatus::Ready &&
              edgeRounded.scene && edgeRoundedScene && edgeRoundedScene->edges.size() == 2,
          QStringLiteral("Per-edge rounded flow did not render"));
  const auto roundedEdge = std::find_if(
      edgeRoundedScene->edges.cbegin(), edgeRoundedScene->edges.cend(),
      [](const auto& edge) { return edge.id == QLatin1String("roundedEdge"); });
  require(roundedEdge != edgeRoundedScene->edges.cend() &&
              !roundedEdge->path.contains(QLatin1Char('C')) &&
              std::any_of(edgeRoundedScene->edges.cbegin(),
                          edgeRoundedScene->edges.cend(),
                          [](const auto& edge) {
                            return edge.id != QLatin1String("roundedEdge") &&
                                   edge.path.contains(QLatin1Char('C'));
                          }),
          QStringLiteral("Per-edge rounded override leaked to the default edge"));

  // State v2 forwards both spacing axes to Dagre.
  const QString stateSource = QStringLiteral(
      "stateDiagram-v2\n[*] --> A\nA --> B\nA --> C\n"
      "B --> [*]\nC --> [*]");
  const QString spacedState = QStringLiteral(
      "%%{init: {\"state\": {\"nodeSpacing\": 140, "
      "\"rankSpacing\": 160}}}%%\n") + stateSource;
  const MermaidRenderEntry normalState = render(stateSource);
  const MermaidRenderEntry configuredState = render(spacedState);
  require(normalState.status == MermaidRenderStatus::Ready &&
              configuredState.status == MermaidRenderStatus::Ready,
          QStringLiteral("State spacing probes did not render"));
  const qreal normalSiblingGap = std::abs(
      stateCenter(normalState, QStringLiteral("B")).x() -
      stateCenter(normalState, QStringLiteral("C")).x());
  const qreal configuredSiblingGap = std::abs(
      stateCenter(configuredState, QStringLiteral("B")).x() -
      stateCenter(configuredState, QStringLiteral("C")).x());
  const qreal normalRankGap = std::abs(
      stateCenter(normalState, QStringLiteral("A")).y() -
      stateCenter(normalState, QStringLiteral("B")).y());
  const qreal configuredRankGap = std::abs(
      stateCenter(configuredState, QStringLiteral("A")).y() -
      stateCenter(configuredState, QStringLiteral("B")).y());
  require(configuredSiblingGap > normalSiblingGap + 50.0 &&
              configuredRankGap > normalRankGap + 80.0,
          QStringLiteral("state nodeSpacing/rankSpacing did not alter placement"));

  // ER forwards both spacing axes to Dagre (Phase 1: er.nodeSpacing/rankSpacing).
  const QString erSource = QStringLiteral(
      "erDiagram\nA ||--o{ B : x\nA ||--o{ C : y");
  const QString spacedEr = QStringLiteral(
      "%%{init: {\"er\": {\"nodeSpacing\": 220, \"rankSpacing\": 180}}}%%\n") + erSource;
  const MermaidRenderEntry normalEr = render(erSource);
  const MermaidRenderEntry configuredEr = render(spacedEr);
  const auto* normalErScene = dynamic_cast<const muffin::mermaid::er::ErScene*>(normalEr.scene.get());
  const auto* configuredErScene = dynamic_cast<const muffin::mermaid::er::ErScene*>(configuredEr.scene.get());
  require(normalEr.status == MermaidRenderStatus::Ready &&
              configuredEr.status == MermaidRenderStatus::Ready &&
              normalErScene && configuredErScene,
          QStringLiteral("ER spacing probes did not render"));
  const qreal normalErSibling = std::abs(
      erCenter(normalEr, QStringLiteral("B")).x() -
      erCenter(normalEr, QStringLiteral("C")).x());
  const qreal configuredErSibling = std::abs(
      erCenter(configuredEr, QStringLiteral("B")).x() -
      erCenter(configuredEr, QStringLiteral("C")).x());
  const qreal normalErRank = std::abs(
      erCenter(normalEr, QStringLiteral("A")).y() -
      erCenter(normalEr, QStringLiteral("B")).y());
  const qreal configuredErRank = std::abs(
      erCenter(configuredEr, QStringLiteral("A")).y() -
      erCenter(configuredEr, QStringLiteral("B")).y());
  require(configuredErSibling > normalErSibling + 40.0 &&
              configuredErRank > normalErRank + 40.0,
          QStringLiteral("er nodeSpacing/rankSpacing did not alter placement"));

  // Root %%{wrap}%% and showSequenceNumbers are separate configuration paths.
  const QString wrappedSequence = QStringLiteral(
      "%%{wrap}%%\nsequenceDiagram\n"
      "participant A as A deliberately long participant label for wrapping\n"
      "participant B as Worker\nA->>B: a deliberately long message for wrapping");
  const MermaidRenderEntry wrapped = render(wrappedSequence);
  const auto* wrappedScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(wrapped.scene.get());
  require(wrapped.status == MermaidRenderStatus::Ready && wrappedScene &&
              !wrappedScene->participantLabels.isEmpty() &&
              wrappedScene->participantLabels.first().richText.text
                  .contains(QLatin1Char('\n')),
          QStringLiteral("Root wrap directive did not reach sequence text/layout"));
  const QString numberedSequence = QStringLiteral(
      "%%{init: {\"sequence\": {\"showSequenceNumbers\": true}}}%%\n"
      "sequenceDiagram\nA->>B: first\nB-->>A: second");
  const MermaidRenderEntry numbered = render(numberedSequence);
  const auto* numberedScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(numbered.scene.get());
  require(numbered.status == MermaidRenderStatus::Ready &&
              numberedScene &&
              numberedScene->sequenceNumbers.size() == 2,
          QStringLiteral("sequence.showSequenceNumbers did not reach text/paint"));

  const QString sequenceWithMenu = QStringLiteral(
      "sequenceDiagram\nparticipant A as Browser\nparticipant B as API\n"
      "links A: {\"Documentation\": \"https://example.com/docs\"}\n"
      "A->>B: request");
  const QString forcedSequence = QStringLiteral(
      "%%{init: {\"sequence\": {\"forceMenus\": true}}}%%\n") +
      sequenceWithMenu;
  const MermaidRenderEntry closedMenu = render(sequenceWithMenu);
  const MermaidRenderEntry forcedMenu = render(forcedSequence);
  const auto* closedMenuScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(closedMenu.scene.get());
  const auto* forcedMenuScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(forcedMenu.scene.get());
  require(closedMenu.status == MermaidRenderStatus::Ready &&
              forcedMenu.status == MermaidRenderStatus::Ready &&
              closedMenuScene && forcedMenuScene &&
              !closedMenuScene->forceMenus &&
              forcedMenuScene->forceMenus &&
              closedMenuScene->menus.size() == 1 &&
              forcedMenuScene->menus.size() == 1 &&
              closedMenu.naturalSize == forcedMenu.naturalSize,
          QStringLiteral("sequence.forceMenus lost scene or stable viewport semantics"));
  const QImage closedMenuPng = pngImage(sequenceWithMenu);
  const QImage forcedMenuPng = pngImage(forcedSequence);
  require(!closedMenuPng.isNull() && !forcedMenuPng.isNull() &&
              closedMenuPng.size() == forcedMenuPng.size() &&
              closedMenuPng != forcedMenuPng,
          QStringLiteral("sequence.forceMenus did not affect deterministic PNG paint"));

  // sequence per-label font weights reach the scene and paint (matrix parity
  // evidence for actor/note/message FontWeight).
  const QString weightedSequence = QStringLiteral(
      "%%{init: {\"sequence\": {\"messageFontWeight\": 700}}}%%\n"
      "sequenceDiagram\nAlice->>Bob: hello");
  const MermaidRenderEntry weighted = render(weightedSequence);
  const auto* weightedScene =
      dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(weighted.scene.get());
  require(weighted.status == MermaidRenderStatus::Ready && weightedScene &&
              !weightedScene->messageLabels.isEmpty() &&
              weightedScene->messageLabels.first().richText.baseWeight == QFont::Bold,
          QStringLiteral("sequence.messageFontWeight did not reach the scene"));
  require(pngImage(weightedSequence) !=
              pngImage(QStringLiteral("sequenceDiagram\nAlice->>Bob: hello")),
          QStringLiteral("sequence.messageFontWeight did not affect PNG paint"));

  // Journey's live family configuration reaches both deterministic geometry
  // and the painter. Dead Sequence-era fields and source-sanitized arrays are
  // classified separately in the generated matrix.
  const QString journeySource = QStringLiteral(
      "%%{init: {\"journey\": {\"useMaxWidth\": false, "
      "\"diagramMarginX\": 80, \"diagramMarginY\": 20, "
      "\"leftMargin\": 175, \"maxLabelWidth\": 42, "
      "\"width\": 120, \"height\": 60, \"boxTextMargin\": 9, "
      "\"taskFontSize\": 18, \"taskFontFamily\": \"Noto Sans\", "
      "\"taskMargin\": 70, \"textPlacement\": \"tspan\", "
      "\"titleColor\": \"#123456\", "
      "\"titleFontFamily\": \"Noto Sans\", "
      "\"titleFontSize\": \"20px\"}}}%%\n"
      "journey\ntitle Configured\nsection S\ntask: 5: Alice");
  const MermaidRenderEntry journeyEntry = render(journeySource);
  const auto* journeyScene =
      dynamic_cast<const muffin::mermaid::journey::JourneyScene*>(
          journeyEntry.scene.get());
  require(journeyEntry.status == MermaidRenderStatus::Ready && journeyScene &&
              !journeyScene->config.useMaxWidth &&
              journeyScene->config.diagramMarginX == 80.0 &&
              journeyScene->config.diagramMarginY == 20.0 &&
              journeyScene->config.leftMargin == 175.0 &&
              journeyScene->config.maxLabelWidth == 42.0 &&
              journeyScene->config.width == 120.0 &&
              journeyScene->config.height == 60.0 &&
              journeyScene->config.boxTextMargin == 9.0 &&
              journeyScene->config.taskFontSize == 18.0 &&
              journeyScene->config.taskMargin == 70.0 &&
              journeyScene->config.textPlacement == QLatin1String("tspan") &&
              journeyScene->config.titleColor == QLatin1String("#123456") &&
              journeyScene->config.titleFontSize == 20.0,
          QStringLiteral("Journey live config did not reach the scene"));
  require(pngImage(journeySource) != pngImage(QStringLiteral(
              "journey\ntitle Configured\nsection S\ntask: 5: Alice")),
          QStringLiteral("Journey live config did not affect PNG output"));

  // Radar's ten live layout/viewport fields reach the immutable scene and
  // produce a different raster. Style keys under config.radar are deliberately
  // inert upstream; their live counterparts are covered through
  // themeVariables.radar by MermaidRadarEdgeParityTest.
  const QString radarSource = QStringLiteral(
      "%%{init: {\"radar\": {\"useMaxWidth\": false, "
      "\"width\": 420, \"height\": 300, \"marginTop\": 10, "
      "\"marginRight\": 20, \"marginBottom\": 30, "
      "\"marginLeft\": 40, \"axisScaleFactor\": 0.8, "
      "\"axisLabelFactor\": 1.2, \"curveTension\": 0.1}}}%%\n"
      "radar-beta\naxis A,B,C\ncurve C {1,2,3}");
  const MermaidRenderEntry radarEntry = render(radarSource);
  const auto* radarScene =
      dynamic_cast<const muffin::mermaid::radar::RadarScene*>(
          radarEntry.scene.get());
  require(radarEntry.status == MermaidRenderStatus::Ready && radarScene &&
              !radarEntry.metadata.svgUseMaxWidth &&
              radarScene->config.width == 420.0 &&
              radarScene->config.height == 300.0 &&
              radarScene->config.marginTop == 10.0 &&
              radarScene->config.marginRight == 20.0 &&
              radarScene->config.marginBottom == 30.0 &&
              radarScene->config.marginLeft == 40.0 &&
              radarScene->config.axisScaleFactor == 0.8 &&
              radarScene->config.axisLabelFactor == 1.2 &&
              radarScene->config.curveTension == 0.1,
          QStringLiteral("Radar live config did not reach the scene"));
  require(pngImage(radarSource) != pngImage(QStringLiteral(
              "radar-beta\naxis A,B,C\ncurve C {1,2,3}")),
          QStringLiteral("Radar live config did not affect PNG output"));

  // XYChart consumes all eleven family-specific fields. Base useWidth and
  // useMaxWidth remain upstream-inert; the renderer always exports with
  // configureSvgSize(..., true).
  const QString xyChartSource = QStringLiteral(
      "%%{init: {\"xyChart\": {\"width\": 640, \"height\": 420, "
      "\"titleFontSize\": 32, \"titlePadding\": 23, "
      "\"showDataLabel\": true, \"showDataLabelOutsideBar\": true, "
      "\"showTitle\": false, \"chartOrientation\": \"horizontal\", "
      "\"plotReservedSpacePercent\": 70, "
      "\"xAxis\": {\"showLabel\": false, \"labelFontSize\": 24, "
      "\"labelPadding\": 17, \"showTitle\": false, "
      "\"titleFontSize\": 26, \"titlePadding\": 19, "
      "\"showTick\": false, \"tickLength\": 13, \"tickWidth\": 7, "
      "\"showAxisLine\": false, \"axisLineWidth\": 9, "
      "\"labelRotation\": 45}, "
      "\"yAxis\": {\"labelFontSize\": 22, \"axisLineWidth\": 8}}}}%%\n"
      "xychart-beta\ntitle Sales\nx-axis [A,B,C]\n"
      "y-axis 0 --> 3\nbar [1,2,3]");
  const MermaidRenderEntry xyChartEntry = render(xyChartSource);
  const auto* xyChartScene =
      dynamic_cast<const muffin::mermaid::xychart::XYChartScene*>(
          xyChartEntry.scene.get());
  require(xyChartEntry.status == MermaidRenderStatus::Ready && xyChartScene &&
              xyChartScene->config.width == 640.0 &&
              xyChartScene->config.height == 420.0 &&
              xyChartScene->config.titleFontSize == 32.0 &&
              xyChartScene->config.titlePadding == 23.0 &&
              xyChartScene->config.showDataLabel &&
              xyChartScene->config.showDataLabelOutsideBar &&
              !xyChartScene->config.showTitle &&
              xyChartScene->config.orientation ==
                  muffin::mermaid::xychart::XYChartOrientation::Horizontal &&
              xyChartScene->config.plotReservedSpacePercent == 70.0 &&
              !xyChartScene->config.xAxis.showLabel &&
              xyChartScene->config.xAxis.labelFontSize == 24.0 &&
              xyChartScene->config.xAxis.labelPadding == 17.0 &&
              !xyChartScene->config.xAxis.showTitle &&
              xyChartScene->config.xAxis.titleFontSize == 26.0 &&
              xyChartScene->config.xAxis.titlePadding == 19.0 &&
              !xyChartScene->config.xAxis.showTick &&
              xyChartScene->config.xAxis.tickLength == 13.0 &&
              xyChartScene->config.xAxis.tickWidth == 7.0 &&
              !xyChartScene->config.xAxis.showAxisLine &&
              xyChartScene->config.xAxis.axisLineWidth == 9.0 &&
              xyChartScene->config.xAxis.labelRotation == 45.0 &&
              xyChartScene->config.yAxis.labelFontSize == 22.0 &&
              xyChartScene->config.yAxis.axisLineWidth == 8.0 &&
              xyChartEntry.metadata.svgUseMaxWidth,
          QStringLiteral("XYChart live config did not reach the scene"));
  require(pngImage(xyChartSource) != pngImage(QStringLiteral(
              "xychart-beta\ntitle Sales\nx-axis [A,B,C]\n"
              "y-axis 0 --> 3\nbar [1,2,3]")),
          QStringLiteral("XYChart live config did not affect PNG output"));

  // Timeline consumes four family-specific fields. The remaining inherited
  // Journey/Sequence-shaped fields are accepted but renderer-inert upstream.
  const QString timelineSource = QStringLiteral(
      "%%{init: {\"timeline\": {\"useMaxWidth\": false, "
      "\"leftMargin\": 210, \"padding\": 17, "
      "\"disableMulticolor\": true}}}%%\n"
      "timeline\ntitle Releases\nAlpha : API ready\nBeta : Ship");
  const MermaidRenderEntry timelineEntry = render(timelineSource);
  const auto* timelineScene =
      dynamic_cast<const muffin::mermaid::timeline::TimelineScene*>(
          timelineEntry.scene.get());
  require(timelineEntry.status == MermaidRenderStatus::Ready &&
              timelineScene && !timelineEntry.metadata.svgUseMaxWidth &&
              timelineScene->config.leftMargin == 210.0 &&
              timelineScene->config.padding == 17.0 &&
              timelineScene->config.disableMulticolor,
          QStringLiteral("Timeline live config did not reach the scene"));
  require(pngImage(timelineSource) != pngImage(QStringLiteral(
              "timeline\ntitle Releases\nAlpha : API ready\nBeta : Ship")),
          QStringLiteral("Timeline live config did not affect PNG output"));
  const MermaidRenderEntry reduxTimeline = render(QStringLiteral(
      "%%{init: {\"theme\": \"redux-color\", \"themeVariables\": {"
      "\"strokeWidth\": \"7px\"}}}%%\n"
      "timeline\nOne : first event"));
  const auto* reduxTimelineScene =
      dynamic_cast<const muffin::mermaid::timeline::TimelineScene*>(
          reduxTimeline.scene.get());
  require(reduxTimeline.status == MermaidRenderStatus::Ready &&
              reduxTimelineScene && reduxTimelineScene->style.strokeWidth == 7.0 &&
              reduxTimelineScene->style.nodeFontWeight == QFont::DemiBold,
          QStringLiteral("Timeline Redux stroke width/font weight did not reach the scene"));
  const MermaidRenderEntry timelineFont = render(QStringLiteral(
      "%%{init: {\"fontFamily\": \"Noto Sans\", \"themeVariables\": {"
      "\"fontFamily\": \"Courier New\"}}}%%\n"
      "timeline\nOne"));
  const auto* timelineFontScene =
      dynamic_cast<const muffin::mermaid::timeline::TimelineScene*>(
          timelineFont.scene.get());
  require(timelineFont.status == MermaidRenderStatus::Ready &&
              timelineFontScene &&
              timelineFontScene->style.fontFamily == QLatin1String("Courier New"),
          QStringLiteral("Timeline nested theme fontFamily precedence drifted"));

  // Packet keeps config scalars raw because the renderer performs JavaScript
  // arithmetic on them. This production-path case pins all seven live fields;
  // useWidth remains classified as upstream-inert in the matrix above.
  const QString packetSource = QStringLiteral(
      "%%{init: {\"packet\": {\"useMaxWidth\": false, "
      "\"rowHeight\": 50, \"bitWidth\": 10, \"bitsPerRow\": 8, "
      "\"showBits\": false, \"paddingX\": 2, \"paddingY\": 3}}}%%\n"
      "packet-beta\ntitle Header\n0-3: \"Type\"\n4-7: \"Code\"");
  const MermaidRenderEntry packetEntry = render(packetSource);
  const auto* packetScene =
      dynamic_cast<const muffin::mermaid::packet::PacketScene*>(
          packetEntry.scene.get());
  require(packetEntry.status == MermaidRenderStatus::Ready && packetScene &&
              !packetEntry.metadata.svgUseMaxWidth &&
              !packetScene->showBits && packetScene->svgWidth == 82.0 &&
              packetScene->config.rowHeight.toDouble() == 50.0 &&
              packetScene->config.bitWidth.toDouble() == 10.0 &&
              packetScene->config.bitsPerRow.toDouble() == 8.0 &&
              packetScene->config.paddingX.toDouble() == 2.0 &&
              packetScene->config.paddingY.toDouble() == 3.0,
          QStringLiteral("Packet live config did not reach the scene"));
  require(pngImage(packetSource) != pngImage(QStringLiteral(
              "packet-beta\ntitle Header\n0-3: \"Type\"\n4-7: \"Code\"")),
          QStringLiteral("Packet live config did not affect PNG output"));

  // Kanban's own sectionWidth and ticketBaseUrl are live. Its padding and
  // useMaxWidth declarations are inert because the 11.16.0 renderer reads the
  // same-named MINDMAP values instead; lock both sides of that cross-family
  // contract through the production adapter.
  const QString kanbanSource = QStringLiteral(
      "%%{init: {\"kanban\": {\"sectionWidth\": 320, "
      "\"ticketBaseUrl\": \"https://example.test/items/#TICKET#\", "
      "\"padding\": 99, \"useMaxWidth\": false}, "
      "\"mindmap\": {\"padding\": 40, \"useMaxWidth\": false}}}%%\n"
      "kanban\n  todo[Todo]\n"
      "    task1[Write docs]@{ ticket: KAN-7 }");
  const MermaidRenderEntry kanbanEntry = render(kanbanSource);
  const auto* kanbanScene =
      dynamic_cast<const muffin::mermaid::kanban::KanbanScene*>(
          kanbanEntry.scene.get());
  require(kanbanEntry.status == MermaidRenderStatus::Ready && kanbanScene &&
              !kanbanEntry.metadata.svgUseMaxWidth &&
              kanbanScene->config.sectionWidth.toDouble() == 320.0 &&
              kanbanScene->config.padding.toDouble() == 40.0 &&
              !kanbanScene->useMaxWidth &&
              kanbanScene->interactions.size() == 1 &&
              kanbanScene->interactions.first().href ==
                  QLatin1String("https://example.test/items/KAN-7"),
          QStringLiteral("Kanban live/cross-family config did not reach the scene"));
  const MermaidRenderEntry inertKanban = render(QStringLiteral(
      "%%{init: {\"kanban\": {\"padding\": 99, "
      "\"useMaxWidth\": false}}}%%\n"
      "kanban\n  todo[Todo]\n    task1[Write docs]"));
  const auto* inertKanbanScene =
      dynamic_cast<const muffin::mermaid::kanban::KanbanScene*>(
          inertKanban.scene.get());
  require(inertKanban.status == MermaidRenderStatus::Ready &&
              inertKanbanScene &&
              inertKanbanScene->config.padding.toDouble() == 10.0 &&
              inertKanbanScene->useMaxWidth,
          QStringLiteral("Inert Kanban padding/useMaxWidth leaked into rendering"));
  require(pngImage(kanbanSource) != pngImage(QStringLiteral(
              "kanban\n  todo[Todo]\n"
              "    task1[Write docs]@{ ticket: KAN-7 }")),
          QStringLiteral("Kanban live config did not affect PNG output"));

  // Mindmap consumes its three live family fields without losing their raw
  // JSON type. Renderer selection comes from the top-level layout key;
  // mindmap.layoutAlgorithm remains a declared-but-inert interface field.
  const QString mindmapSource = QStringLiteral(
      "%%{init: {\"mindmap\": {\"padding\": 20, "
      "\"maxNodeWidth\": 80, \"useMaxWidth\": false}, "
      "\"htmlLabels\": false, \"markdownAutoWrap\": false, "
      "\"layout\": \"dagre\"}}%%\n"
      "mindmap\n  root((Root))\n"
      "    A[alpha beta gamma delta epsilon]\n    B[Beta]");
  const MermaidRenderEntry mindmapEntry = render(mindmapSource);
  const auto* mindmapScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          mindmapEntry.scene.get());
  require(mindmapEntry.status == MermaidRenderStatus::Ready && mindmapScene &&
              !mindmapEntry.metadata.svgUseMaxWidth &&
              mindmapScene->config.padding.toDouble() == 20.0 &&
              mindmapScene->config.maxNodeWidth.toDouble() == 80.0 &&
              !mindmapScene->config.htmlLabels &&
              !mindmapScene->config.markdownAutoWrap &&
              mindmapScene->effectiveLayout == QLatin1String("dagre"),
          QStringLiteral("Mindmap live config did not reach the scene"));
  const MermaidRenderEntry inertMindmap = render(QStringLiteral(
      "%%{init: {\"mindmap\": {\"layoutAlgorithm\": \"dagre\"}}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* inertMindmapScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          inertMindmap.scene.get());
  const MermaidRenderEntry fallbackMindmap = render(QStringLiteral(
      "%%{init: {\"layout\": \"missing\"}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* fallbackMindmapScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          fallbackMindmap.scene.get());
  const MermaidRenderEntry uppercaseLayoutMindmap = render(QStringLiteral(
      "%%{init: {\"layout\": \"DAGRE\"}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* uppercaseLayoutMindmapScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          uppercaseLayoutMindmap.scene.get());
  require(inertMindmap.status == MermaidRenderStatus::Ready &&
              inertMindmapScene &&
              inertMindmapScene->effectiveLayout ==
                  QLatin1String("cose-bilkent") &&
              fallbackMindmap.status == MermaidRenderStatus::Ready &&
              fallbackMindmapScene &&
              fallbackMindmapScene->effectiveLayout ==
                  QLatin1String("cose-bilkent") &&
              uppercaseLayoutMindmap.status == MermaidRenderStatus::Ready &&
              uppercaseLayoutMindmapScene &&
              uppercaseLayoutMindmapScene->effectiveLayout ==
                  QLatin1String("cose-bilkent"),
          QStringLiteral("Mindmap layoutAlgorithm/fallback contract drifted"));
  require(pngImage(mindmapSource) != pngImage(QStringLiteral(
              "mindmap\n  root((Root))\n"
              "    A[alpha beta gamma delta epsilon]\n    B[Beta]")),
          QStringLiteral("Mindmap live config did not affect PNG output"));

  const QString blockBody = QStringLiteral(
      "block-beta\ncolumns 2\na[\"Alpha\"] b(\"Beta\")\na --> b");
  const QString configuredBlockSource = QStringLiteral(
      "%%{init: {\"block\": {\"useWidth\": 999, "
      "\"useMaxWidth\": false, \"padding\": 20}}}%%\n") + blockBody;
  const MermaidRenderEntry baselineBlock = render(blockBody);
  const MermaidRenderEntry configuredBlock = render(configuredBlockSource);
  const MermaidRenderEntry inertBlock = render(QStringLiteral(
      "%%{init: {\"block\": {\"useWidth\": 999}}}%%\n") + blockBody);
  const auto* baselineBlockScene =
      dynamic_cast<const muffin::mermaid::block::BlockScene*>(
          baselineBlock.scene.get());
  const auto* configuredBlockScene =
      dynamic_cast<const muffin::mermaid::block::BlockScene*>(
          configuredBlock.scene.get());
  const auto* inertBlockScene =
      dynamic_cast<const muffin::mermaid::block::BlockScene*>(
          inertBlock.scene.get());
  require(baselineBlock.status == MermaidRenderStatus::Ready &&
              configuredBlock.status == MermaidRenderStatus::Ready &&
              inertBlock.status == MermaidRenderStatus::Ready &&
              baselineBlockScene && configuredBlockScene && inertBlockScene &&
              !configuredBlock.metadata.svgUseMaxWidth &&
              configuredBlockScene->nodes.first().paintSize.width() >
                  baselineBlockScene->nodes.first().paintSize.width() &&
              inertBlockScene->bounds == baselineBlockScene->bounds,
          QStringLiteral("Block live/inert config did not reach the scene"));
  require(pngImage(configuredBlockSource) != pngImage(blockBody),
          QStringLiteral("Block live config did not affect PNG output"));

  const QString gitGraphBody = QStringLiteral(
      "gitGraph LR:\ncommit id: \"root\"\nbranch alpha\n"
      "commit id: \"alpha-1\"\ncheckout main\nbranch beta\n"
      "commit id: \"beta-1\"");
  const QString configuredGitGraphSource = QStringLiteral(
      "%%{init: {\"gitGraph\": {\"useMaxWidth\": false, "
      "\"diagramPadding\": 20, \"parallelCommits\": true}}}%%\n") +
      gitGraphBody;
  const MermaidRenderEntry baselineGitGraph = render(gitGraphBody);
  const MermaidRenderEntry configuredGitGraph =
      render(configuredGitGraphSource);
  const auto* baselineGitGraphScene =
      dynamic_cast<const muffin::mermaid::gitgraph::GitGraphScene*>(
          baselineGitGraph.scene.get());
  const auto* configuredGitGraphScene =
      dynamic_cast<const muffin::mermaid::gitgraph::GitGraphScene*>(
          configuredGitGraph.scene.get());
  require(baselineGitGraph.status == MermaidRenderStatus::Ready &&
              configuredGitGraph.status == MermaidRenderStatus::Ready &&
              baselineGitGraphScene && configuredGitGraphScene &&
              !configuredGitGraph.metadata.svgUseMaxWidth &&
              configuredGitGraphScene->config.parallelCommits &&
              configuredGitGraphScene->config.diagramPadding == 20.0 &&
              configuredGitGraphScene->bounds != baselineGitGraphScene->bounds,
          QStringLiteral("GitGraph live config did not reach the scene"));
  require(pngImage(configuredGitGraphSource) != pngImage(gitGraphBody),
          QStringLiteral("GitGraph live config did not affect PNG output"));

  // TreeView preserves JavaScript scalar coercion and uses all icon mapping
  // fields while reproducing the upstream bug that strips the final <use>
  // elements. The 18px icon reservation therefore remains observable in the
  // layout even though no icon glyph is painted.
  const QString treeViewSource = QStringLiteral(
      "%%{init: {\"treeView\": {\"useWidth\": 999, "
      "\"useMaxWidth\": false, \"rowIndent\": 22, \"paddingX\": 8, "
      "\"paddingY\": 7, \"lineThickness\": 3, \"showIcons\": true, "
      "\"defaultIconPack\": \"mdi\", "
      "\"filenameIcons\": {\"README.md\": \"none\"}, "
      "\"extensionIcons\": {\".js\": \"code\"}}}}%%\n"
      "treeView-beta\nproject/\n  README.md\n  app.js");
  const MermaidRenderEntry treeViewEntry = render(treeViewSource);
  const auto* treeViewScene =
      dynamic_cast<const muffin::mermaid::treeview::TreeViewScene*>(
          treeViewEntry.scene.get());
  require(treeViewEntry.status == MermaidRenderStatus::Ready && treeViewScene &&
              !treeViewEntry.metadata.svgUseMaxWidth &&
              treeViewScene->config.rowIndent.toDouble() == 22.0 &&
              treeViewScene->config.paddingX.toDouble() == 8.0 &&
              treeViewScene->config.paddingY.toDouble() == 7.0 &&
              treeViewScene->config.lineThickness.toDouble() == 3.0 &&
              treeViewScene->nodes.size() == 4 &&
              treeViewScene->nodes.at(0).iconReserved &&
              treeViewScene->nodes.at(1).iconReserved &&
              !treeViewScene->nodes.at(2).iconReserved &&
              treeViewScene->nodes.at(3).iconReserved &&
              treeViewScene->nodes.at(3).iconName == QLatin1String("mdi:code"),
          QStringLiteral("TreeView live config/icon mapping did not reach the scene"));
  require(pngImage(treeViewSource) != pngImage(QStringLiteral(
              "treeView-beta\nproject/\n  README.md\n  app.js")),
          QStringLiteral("TreeView live config did not affect PNG output"));

  // Gantt is the only Mermaid family that consumes BaseDiagramConfig.useWidth.
  // Pin all 17 declared fields through the source-entry adapter and typed scene.
  const QString ganttSource = QStringLiteral(
      "%%{init: {\"gantt\": {\"useWidth\": 640, "
      "\"useMaxWidth\": false, \"titleTopMargin\": 17, "
      "\"barHeight\": 28, \"barGap\": 9, \"topPadding\": 61, "
      "\"rightPadding\": 81, \"leftPadding\": 91, "
      "\"gridLineStartPadding\": 23, \"fontSize\": 13, "
      "\"sectionFontSize\": 15, \"numberSectionStyles\": 3, "
      "\"axisFormat\": \"%m/%d\", \"tickInterval\": \"2day\", "
      "\"topAxis\": true, \"displayMode\": \"compact\", "
      "\"weekday\": \"monday\"}}}%%\n"
      "gantt\ntitle Plan\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
      "section Delivery\nBuild :build, 2024-01-01, 3d\n"
      "Ship :ship, after build, 2d");
  const MermaidRenderEntry ganttEntry = render(ganttSource);
  const auto* ganttScene =
      dynamic_cast<const muffin::mermaid::gantt::GanttScene*>(
          ganttEntry.scene.get());
  require(ganttEntry.status == MermaidRenderStatus::Ready && ganttScene &&
              !ganttEntry.metadata.svgUseMaxWidth &&
              ganttScene->config.useWidth == 640.0 &&
              ganttScene->config.titleTopMargin == 17.0 &&
              ganttScene->config.barHeight == 28.0 &&
              ganttScene->config.barGap == 9.0 &&
              ganttScene->config.topPadding == 61.0 &&
              ganttScene->config.rightPadding == 81.0 &&
              ganttScene->config.leftPadding == 91.0 &&
              ganttScene->config.gridLineStartPadding == 23.0 &&
              ganttScene->config.fontSize == 13.0 &&
              ganttScene->config.sectionFontSize == 15.0 &&
              ganttScene->config.numberSectionStyles == 3 &&
              ganttScene->config.axisFormat == QLatin1String("%m/%d") &&
              ganttScene->config.tickInterval == QLatin1String("2day") &&
              ganttScene->config.topAxis &&
              ganttScene->config.displayMode == QLatin1String("compact") &&
              ganttScene->config.weekday == QLatin1String("monday"),
          QStringLiteral("Gantt live config did not reach the scene"));
  require(pngImage(ganttSource) != pngImage(QStringLiteral(
              "gantt\ntitle Plan\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
              "section Delivery\nBuild :build, 2024-01-01, 3d\n"
              "Ship :ship, after build, 2d")),
          QStringLiteral("Gantt live config did not affect PNG output"));

  // CSS ex/ch resolution must measure the same full font fallback list that
  // the scene painter uses. A missing first family must therefore fall through
  // to Noto Sans instead of being measured with an unrelated system fallback.
  const auto fallbackFontEntry = render(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fontFamily\": "
      "\"DefinitelyMissing, Noto Sans\", \"fontSize\": \"2ex\"}}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto notoFontEntry = render(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fontFamily\": \"Noto Sans\", "
      "\"fontSize\": \"2ex\"}}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* fallbackFontScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          fallbackFontEntry.scene.get());
  const auto* notoFontScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          notoFontEntry.scene.get());
  require(fallbackFontEntry.status == MermaidRenderStatus::Ready &&
              notoFontEntry.status == MermaidRenderStatus::Ready &&
              fallbackFontScene && notoFontScene &&
              fallbackFontScene->style.fontSize > 0.0 &&
              std::abs(fallbackFontScene->style.fontSize -
                       notoFontScene->style.fontSize) < 0.001,
          QStringLiteral("Mindmap font fallback/ex metric contract drifted"));

  // Theme names and renderer names are case-sensitive at Mermaid's source
  // entry point. Uppercase variants are not aliases; they fall back to the
  // default theme / cose-bilkent layout.
  const auto reduxThemeEntry = render(QStringLiteral(
      "%%{init: {\"theme\": \"redux\", \"look\": \"neo\"}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto uppercaseThemeEntry = render(QStringLiteral(
      "%%{init: {\"theme\": \"Redux\", \"look\": \"neo\"}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto defaultThemeEntry = render(QStringLiteral(
      "%%{init: {\"theme\": \"default\", \"look\": \"neo\"}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* reduxThemeScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          reduxThemeEntry.scene.get());
  const auto* uppercaseThemeScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          uppercaseThemeEntry.scene.get());
  const auto* defaultThemeScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          defaultThemeEntry.scene.get());
  require(reduxThemeScene && uppercaseThemeScene && defaultThemeScene &&
              reduxThemeScene->style.themeName == QLatin1String("redux") &&
              uppercaseThemeScene->style.themeName == QLatin1String("default") &&
              uppercaseThemeScene->style.rootFill ==
                  defaultThemeScene->style.rootFill &&
              uppercaseThemeScene->style.mainBkg ==
                  defaultThemeScene->style.mainBkg,
          QStringLiteral("Mindmap source theme case-sensitivity drifted"));

  const auto styledMindmapEntry = render(QStringLiteral(
      "%%{init: {\"theme\": \"neo\", \"look\": \"neo\", "
      "\"themeVariables\": {\"mainBkg\": \"#123456\", "
      "\"strokeWidth\": 7}}}%%\n"
      "mindmap\n  root((Root))\n    Child"));
  const auto* styledMindmapScene =
      dynamic_cast<const muffin::mermaid::mindmap::MindmapScene*>(
          styledMindmapEntry.scene.get());
  require(styledMindmapScene &&
              styledMindmapScene->style.mainBkg == QLatin1String("#123456") &&
              styledMindmapScene->style.strokeWidth == 7.0 &&
              !styledMindmapScene->nodes.isEmpty() &&
              std::all_of(styledMindmapScene->nodes.cbegin(),
                          styledMindmapScene->nodes.cend(), [](const auto& node) {
                            return node.fill == QLatin1String("#123456") &&
                                   node.strokeWidth == 7.0;
                          }),
          QStringLiteral("Mindmap mainBkg/strokeWidth adapter wiring drifted"));

  const auto shadowScene = [&](const QString& value, bool includeValue) {
    const QString vars = includeValue
        ? QStringLiteral(", \"themeVariables\": {\"dropShadow\": \"%1\"}")
              .arg(value)
        : QString();
    const MermaidRenderEntry entry = render(
        QStringLiteral("%%{init: {\"theme\": \"neo\", \"look\": \"neo\"%1}}%%\n"
                       "mindmap\n  root((Root))\n    Child")
            .arg(vars));
    return std::dynamic_pointer_cast<const muffin::mermaid::mindmap::MindmapScene>(
        entry.scene);
  };
  const auto shadowAbsent = shadowScene(QString(), false);
  const auto shadowNone = shadowScene(QStringLiteral("none"), true);
  const auto shadowCustom =
      shadowScene(QStringLiteral("drop-shadow(9px 8px 0 #ff0000)"), true);
  const auto shadowBogus = shadowScene(QStringLiteral("bogus"), true);
  const auto noNodeShadow = [](const auto& scene) {
    return scene && std::none_of(scene->nodes.cbegin(), scene->nodes.cend(),
                                 [](const auto& node) { return node.dropShadow; });
  };
  require(shadowAbsent && !shadowAbsent->nodes.isEmpty() &&
              std::all_of(shadowAbsent->nodes.cbegin(), shadowAbsent->nodes.cend(),
                          [](const auto& node) { return node.dropShadow; }) &&
              noNodeShadow(shadowNone) && noNodeShadow(shadowCustom) &&
              noNodeShadow(shadowBogus),
          QStringLiteral("Mindmap dropShadow source used-value contract drifted"));

  // The bundled Mermaid 11.16 runtime deliberately resolves ELK to Dagre when
  // the optional external loader is absent.
  for (const QString& source : {
           QStringLiteral(
               "%%{init: {\"layout\": \"elk\"}}%%\nflowchart TB\nA --> B"),
           QStringLiteral(
               "%%{init: {\"flowchart\": {\"defaultRenderer\": \"elk\"}}}%%\n"
               "flowchart TB\nA --> B")}) {
    const MermaidRenderEntry fallback = render(source);
    require(fallback.status == MermaidRenderStatus::Ready && fallback.scene,
            QStringLiteral("Flowchart ELK fallback stopped rendering"));
  }

  // Other unregistered family renderer selections remain explicit errors.
  for (const QString& source : {
           QStringLiteral(
               "%%{init: {\"class\": {\"defaultRenderer\": \"elk\"}}}%%\n"
               "classDiagram\nclass A")}) {
    const MermaidRenderEntry unsupportedEntry = render(source);
    require(unsupportedEntry.status == MermaidRenderStatus::Unsupported &&
                unsupportedEntry.diagnostic.stage ==
                    QLatin1String("configuration") &&
                unsupportedEntry.diagnostic.code ==
                    QLatin1String("unsupported-layout-engine") &&
                !unsupportedEntry.diagnostic.actual.isEmpty() &&
                !unsupportedEntry.diagnostic.expected.isEmpty(),
            QStringLiteral("Unsupported layout silently fell back to Dagre"));
  }

  qDebug() << "MermaidConfigEffectMatrixTest:" << entries.size()
           << "classified rows and all seven effect dimensions passed";
  return 0;
}
