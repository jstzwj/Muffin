#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/journey/JourneyScene.h"

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
  require(entries.size() == 178,
          QStringLiteral("Expected 178 classified config rows, found %1")
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
                                             {QStringLiteral("journey"), 26},
                                             {QStringLiteral("pie"), 6},
                                             {QStringLiteral("quadrantChart"), 20},
                                             {QStringLiteral("requirement"), 11},
                                             {QStringLiteral("sequence"), 37},
                                             {QStringLiteral("state"), 22}},
          QStringLiteral("Family interface coverage drifted"));
  require(observedNativeDimensions == expectedDimensions,
          QStringLiteral("At least one native effect dimension has no evidence"));
  const QSet<QString> scopedFamilies = stringSet(
      fixture.value(QStringLiteral("scope")).toObject()
          .value(QStringLiteral("families")).toArray());
  require(stringSet(byPath.value(QStringLiteral("maxTextSize"))
                        .value(QStringLiteral("families")).toArray()) ==
              scopedFamilies,
          QStringLiteral("Global maxTextSize scope must cover every native family"));
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
                  QLatin1String("parity"),
          QStringLiteral("Critical config status rows drifted"));
  for (const QString& path : {
           QStringLiteral("deterministicIds"),
           QStringLiteral("deterministicIDSeed"),
           QStringLiteral("flowchart.useMaxWidth"),
           QStringLiteral("journey.useMaxWidth"),
           QStringLiteral("quadrantChart.useMaxWidth"),
           QStringLiteral("sequence.useMaxWidth"),
           QStringLiteral("state.useMaxWidth")}) {
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
