// Milestone I-1 unit test for MermaidRenderCache: sync render (ready/error/
// unsupported), async request (loading → renderReady → ready), LRU eviction,
// key stability.

#include "mermaid/editor/MermaidRenderCache.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>

#include <cstdlib>

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
    require(e.status == kUnsupported, QStringLiteral("sequenceDiagram should be Unsupported (got %1)").arg((int)e.status));
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
