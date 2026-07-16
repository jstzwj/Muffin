// Milestone I-1 unit test for MermaidRenderCache: sync render (ready/error/
// unsupported), async request (loading → renderReady → ready), LRU eviction,
// key stability.

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/sequence/SequenceLabel.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>

#include <algorithm>
#include <cstdlib>

using namespace muffin::mermaid::editor;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

constexpr auto kReady = MermaidRenderStatus::Ready;
constexpr auto kLoading = MermaidRenderStatus::Loading;
constexpr auto kError = MermaidRenderStatus::Error;

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

  // --- getSync: valid flowchart → Ready + scene + natural size ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(flow);
    const MermaidRenderEntry e = cache.getSync(key, flow);
    require(e.status == kReady, QStringLiteral("valid flowchart should be Ready (got %1)").arg((int)e.status));
    require(e.scene != nullptr, QStringLiteral("Ready entry must carry a scene"));
    require(e.naturalSize.width() > 0 && e.naturalSize.height() > 0, QStringLiteral("natural size must be positive"));
    // Second getSync hits the cache (same entry, no re-render).
    const MermaidRenderEntry e2 = cache.getSync(key, flow);
    require(e2.status == kReady && e2.scene == e.scene, QStringLiteral("cache hit returns the same scene"));
  }

  // --- getSync: malformed → Error ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(malformed);
    const MermaidRenderEntry e = cache.getSync(key, malformed);
    require(e.status == kError, QStringLiteral("malformed flowchart should be Error (got %1)").arg((int)e.status));
    require(!e.errorMessage.isEmpty(), QStringLiteral("Error entry must carry a message"));
  }

  // --- getSync: non-flowchart → Unsupported ---
  {
    MermaidRenderCache cache;
    const MermaidRenderKey key = MermaidRenderCache::makeKey(sequence);
    const MermaidRenderEntry e = cache.getSync(key, sequence);
    require(e.status == kReady && e.sequenceScene != nullptr,
            QStringLiteral("sequenceDiagram should be Ready (got %1)").arg((int)e.status));
    require(e.sequenceScene->participants.size() == 2 && e.sequenceScene->messages.size() == 1 &&
                e.naturalSize.width() > 0 && e.naturalSize.height() > 0,
            QStringLiteral("sequenceDiagram scene must contain participant/message geometry"));
  }

  // --- sequence config + box measurements reach the production scene ---
  {
    MermaidRenderCache cache;
    const QString configured = QStringLiteral(
        "%%{init: {\"sequence\": {\"mirrorActors\": false, "
        "\"hideUnusedParticipants\": true, \"width\": 180, \"boxMargin\": 14}}}%%\n"
        "sequenceDiagram\n"
        "participant UNUSED as Hidden\n"
        "box rgb(238, 246, 255) Services\n"
        "participant A as API\n"
        "participant B as Worker\n"
        "end\n"
        "A->>B:call");
    const MermaidRenderEntry e = cache.getSync(MermaidRenderCache::makeKey(configured), configured);
    require(e.status == kReady && e.sequenceScene != nullptr,
            QStringLiteral("configured sequence must render"));
    require(e.sequenceScene->participants.size() == 2 && e.sequenceScene->boxes.size() == 1,
            QStringLiteral("sequence hideUnusedParticipants/box config must reach the scene"));
    require(e.sequenceScene->participants.first().logicalRect.width() == 180.0 &&
                !e.sequenceScene->participants.first().drawBottom &&
                e.sequenceScene->participants.first().lifelineStopY == 2000.0,
            QStringLiteral("sequence width/mirrorActors config must reach layout"));
    require(e.sequenceScene->boxes.first().label == QLatin1String("Services") &&
                e.sequenceScene->boxes.first().labelRect.height() > 0.0,
            QStringLiteral("sequence box title must be measured and retained"));
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
    require(!e.scene->nodes.isEmpty() && e.scene->nodes.first().fill == QLatin1String("#123456"),
            QStringLiteral("themeVariables.mainBkg must reach the rendered scene"));
    require(e.scene->nodes.first().label.fontSize == QLatin1String("14px"),
            QStringLiteral("themeVariables.fontSize must reach labels"));
    require(e.scene->look == muffin::mermaid::flowchart::FlowLook::Neo,
            QStringLiteral("top-level look must reach the rendered scene"));
    require(e.scene->nodes.first().label.fontFamily.contains(QLatin1String("Noto Sans")),
            QStringLiteral("default Mermaid labels must use the bundled Noto stack"));
    require(e.scene->nodes.size() == 2 &&
                qAbs(e.scene->nodes.at(1).cx - e.scene->nodes.at(0).cx) > 150.0,
            QStringLiteral("flowchart spacing must reach Dagre"));
  }

  // --- structured labels: markers/tags affect formatting, not visible text ---
  {
    using muffin::mermaid::flowchart::parseFlowLabel;
    const auto markdown = parseFlowLabel(QStringLiteral("**Bold** and `code`<br/>next"),
                                         QStringLiteral("markdown"));
    require(markdown.text == QLatin1String("Bold and code\nnext"),
            QStringLiteral("markdown markers and br must be consumed by the label model"));
    require(markdown.formats.size() == 2 && markdown.formats.first().format.fontWeight() == QFont::Bold,
            QStringLiteral("markdown bold/code formatting must be represented structurally"));
    const auto html = parseFlowLabel(QStringLiteral("<b>Bold</b><br><i>italic</i>"),
                                     QStringLiteral("string"));
    require(html.text == QLatin1String("Bold\nitalic") && html.formats.size() == 2,
            QStringLiteral("safe inline HTML must use the same native label model"));
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
    require(e.status == kReady && e.sequenceScene != nullptr &&
                e.sequenceScene->messages.size() == 2 && e.sequenceScene->notes.size() == 1,
            QStringLiteral("structured sequence labels must render end-to-end"));
    require(e.sequenceScene->messages.first().labelRect.height() >= 44.0 &&
                e.sequenceScene->notes.first().rect.height() >= 64.0 &&
                e.sequenceScene->messages.at(1).lineY - e.sequenceScene->messages.first().lineY > 100.0,
            QStringLiteral("multiline label height must advance sequence geometry"));
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

  qDebug().noquote() << "MermaidRenderCacheTest: sync ready/error/unsupported + async loading→ready + LRU + key stability";
  return 0;
}
