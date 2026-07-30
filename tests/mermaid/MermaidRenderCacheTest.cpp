// Milestone I-1 unit test for MermaidRenderCache: sync render (ready/error/
// unsupported), async request (loading → renderReady → ready), LRU eviction,
// key stability.

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScenePainter.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

using namespace muffin::mermaid::editor;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

constexpr auto kReady = MermaidRenderStatus::Ready;
constexpr auto kLoading = MermaidRenderStatus::Loading;
constexpr auto kError = MermaidRenderStatus::Error;
constexpr auto kUnsupported = MermaidRenderStatus::Unsupported;

// Spin the event loop until `cache` signals renderReady for `key`, or timeout.
bool waitForReady(MermaidRenderCache& cache, const MermaidRenderKey& key, int timeoutMs = 5000) {
  QEventLoop loop;
  bool fired = false;
  QObject::connect(&cache, &MermaidRenderCache::renderReady, &loop, [&](const MermaidRenderKey& emitted) {
    if (emitted == key) { fired = true; loop.quit(); }
  });
  QTimer::singleShot(timeoutMs, &loop, [&]() { loop.quit(); });
  loop.exec();
  return fired;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  const QString flow = QStringLiteral("flowchart TB\nA[Alpha] --> B[Beta] --> C[Gamma]");
  const QString malformed = QStringLiteral("flowchart TB\nA --> B\nlinkStyle 9 stroke:red");
  const QString sequence = QStringLiteral("sequenceDiagram\nAlice->>Bob: Hi");
  const QString classDiagram = QStringLiteral(
      "classDiagram\nclass Service {\n  <<interface>>\n  +run() Result\n}\n"
      "class Client\nClient --> Service : uses");
  const QString stateDiagram = QStringLiteral(
      "stateDiagram-v2\n[*] --> Idle\nIdle --> Active : start\nActive --> [*]");
  const QString erDiagram = QStringLiteral(
      "erDiagram\n"
      "CUSTOMER ||--o{ ORDER : places\n"
      "ORDER ||--|{ LINE-ITEM : contains\n"
      "CUSTOMER {\n  string name PK\n  int age\n}\n"
      "ORDER {\n  bigint id PK\n  string status\n}");
  const QString unsupported = QStringLiteral(
      "pie title Pets\n  \"Dogs\" : 42\n  \"Cats\" : 58");

  // --- getSync: valid flowchart → Ready + scene + natural size ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(flow);
    const MermaidRenderEntry e = cache.getSync(key, flow);
    require(e.status == kReady, QStringLiteral("valid flowchart should be Ready (got %1)").arg((int)e.status));
    require(e.scene != nullptr, QStringLiteral("Ready entry must carry a scene"));
    require(e.naturalSize.width() > 0 && e.naturalSize.height() > 0, QStringLiteral("natural size must be positive"));
    require(e.metadata.title.isEmpty() && e.metadata.titleHeight == 0.0 &&
                e.metadata.diagramPadding == 8.0 &&
                e.naturalSize.width() ==
                    qCeil(e.metadata.contentSize.width() + 16.0) &&
                e.naturalSize.height() ==
                    qCeil(e.metadata.contentSize.height() + 16.0),
            QStringLiteral("untitled flowcharts must retain configured viewport padding"));
    // Second getSync hits the cache (same entry, no re-render).
    const MermaidRenderEntry e2 = cache.getSync(key, flow);
    require(e2.status == kReady && e2.scene == e.scene, QStringLiteral("cache hit returns the same scene"));
  }

  // --- getSync: valid erDiagram → Ready + er scene + positive size ---
  // Exercises the full er pipeline (parse → layout → scene → painter entry
  // points) end to end, mirroring the flowchart smoke above.
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(erDiagram);
    const MermaidRenderEntry e = cache.getSync(key, erDiagram);
    require(e.status == kReady, QStringLiteral("valid erDiagram should be Ready (got %1)").arg((int)e.status));
    const auto* erScene = dynamic_cast<const muffin::mermaid::er::ErScene*>(e.scene.get());
    require(erScene != nullptr, QStringLiteral("Ready er entry must carry an erScene"));
    require(e.naturalSize.width() > 0 && e.naturalSize.height() > 0, QStringLiteral("er natural size must be positive"));
  }

  // --- look: handDrawn renders sequence/class/state through the rough painter
  // without crashing, and the scene's handDrawn flag + seed propagate from the
  // frontmatter config. Default-look tests never reach these branches, so this
  // block is the guard that the handDrawn wiring (Task 5) stays functional.
  {
    const QString handDrawnSources[] = {
        QStringLiteral("---\nconfig:\n  look: handDrawn\n  handDrawnSeed: 7\n---\n"
                       "sequenceDiagram\nAlice->>Bob: Hi\nBob-->>Alice: Yo\n"
                       "Note over Alice,Bob: shared\n"),
        QStringLiteral("---\nconfig:\n  look: handDrawn\n  handDrawnSeed: 7\n---\n"
                       "classDiagram\nclass Foo\nclass Bar\nFoo --> Bar : uses\n"),
        QStringLiteral("---\nconfig:\n  look: handDrawn\n  handDrawnSeed: 7\n---\n"
                       "stateDiagram-v2\n[*] --> Idle\nIdle --> Active : go\n"
                       "Active --> [*]\n"),
    };
    for (const QString& src : handDrawnSources) {
      const QString url = MermaidRenderCache::renderMermaidSourceToPngDataUrl(src, 1.0);
      require(url.startsWith(QStringLiteral("data:image/png")),
              QStringLiteral("handDrawn look must render a PNG for: %1").arg(src));
    }
    MermaidRenderCache cache;
    const MermaidRenderEntry seq = cache.getSync(
        MermaidRenderCache::makeKey(handDrawnSources[0]), handDrawnSources[0]);
    require(seq.status == kReady, QStringLiteral("handDrawn sequence should be Ready"));
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(seq.scene.get());
    require(sequenceScene && sequenceScene->handDrawn,
            QStringLiteral("sequence scene must reflect look: handDrawn"));
    require(sequenceScene->handDrawnSeed == 7u,
            QStringLiteral("handDrawnSeed must propagate to the sequence scene"));
  }

  // --- shared title/accessibility metadata reaches all four native families ---
  {
    struct MetadataCase {
      QString family;
      QString source;
      QString title;
      QString accessibleTitle;
      QString accessibleDescription;
    };
    const QVector<MetadataCase> cases = {
        {QStringLiteral("flowchart"),
         QStringLiteral(
             "---\ntitle: A deliberately long flowchart overview title\n---\n"
             "flowchart TB\naccTitle: Flow accessible\n"
             "accDescr: Flow description\nA --> B"),
         QStringLiteral("A deliberately long flowchart overview title"),
         QStringLiteral("Flow accessible"),
         QStringLiteral("Flow description")},
        {QStringLiteral("sequence"),
         QStringLiteral(
             "---\ntitle: Sequence overview\n---\nsequenceDiagram\n"
             "accTitle: Sequence accessible\n"
             "accDescr: Sequence description\nA->>B: Hi"),
         QStringLiteral("Sequence overview"),
         QStringLiteral("Sequence accessible"),
         QStringLiteral("Sequence description")},
        {QStringLiteral("class"),
         QStringLiteral(
             "---\ntitle: Class overview\n---\nclassDiagram\n"
             "accTitle: Class accessible\naccDescr: Class description\n"
             "class Service"),
         QStringLiteral("Class overview"),
         QStringLiteral("Class accessible"),
         QStringLiteral("Class description")},
        {QStringLiteral("state"),
         QStringLiteral(
             "---\ntitle: State overview\n---\nstateDiagram-v2\n"
             "accTitle: State accessible\naccDescr: State description\n"
             "[*] --> Idle"),
         QStringLiteral("State overview"),
         QStringLiteral("State accessible"),
         QStringLiteral("State description")},
    };
    MermaidRenderCache cache;
    for (const MetadataCase& value : cases) {
      const MermaidRenderEntry entry = cache.getSync(
          MermaidRenderCache::makeKey(value.source), value.source);
      require(entry.status == kReady,
              value.family + QStringLiteral(" titled diagram must render"));
      require(entry.metadata.title == value.title &&
                  entry.metadata.accessibleTitle == value.accessibleTitle &&
                  entry.metadata.accessibleDescription ==
                      value.accessibleDescription &&
                  entry.metadata.accessibleName() == value.accessibleTitle &&
                  !entry.metadata.roleDescription.isEmpty(),
              value.family +
                  QStringLiteral(" title/accessibility metadata drifted"));
      require(entry.metadata.titleHeight >= 40.0 &&
                  entry.metadata.contentSize.width() > 0.0 &&
                  entry.metadata.contentSize.height() > 0.0 &&
                  entry.naturalSize.width() >=
                      qCeil(entry.metadata.contentSize.width()) &&
                  entry.naturalSize.height() >=
                      qCeil(entry.metadata.contentSize.height() +
                            entry.metadata.titleHeight),
              value.family + QStringLiteral(" title canvas was not reserved"));
      const MermaidPngRenderResult png =
          MermaidRenderCache::renderMermaidSourceToPng(value.source, 1.0);
      require(!png.dataUrl.isEmpty() &&
                  png.metadata.accessibleName() == value.accessibleTitle,
              value.family +
                  QStringLiteral(" PNG export lost accessibility metadata"));
    }
  }

  // Mermaid sets frontmatter metadata before parsing, so sequence's native
  // `title` statement wins when both are present.
  {
    const QString source = QStringLiteral(
        "---\ntitle: Frontmatter title\n---\nsequenceDiagram\n"
        "title Inline title\nA->>B: Hi");
    MermaidRenderCache cache;
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(source), source);
    require(entry.status == kReady &&
                entry.metadata.title == QLatin1String("Inline title"),
            QStringLiteral("sequence inline title must override frontmatter"));
  }

  // --- state diagrams use the immutable state scene pipeline ---
  {
    MermaidRenderCache cache;
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(stateDiagram), stateDiagram);
    const auto* stateScene = dynamic_cast<const muffin::mermaid::state::StateScene*>(entry.scene.get());
    require(entry.status == kReady && stateScene != nullptr,
            QStringLiteral("stateDiagram-v2 should be Ready"));
    require(stateScene->nodes.size() == 4 &&
                stateScene->edges.size() == 3 &&
                entry.naturalSize.width() > 0 && entry.naturalSize.height() > 0,
            QStringLiteral("state scene must contain state and transition geometry"));
    require(!MermaidRenderCache::renderMermaidSourceToPngDataUrl(
                 stateDiagram, 1.0).isEmpty(),
            QStringLiteral("state scene PNG export must be available"));
  }

  // --- class diagrams use the immutable class scene pipeline ---
  {
    MermaidRenderCache cache;
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(classDiagram), classDiagram);
    const auto* classScene = dynamic_cast<const muffin::mermaid::classdiagram::ClassScene*>(entry.scene.get());
    require(entry.status == kReady && classScene != nullptr,
            QStringLiteral("classDiagram should be Ready"));
    require(classScene->nodes.size() == 2 &&
                classScene->edges.size() == 1 &&
                classScene->markers.size() == 20 &&
                entry.naturalSize.width() > 0 && entry.naturalSize.height() > 0,
            QStringLiteral("classDiagram scene must contain class, relation, and marker geometry"));
  }

  // Mermaid 11.16.0 accepts class.nodeSpacing/rankSpacing in configuration but
  // does not forward them to the class renderer's Dagre graph.
  {
    MermaidRenderCache cache;
    const QString configured = QStringLiteral(
        "%%{init: {\"class\": {\"nodeSpacing\": 123, \"rankSpacing\": 234}}}%%\n") +
        classDiagram;
    const MermaidRenderEntry baseline = cache.getSync(
        MermaidRenderCache::makeKey(classDiagram), classDiagram);
    const MermaidRenderEntry inert = cache.getSync(
        MermaidRenderCache::makeKey(configured), configured);
    const auto* baselineScene = dynamic_cast<const muffin::mermaid::classdiagram::ClassScene*>(baseline.scene.get());
    const auto* inertScene = dynamic_cast<const muffin::mermaid::classdiagram::ClassScene*>(inert.scene.get());
    require(baseline.status == kReady && inert.status == kReady &&
                baselineScene && inertScene &&
                baseline.naturalSize == inert.naturalSize &&
                baselineScene->nodes.size() == inertScene->nodes.size(),
            QStringLiteral("class spacing config must remain upstream-inert"));
    for (qsizetype i = 0; i < baselineScene->nodes.size(); ++i)
      require(baselineScene->nodes.at(i).center == inertScene->nodes.at(i).center,
              QStringLiteral("class spacing config changed node placement"));
  }

  // --- getSync: malformed → Error ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(malformed);
    const MermaidRenderEntry e = cache.getSync(key, malformed);
    require(e.status == kError, QStringLiteral("malformed flowchart should be Error (got %1)").arg((int)e.status));
    require(!e.errorMessage.isEmpty(), QStringLiteral("Error entry must carry a message"));
    require(e.diagnostic.stage == QLatin1String("semantic") &&
                e.diagnostic.code == QLatin1String("link-style-bounds"),
            QStringLiteral("flowchart cache error must preserve stage/code"));
    require(e.diagnostic.span.offset == malformed.indexOf(QLatin1Char('9')) &&
                e.diagnostic.span.length == 1 &&
                e.diagnostic.span.line == 3 &&
                e.diagnostic.span.column == 11,
            QStringLiteral("flowchart cache error must preserve its 1-based source position"));
    require(e.errorMessage.contains(QStringLiteral("Line 3, column 11")) &&
                e.errorMessage.contains(QStringLiteral("Semantic error")) &&
                e.errorDiagnostic.value(QStringLiteral("offset")).toInt() ==
                    malformed.indexOf(QLatin1Char('9')),
            QStringLiteral("flowchart display/JSON diagnostics must expose the source position"));
  }

  // Parser offsets refer to preprocessed code. Map them back around removed
  // init directives and comments before exposing them to the editor.
  {
    MermaidRenderCache cache;
    const QString decorated = QStringLiteral(
        "%%{init: {\"theme\": \"default\"}}%%\n"
        "%% generated comment\n"
        "flowchart TB\n"
        "A --> B\n"
        "linkStyle 9 stroke:red");
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(decorated), decorated);
    require(entry.status == kError &&
                entry.diagnostic.span.offset == decorated.indexOf(
                    QStringLiteral("9 stroke")) &&
                entry.diagnostic.span.line == 5 &&
                entry.diagnostic.span.column == 11,
            QStringLiteral("diagnostic mapping must restore original directive/comment lines"));
  }

  // Every native family is normalised into the same 1-based diagnostic model.
  {
    struct InvalidCase {
      QString source;
      QString type;
      QString code;
      int line;
    };
    const QVector<InvalidCase> cases = {
        {QStringLiteral("sequenceDiagram\nloop open\nA->>B:x"),
         QStringLiteral("sequence"), QStringLiteral("missing-end"), 2},
        {QStringLiteral("classDiagram\nA -->"),
         QStringLiteral("class"), QStringLiteral("missing-relation-target"), 2},
        {QStringLiteral("stateDiagram-v2\nA -->"),
         QStringLiteral("state"), QStringLiteral("unexpected-token"), 2},
    };
    for (const InvalidCase& invalid : cases) {
      MermaidRenderCache cache;
      const MermaidRenderEntry entry = cache.getSync(
          MermaidRenderCache::makeKey(invalid.source), invalid.source);
      require(entry.status == kError &&
                  entry.diagnostic.diagramType.startsWith(invalid.type) &&
                  entry.diagnostic.code == invalid.code &&
                  entry.diagnostic.span.hasLocation() &&
                  entry.diagnostic.span.line == invalid.line &&
                  entry.diagnostic.span.column >= 1 &&
                  entry.diagnostic.span.offset >= 0 &&
                  entry.diagnostic.span.offset <= invalid.source.size(),
              QStringLiteral("%1 diagnostic was not normalised: %2")
                  .arg(invalid.type, entry.errorMessage));
      require(entry.errorMessage.contains(QStringLiteral("Line %1, column ").arg(invalid.line)),
              QStringLiteral("%1 diagnostic display omitted line/column").arg(invalid.type));
    }
  }

  // Detection and preprocessing failures also use the common diagnostic shape.
  {
    MermaidRenderCache cache;
    const QString noHeader = QStringLiteral("A --> B");
    const MermaidRenderEntry detected = cache.getSync(
        MermaidRenderCache::makeKey(noHeader), noHeader);
    require(detected.status == kError &&
                detected.diagnostic.stage == QLatin1String("detector") &&
                detected.diagnostic.span.offset == 0 &&
                detected.diagnostic.span.line == 1 &&
                detected.diagnostic.span.column == 1,
            QStringLiteral("missing diagram header must carry a source position"));

    const QString invalidYaml = QStringLiteral(
        "---\nconfig: [\n---\nflowchart TB\nA --> B");
    const MermaidRenderEntry preprocessed = cache.getSync(
        MermaidRenderCache::makeKey(invalidYaml), invalidYaml);
    require(preprocessed.status == kError &&
                preprocessed.diagnostic.stage == QLatin1String("preprocess") &&
                preprocessed.diagnostic.span.hasLocation() &&
                preprocessed.errorMessage.contains(QStringLiteral("Line ")) &&
                preprocessed.errorMessage.contains(QStringLiteral("Preprocessing error")),
            QStringLiteral("invalid Mermaid front matter must be classified as preprocessing"));
  }

  // --- getSync: sequence diagrams are supported ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(sequence);
    const MermaidRenderEntry e = cache.getSync(key, sequence);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(e.scene.get());
    require(e.status == kReady && sequenceScene != nullptr,
            QStringLiteral("sequenceDiagram should be Ready (got %1)").arg((int)e.status));
    require(sequenceScene->participants.size() == 2 && sequenceScene->messages.size() == 1 &&
                e.naturalSize.width() > 0 && e.naturalSize.height() > 0,
            QStringLiteral("sequenceDiagram scene must contain participant/message geometry"));
  }

  // --- getSync: an unknown native family reports Unsupported with context ---
  {
    MermaidRenderCache cache;
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(unsupported), unsupported);
    require(entry.status == kUnsupported && !entry.errorMessage.isEmpty(),
            QStringLiteral("unsupported family must carry an explanatory message"));
  }

  // --- sequence MathML is compiled into the immutable scene once ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "sequenceDiagram\nA->>B: $$\\frac{\\sqrt{x}}{y^2}$$");
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(source), source);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(entry.scene.get());
    require(entry.status == kReady && sequenceScene &&
                sequenceScene->messageLabels.size() == 1,
            QStringLiteral("sequence MathML cache case must render"));
    const auto& math = sequenceScene->messageLabels.first().richText.math;
    require(math.size() == 1 && math.front().prepared,
            QStringLiteral("sequence scene must own a prepared MathML operation"));
    const auto* prepared = math.front().prepared.get();
    const QImage first = muffin::mermaid::sequence::renderSequenceSceneToImage(
        *sequenceScene, 1.0, 0.0);
    const QImage second = muffin::mermaid::sequence::renderSequenceSceneToImage(
        *sequenceScene, 1.0, 0.0);
    require(!first.isNull() && first == second &&
                math.front().prepared.get() == prepared,
            QStringLiteral("repaint must reuse the immutable MathML operation"));
  }

  // --- prepared MathML crosses the worker/GUI thread boundary read-only ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "sequenceDiagram\nA->>B: $$\\hat{\\frac{x}{y}}$$");
    const MermaidRenderKey key = MermaidRenderCache::makeKey(source);
    require(cache.request(key, source).status == kLoading,
            QStringLiteral("async sequence MathML request must start loading"));
    require(waitForReady(cache, key),
            QStringLiteral("async sequence MathML renderReady must fire"));
    const MermaidRenderEntry entry = cache.request(key, source);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(entry.scene.get());
    require(entry.status == kReady && sequenceScene &&
                !sequenceScene->messageLabels.isEmpty() &&
                sequenceScene->messageLabels.first()
                    .richText.math.front().prepared,
            QStringLiteral("worker-built MathML scene must retain operations"));
    const QImage image = muffin::mermaid::sequence::renderSequenceSceneToImage(
        *sequenceScene, 1.0, 0.0);
    require(!image.isNull(),
            QStringLiteral("worker-built MathML operations must paint on GUI thread"));
  }

  // --- preparation is idempotent and strict scene assembly preserves identity ---
  {
    auto label = muffin::mermaid::sequence::parseSequenceLabel(
        QStringLiteral("$$\\frac{x}{y}$$"),
        muffin::mermaid::sequence::SequenceLabelKind::Message);
    require(muffin::mermaid::flowchart::prepareFlowLabelMath(
                label.richText, 16.0) == 1,
            QStringLiteral("first MathML preparation must build one operation"));
    const auto* first = label.richText.math.front().prepared.get();
    require(muffin::mermaid::flowchart::prepareFlowLabelMath(
                label.richText, 16.0) == 0 &&
                label.richText.math.front().prepared.get() == first,
            QStringLiteral("same-font preparation must reuse the operation"));
    require(muffin::mermaid::flowchart::prepareFlowLabelMath(
                label.richText, 20.0) == 1 &&
                label.richText.math.front().prepared.get() != first,
            QStringLiteral("font-size changes must rebuild the operation"));

    muffin::mermaid::sequence::SequenceLayoutResult layout;
    muffin::mermaid::sequence::SequenceLayoutMessage message;
    message.messageIndex = 7;
    message.label = label.richText.text;
    layout.messages.append(message);
    muffin::mermaid::sequence::SequencePreparedLabels labels;
    labels.messagesByIndex.insert(7, label);
    const auto* prepared = label.richText.math.front().prepared.get();
    const auto scene = muffin::mermaid::sequence::buildSequenceScene(
        layout, {}, labels, true);
    require(scene.messageLabels.size() == 1 &&
                scene.messageLabels.front().richText.math.front()
                    .prepared.get() == prepared,
            QStringLiteral("scene assembly must preserve prepared identity"));
    bool rejectedMissing = false;
    try {
      (void)muffin::mermaid::sequence::buildSequenceScene(
          layout, {}, {}, true);
    } catch (const std::logic_error&) {
      rejectedMissing = true;
    }
    require(rejectedMissing,
            QStringLiteral("strict scene assembly must reject missing labels"));
  }

  // --- sequence font style is resolved before measurement and preparation ---
  {
    MermaidRenderCache cache;
    const QString body = QStringLiteral(
        "sequenceDiagram\nA->>B: a deliberately long message label\n"
        "A->>B: $$\\hat{\\frac{x}{y}}$$");
    const QString large = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"fontSize\": \"20px\"}}}%%\n") +
        body;
    const auto normal = cache.getSync(
        MermaidRenderCache::makeKey(body), body);
    const auto enlarged = cache.getSync(
        MermaidRenderCache::makeKey(large), large);
    const auto* normalScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(normal.scene.get());
    const auto* enlargedScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(enlarged.scene.get());
    require(normal.status == kReady && normalScene &&
                enlarged.status == kReady && enlargedScene,
            QStringLiteral("sequence font-size comparison must render"));
    require(normalScene->style.fontSize == 16.0 &&
                enlargedScene->style.fontSize == 20.0 &&
                enlargedScene->messages.first().labelRect.width() >
                    normalScene->messages.first().labelRect.width() &&
                enlargedScene->messages.first().labelRect.height() >
                    normalScene->messages.first().labelRect.height(),
            QStringLiteral("sequence font size must affect measured geometry"));
    const auto& normalMath = normalScene->messageLabels.at(1)
                                 .richText.math.front().prepared;
    const auto& enlargedMath = enlargedScene->messageLabels.at(1)
                                   .richText.math.front().prepared;
    require(normalMath && enlargedMath && normalMath.get() != enlargedMath.get(),
            QStringLiteral("font-size changes must produce a new Math operation"));
  }

  // --- sequence config + box measurements reach the production scene ---
  {
    MermaidRenderCache cache;
    const QString configured = QStringLiteral(
        "%%{init: {\"sequence\": {\"mirrorActors\": false, "
        "\"hideUnusedParticipants\": true, \"width\": 180, \"boxMargin\": 14, "
        "\"diagramMarginX\": 31, \"diagramMarginY\": 23, "
        "\"bottomMarginAdj\": 7}}}%%\n"
        "sequenceDiagram\n"
        "participant UNUSED as Hidden\n"
        "box rgb(238, 246, 255) Services\n"
        "participant A as API\n"
        "participant B as Worker\n"
        "end\n"
        "A->>B:call");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(configured), configured);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(e.scene.get());
    require(e.status == kReady && sequenceScene != nullptr,
            QStringLiteral("configured sequence must render"));
    require(sequenceScene->participants.size() == 2 && sequenceScene->boxes.size() == 1,
            QStringLiteral("sequence hideUnusedParticipants/box config must reach the scene"));
    require(sequenceScene->participants.first().logicalRect.width() == 180.0 &&
                !sequenceScene->participants.first().drawBottom &&
                sequenceScene->participants.first().lifelineStopY == 2000.0,
            QStringLiteral("sequence width/mirrorActors config must reach layout"));
    require(sequenceScene->boxes.first().label == QLatin1String("Services") &&
                sequenceScene->boxes.first().labelRect.height() > 0.0,
            QStringLiteral("sequence box title must be measured and retained"));
    const QRectF viewport = muffin::mermaid::sequence::sequenceViewportRect(
        *sequenceScene, e.sequenceViewport);
    const QImage viewportImage = muffin::mermaid::sequence::renderSequenceSceneToImage(
        *sequenceScene, 1.0, e.sequenceViewport);
    require(e.sequenceViewport.diagramMarginX == 31.0 &&
                e.sequenceViewport.diagramMarginY == 23.0 &&
                e.sequenceViewport.boxMargin == 14.0 &&
                e.sequenceViewport.bottomMarginAdj == 7.0 &&
                !e.sequenceViewport.mirrorActors &&
                e.naturalSize == QSize(qCeil(viewport.width()), qCeil(viewport.height())) &&
                viewportImage.size() == e.naturalSize,
            QStringLiteral("sequence viewport config must determine cache/image dimensions"));
  }

  // --- async request: Loading → renderReady → Ready ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(flow);
    const MermaidRenderEntry first = cache.request(key, flow);
    require(first.status == kLoading, QStringLiteral("first request must be Loading (got %1)").arg((int)first.status));
    require(waitForReady(cache, key), QStringLiteral("renderReady must fire for the key"));
    const MermaidRenderEntry second = cache.request(key, flow);
    require(second.status == kReady && second.scene != nullptr, QStringLiteral("after renderReady, request must be Ready"));
  }

  // --- edit debounce supersedes stale source and reports async errors ---
  {
    MermaidRenderCache cache;
    const QString stale = QStringLiteral("flowchart TB\nOld --> Work");
    const MermaidRenderKey staleKey = MermaidRenderCache::makeKey(stale);
    const MermaidRenderKey latestKey = MermaidRenderCache::makeKey(malformed);
    bool staleRendered = false;
    QObject::connect(&cache, &MermaidRenderCache::renderReady, &app,
                     [&](const MermaidRenderKey& key) {
      if (key == staleKey) staleRendered = true;
    });
    require(cache.requestDebounced(staleKey, stale, 150).status == kLoading,
            QStringLiteral("first debounced source must enter Loading"));
    require(cache.requestDebounced(latestKey, malformed, 5).status == kLoading,
            QStringLiteral("latest debounced source must enter Loading"));
    require(waitForReady(cache, latestKey),
            QStringLiteral("latest debounced source must emit renderReady"));
    const MermaidRenderEntry result =
        cache.requestDebounced(latestKey, malformed, 5);
    require(result.status == kError && !result.errorMessage.isEmpty(),
            QStringLiteral("debounced malformed source must resolve to Error"));

    QEventLoop quietPeriod;
    QTimer::singleShot(180, &quietPeriod, &QEventLoop::quit);
    quietPeriod.exec();
    require(!staleRendered && cache.size() == 1,
            QStringLiteral("superseded debounce input must never render or remain cached"));
  }

  // --- LRU eviction ---
  {
    MermaidRenderCache cache(nullptr, /*capacity=*/2);
    const QString a = QStringLiteral("flowchart TB\nA[A1] --> A[A2]");
    const QString b = QStringLiteral("flowchart TB\nB[B1] --> B[B2]");
    const QString c = QStringLiteral("flowchart TB\nC[C1] --> C[C2]");
    const MermaidRenderKey ka = MermaidRenderCache::makeKey(a);
    const MermaidRenderKey kb = MermaidRenderCache::makeKey(b);
    const MermaidRenderKey kc = MermaidRenderCache::makeKey(c);
    cache.getSync(ka, a);  // [a]
    cache.getSync(kb, b);  // [a,b]
    require(cache.size() == 2, QStringLiteral("size after 2 inserts = %1").arg(cache.size()));
    cache.getSync(kc, c);  // [b,c]  (a evicted — least recently used)
    require(cache.size() == 2, QStringLiteral("size stays at capacity after 3 inserts"));
    const MermaidRenderEntry evicted = cache.getSync(ka, a);  // a was evicted → re-rendered → Ready again
    require(evicted.status == kReady, QStringLiteral("evicted entry re-renders on request"));
  }

  // --- key stability: same inputs → same key ---
  {
    const MermaidRenderKey k1 = MermaidRenderCache::makeKey(flow);
    const MermaidRenderKey k2 = MermaidRenderCache::makeKey(flow);
    require(k1 == k2, QStringLiteral("same inputs must produce the same key"));
    const MermaidRenderKey k3 = MermaidRenderCache::makeKey(flow + QStringLiteral(" "));  // different source
    require(!(k1 == k3), QStringLiteral("different source must produce a different key"));
  }

  // --- complete config propagation: themeVariables + flowchart spacing/font ---
  {
    MermaidRenderCache cache;
    const QString configured = QStringLiteral(
        "%%{init: {\"look\": \"neo\", "
        "\"themeVariables\": {\"mainBkg\": \"#123456\", \"fontSize\": \"14px\"}, "
        "\"flowchart\": {\"nodeSpacing\": 120, \"rankSpacing\": 90, \"curve\": \"linear\"}}}%%\n"
        "flowchart LR\nA[\"`**Bold** label`\"] --> B[Beta]");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(configured), configured);
    require(e.status == kReady && e.scene != nullptr, QStringLiteral("configured flowchart must render"));
    const auto* flowScene = dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(e.scene.get());
    require(flowScene != nullptr, QStringLiteral("configured flowchart must expose a FlowScene"));
    require(!flowScene->nodes.isEmpty() && flowScene->nodes.first().fill == QLatin1String("#123456"),
            QStringLiteral("themeVariables.mainBkg must reach the rendered scene"));
    require(flowScene->nodes.first().label.fontSize == QLatin1String("14px"),
            QStringLiteral("themeVariables.fontSize must reach labels"));
    require(flowScene->look == muffin::mermaid::flowchart::FlowLook::Neo,
            QStringLiteral("top-level look must reach the rendered scene"));
    require(flowScene->nodes.first().label.fontFamily.contains(QLatin1String("Noto Sans")),
            QStringLiteral("default Mermaid labels must use the bundled Noto stack"));
    require(flowScene->nodes.size() == 2 &&
                qAbs(flowScene->nodes.at(1).cx - flowScene->nodes.at(0).cx) > 150.0,
            QStringLiteral("flowchart spacing must reach Dagre"));
  }

  // --- structured labels: markers/tags affect formatting, not visible text ---
  {
    using muffin::mermaid::flowchart::parseFlowLabel;
    using muffin::mermaid::flowchart::parseFlowSvgLabel;
    const auto markdown = parseFlowLabel(QStringLiteral("**Bold** and `code`<br/>next"),
                                         QStringLiteral("markdown"));
    require(markdown.text == QLatin1String("Bold and code\nnext"),
            QStringLiteral("markdown markers and br must be consumed by the label model"));
    require(markdown.formats.size() == 2 && markdown.formats.first().format.fontWeight() == QFont::Bold,
            QStringLiteral("markdown bold/code formatting must be represented structurally"));
    const auto markdownMetrics = muffin::mermaid::flowchart::layoutFlowLabel(
        markdown, QStringLiteral("Noto Sans"), 16.0, 22.0);
    const auto& formattedRuns = markdownMetrics.lines.first().runs;
    require(!formattedRuns.isEmpty() &&
                std::all_of(formattedRuns.cbegin(), formattedRuns.cend(), [](const auto& run) {
                  return !run.preparedGlyphs.isEmpty();
                }) &&
                std::any_of(formattedRuns.cbegin(), formattedRuns.cend(), [](const auto& run) {
                  return run.fontWeight >= QFont::Bold;
                }),
            QStringLiteral("formatted labels must retain styled prepared glyph runs"));
    const auto html = parseFlowLabel(QStringLiteral("<b>Bold</b><br><i>italic</i>"),
                                     QStringLiteral("string"));
    require(html.text == QLatin1String("Bold\nitalic") && html.formats.size() == 2 &&
                html.direction == Qt::LeftToRight,
            QStringLiteral("safe inline HTML must use the same native label model"));
    const auto htmlMetrics = muffin::mermaid::flowchart::layoutFlowLabel(
        html, QStringLiteral("Noto Sans"), 16.0, 22.0);
    require(std::any_of(htmlMetrics.lines.at(1).runs.cbegin(),
                        htmlMetrics.lines.at(1).runs.cend(), [](const auto& run) {
              return run.fontItalic;
            }), QStringLiteral("italic labels must retain the synthetic style face"));
    const auto svgHtml = parseFlowSvgLabel(
        QStringLiteral("<b>Bold</b><br><i>italic</i>"),
        QStringLiteral("string"));
    require(svgHtml.text == QLatin1String("<b> Bold </b>\n<i> italic </i>") &&
                svgHtml.formats.isEmpty() &&
                svgHtml.formattingContext ==
                    muffin::mermaid::flowchart::FlowLabelFormattingContext::
                        FlowSvgFormattedText,
            QStringLiteral("SVG formatted text must retain visible HTML tags"));
    const auto svgMarkdown = parseFlowSvgLabel(
        QStringLiteral("**Bold** next"), QStringLiteral("markdown"));
    require(svgMarkdown.text == QLatin1String("Bold next") &&
                svgMarkdown.formats.size() == 1 &&
                svgMarkdown.formats.first().format.fontWeight() == QFont::Bold,
            QStringLiteral("SVG formatted text must still parse Markdown markers"));
  }

  // --- cluster titles constrain the compound layout width ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "flowchart TB\nsubgraph S[\"<b>Group \u4e2d\u6587</b> "
        "\u0645\u0631\u062d\u0628\u0627 \u05e9\u05dc\u05d5\u05dd\"]\nA[Inside]\nend");
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(source), source);
    const auto* flowScene = dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(entry.scene.get());
    require(entry.status == kReady && entry.scene && flowScene &&
                flowScene->clusters.size() == 1,
            QStringLiteral("wide cluster title must render"));
    const auto& cluster = flowScene->clusters.first();
    const auto titleLayout = muffin::mermaid::flowchart::layoutFlowLabel(
        cluster.label.richText, cluster.label.fontFamily, 16.0, 17.0);
    require(cluster.width + 0.001 >= titleLayout.size.width() + 8.0,
            QStringLiteral("cluster width must include the SVG title inset"));
  }

  // --- wrapped edge labels are immutable layout products ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "flowchart LR\nA[Start] -->|\"A deliberately long edge label that "
        "wraps at the upstream limit\"| B[Finish]");
    const MermaidRenderEntry entry = cache.getSync(
        MermaidRenderCache::makeKey(source), source);
    const auto* flowScene = dynamic_cast<const muffin::mermaid::flowscene::FlowScene*>(entry.scene.get());
    require(entry.status == kReady && entry.scene && flowScene &&
                flowScene->edges.size() == 1,
            QStringLiteral("wrapped edge label must render"));
    const auto& edge = flowScene->edges.first();
    require(edge.label.richText.visualLines.size() == 3 &&
                edge.label.richText.visualLineAdvance > 0.0 &&
                edge.labelSize.width() > 0.0 &&
                edge.labelSize.height() > 0.0,
            QStringLiteral("edge wrapping must reach the immutable scene"));
    const auto metrics = muffin::mermaid::flowchart::layoutFlowLabel(
        edge.label.richText, edge.label.fontFamily, 16.0, 24.0);
    const auto fontMetrics =
        muffin::mermaid::flowchart::flowLabelFontBoundingMetrics(
            edge.label.fontFamily, 16.0);
    require(metrics.lines.size() == 3 &&
                qAbs(metrics.lines.first().baseline -
                     fontMetrics.ascent) < 0.001,
            QStringLiteral("wrapped SVG lines must use the font-table baseline"));
  }

  // --- sequence labels share structured HTML/Markdown/Math/bidi metrics ---
  {
    using muffin::mermaid::sequence::layoutSequenceLabel;
    using muffin::mermaid::sequence::parseSequenceLabel;
    const auto html = parseSequenceLabel(QStringLiteral("\u4e2d\u6587 <b>bold</b><br/>"
                                                        "\u0645\u0631\u062d\u0628\u0627 "
                                                        "\u05e9\u05dc\u05d5\u05dd"));
    const auto htmlMetrics = layoutSequenceLabel(html, QStringLiteral("Noto Sans"), 16.0, 22.0);
    require(html.richText.text == QStringLiteral("\u4e2d\u6587 <b>bold</b>\n"
                                                 "\u0645\u0631\u062d\u0628\u0627 "
                                                 "\u05e9\u05dc\u05d5\u05dd") &&
                htmlMetrics.lines.size() == 2 && htmlMetrics.size.height() >= 44.0,
            QStringLiteral("sequence HTML/br labels must retain visual lines"));
    require(std::all_of(htmlMetrics.lines.cbegin(), htmlMetrics.lines.cend(), [](const auto& line) {
              return line.baseline > 0.0 && line.ascent > 0.0 && line.descent >= 0.0 &&
                     !line.runs.isEmpty();
            }), QStringLiteral("sequence labels must expose baseline/ascent/descent/bidi runs"));
    const auto literalBidi = parseSequenceLabel(
        QStringLiteral("<b>\u4e2d\u6587</b> \u0645\u0631\u062d\u0628\u0627"));
    const auto literalBidiMetrics = layoutSequenceLabel(
        literalBidi, QStringLiteral("Noto Sans"), 16.0, 22.0);
    require(!literalBidi.richText.text.contains(QLatin1Char('\n')) &&
                literalBidiMetrics.lines.size() == 1,
            QStringLiteral("sequence SVG labels must not be paint-time auto-wrapped"));
    const auto& literalRuns = literalBidiMetrics.lines.first().runs;
    require(!literalRuns.isEmpty() &&
                std::any_of(literalRuns.cbegin(), literalRuns.cend(), [](const auto& run) {
                  return run.rightToLeft;
                }) &&
                std::all_of(literalRuns.cbegin(), literalRuns.cend(), [](const auto& run) {
                  return !run.preparedGlyphs.isEmpty() &&
                         run.preparedGlyphs.glyphIndexes().size() ==
                             run.preparedGlyphs.positions().size() &&
                         run.preparedGlyphWidth > 0.0;
                }),
            QStringLiteral("sequence bidi runs must retain complete full-line glyph shaping"));
    const auto markdownMath = parseSequenceLabel(
        QStringLiteral("`**\u901f\u5ea6** $$x^2$$`"),
        muffin::mermaid::sequence::SequenceLabelKind::Note);
    const auto mathMetrics = layoutSequenceLabel(markdownMath, QStringLiteral("Noto Sans"), 16.0, 22.0);
    require(!markdownMath.markdown && !markdownMath.richText.math.isEmpty() &&
                markdownMath.richText.text.startsWith(QLatin1String("`**")) &&
                mathMetrics.size.width() > 0.0,
            QStringLiteral("sequence literal Markdown/Math labels must use structured measurement"));
  }

  // --- structured label height participates in sequence vertical placement ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "sequenceDiagram\n"
        "box rgb(238, 246, 255) <b>\u670d\u52a1</b>\n"
        "participant A as `**\u5ba2\u6237\u7aef**`\n"
        "participant B as \u062e\u0627\u062f\u0645\n"
        "end\n"
        "A->>B:\u7b2c\u4e00\u884c<br/>\u0645\u0631\u062d\u0628\u0627 "
        "\u05e9\u05dc\u05d5\u05dd\n"
        "note over A,B:\u8bf4\u660e<br/>$$x^2$$\n"
        "B-->>A:after");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(source), source);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(e.scene.get());
    require(e.status == kReady && sequenceScene != nullptr &&
                sequenceScene->messages.size() == 2 && sequenceScene->notes.size() == 1,
            QStringLiteral("structured sequence labels must render end-to-end"));
    require(sequenceScene->messages.first().labelRect.height() >= 44.0 &&
                sequenceScene->notes.first().rect.height() >= 64.0 &&
                sequenceScene->messages.at(1).lineY - sequenceScene->messages.first().lineY > 100.0,
            QStringLiteral("multiline label height must advance sequence geometry"));
  }

  // --- production wrap uses the same participant/note/message documents ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "%%{init: {\"sequence\": {\"width\": 100, \"actorMargin\": 50, "
        "\"wrapPadding\": 10}}}%%\n"
        "sequenceDiagram\n"
        "participant A as wrap:alpha beta gamma delta epsilon\n"
        "participant B as Worker\n"
        "A->>B:wrap:alpha beta gamma delta epsilon zeta\n"
        "Note over A,B:wrap:alpha beta gamma delta epsilon zeta");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(source), source);
    const auto* sequenceScene = dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(e.scene.get());
    require(e.status == kReady && sequenceScene != nullptr &&
                sequenceScene->participants.size() == 2 &&
                sequenceScene->messages.size() == 1 && sequenceScene->notes.size() == 1,
            QStringLiteral("wrapped sequence labels must render end-to-end"));
    const auto& scene = *sequenceScene;
    require(scene.participants.first().label.contains(QLatin1Char('\n')) &&
                scene.messages.first().label.contains(QLatin1Char('\n')) &&
                scene.notes.first().label.contains(QLatin1Char('\n')),
            QStringLiteral("participant/message/note wrap display must reach the scene"));
    const qreal anchorGap = scene.participants.at(1).anchorX - scene.participants.at(0).anchorX;
    require(qAbs(anchorGap - 150.0) < 0.01,
            QStringLiteral("wrapped message labels must not enlarge actor margins (gap=%1)")
                .arg(anchorGap));
    require(scene.participants.first().logicalRect.height() > 65.0,
            QStringLiteral("wrapped participant height must reach participant geometry"));
  }

  // --- sequence themeVariables reach their distinct painter channels ---
  {
    MermaidRenderCache cache;
    const QString source = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"actorBkg\": \"#101112\", "
        "\"actorBorder\": \"#202122\", \"actorTextColor\": \"#303132\", "
        "\"actorLineColor\": \"#404142\", \"signalColor\": \"#505152\", "
        "\"signalTextColor\": \"#606162\", \"noteBkgColor\": \"#707172\", "
        "\"noteBorderColor\": \"#808182\", \"noteTextColor\": \"#909192\", "
        "\"activationBkgColor\": \"#a0a1a2\", "
        "\"activationBorderColor\": \"#b0b1b2\", "
        "\"labelBoxBkgColor\": \"#c0c1c2\", "
        "\"labelBoxBorderColor\": \"#d0d1d2\", "
        "\"labelTextColor\": \"#e0e1e2\", \"loopTextColor\": \"#f0f1f2\", "
        "\"sequenceNumberColor\": \"#123456\"}}}%%\n"
        "sequenceDiagram\nautonumber\nA->>+B:call\nNote over A,B:note\nalt branch\nB-->>-A:return\nend");
    const auto entry=cache.getSync(MermaidRenderCache::makeKey(source),source);
    const auto* sequenceScene=dynamic_cast<const muffin::mermaid::sequence::SequenceScene*>(entry.scene.get());
    require(entry.status==kReady&&sequenceScene,
            QStringLiteral("sequence themeVariables case must render"));
    const auto& style=sequenceScene->style;
    require(style.actorFill==QLatin1String("#101112")&&style.actorStroke==QLatin1String("#202122")&&
                style.actorTextColor==QLatin1String("#303132")&&style.lifelineColor==QLatin1String("#404142")&&
                style.signalColor==QLatin1String("#505152")&&style.signalTextColor==QLatin1String("#606162")&&
                style.noteFill==QLatin1String("#707172")&&style.noteStroke==QLatin1String("#808182")&&
                style.noteTextColor==QLatin1String("#909192")&&style.activationFill==QLatin1String("#a0a1a2")&&
                style.activationStroke==QLatin1String("#b0b1b2")&&style.labelFill==QLatin1String("#c0c1c2")&&
                style.labelStroke==QLatin1String("#d0d1d2")&&style.labelTextColor==QLatin1String("#e0e1e2")&&
                style.loopTextColor==QLatin1String("#f0f1f2")&&style.sequenceNumberColor==QLatin1String("#123456"),
            QStringLiteral("sequence themeVariables did not reach all painter channels"));
  }

  // --- regression: the example.md diagram (nested compound + cluster-crossing
  // edges + `-- text -->` labeled edges + self-loop + cycle). This previously
  // failed twice: (1) `A -- x --> B` labeled edges mis-parsed as a node, and
  // (2) addBorderSegments held a QHash pointer across a rehash → use-after-free
  // → non-deterministic crash. Must render a non-degenerate scene.
  {
    MermaidRenderCache cache;
    const QString src = QStringLiteral(
        "flowchart TB\n"
        "    subgraph C1[\"One\"]\n"
        "        A([Browser]) --> B[Web]\n"
        "        B --> C{Cache?}\n"
        "        C -- yes --> D[(Cache)]\n"
        "    end\n"
        "    subgraph C2[\"Two\"]\n"
        "        subgraph Inner[\"Inner\"]\n"
        "            E[Gw] --> F[Auth]\n"
        "            F --> G[(DB)]\n"
        "        end\n"
        "    end\n"
        "    C -- no --> E\n"
        "    B --> B\n"
        "    G --> E\n");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(src), src);
    require(e.status == kReady, QStringLiteral("compound-crossing+self-edge graph must render (got %1)").arg((int)e.status));
    require(e.naturalSize.width() > 0 && e.naturalSize.height() > 0,
            QStringLiteral("compound graph must have a non-degenerate size (got %1x%2)").arg(e.naturalSize.width()).arg(e.naturalSize.height()));
    require(e.scene != nullptr, QStringLiteral("Ready entry must carry a scene"));
  }

  qDebug().noquote() << "MermaidRenderCacheTest: sync ready/error/unsupported + async/debounced rendering + LRU + key stability";
  return 0;
}
