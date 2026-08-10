#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/kanban/KanbanScene.h"
#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/timeline/TimelineScene.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/treeview/TreeViewScene.h"
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
  require(entries.size() == 272,
          QStringLiteral("Expected 272 classified config rows, found %1")
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
                                             {QStringLiteral("er"), 13},
                                             {QStringLiteral("flowchart"), 14},
                                             {QStringLiteral("gantt"), 17},
                                             {QStringLiteral("journey"), 26},
                                             {QStringLiteral("kanban"), 5},
                                             {QStringLiteral("mindmap"), 5},
                                             {QStringLiteral("pie"), 6},
                                             {QStringLiteral("packet"), 8},
                                             {QStringLiteral("quadrantChart"), 20},
                                             {QStringLiteral("radar"), 11},
                                             {QStringLiteral("requirement"), 11},
                                             {QStringLiteral("sequence"), 37},
                                             {QStringLiteral("state"), 22},
                                             {QStringLiteral("timeline"), 24},
                                             {QStringLiteral("treeView"), 10},
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
    require(stringSet(byPath.value(path)
                          .value(QStringLiteral("families")).toArray()) ==
                scopedFamilies,
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
                  .contains(QStringLiteral("interaction")),
          QStringLiteral("Flowchart curve matrix row lost full native parity"));
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
           QStringLiteral("treeView.useMaxWidth")}) {
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

  // Unsupported engines must not silently produce a Dagre scene.
  for (const QString& source : {
           QStringLiteral(
               "%%{init: {\"layout\": \"elk\"}}%%\nflowchart TB\nA --> B"),
           QStringLiteral(
               "%%{init: {\"flowchart\": {\"defaultRenderer\": \"elk\"}}}%%\n"
               "flowchart TB\nA --> B"),
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
