#pragma once

// Cached, async mermaid→FlowScene renderer for the editor + export (milestone I).
//
// The editor renders a ```mermaid code fence as the native diagram. Producing a
// FlowScene (parse + measure + dagre layout + theme + buildFlowScene) is too
// expensive to do in the paint event, so it is run OFF the UI thread and cached.
// The cache stores the immutable FlowScene (pure data — geometry + resolved
// colours + label text; NO QFont/QPainter state), so producing it on a worker
// thread is safe (only QFontMetrics is touched, which is thread-safe). The actual
// pixel painting stays on the GUI thread (FlowScenePainter in BlockLayout).
//
// Key = (sha256(source), mermaidTheme, contentWidth, dpr, fontSize) — everything
// the render depends on (docs/mermaid-flowchart-remaining-plan.md §12.2). Theme
// switch / zoom / resize / font change → new key → automatic invalidation; old
// entries age out via LRU. Mirrors DocumentSession's async pattern (QtConcurrent +
// QFutureWatcher + generation-guarded commit).

#include "mermaid/scene/FlowScene.h"
#include "mermaid/sequence/SequenceScene.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QFutureWatcher>

#include <memory>

namespace muffin::mermaid::editor {

struct MermaidRenderKey {
  QByteArray sourceHash;  // sha256 of the (preprocessed) mermaid source
  QString theme;          // resolved mermaid theme name ("default", "dark", …)
  bool operator==(const MermaidRenderKey&) const = default;
};
inline size_t qHash(const MermaidRenderKey& k, size_t seed = 0) noexcept {
  return qHashMulti(seed, k.sourceHash, k.theme);
}

enum class MermaidRenderStatus { Absent, Loading, Ready, Error, Unsupported };

struct MermaidRenderEntry {
  MermaidRenderStatus status = MermaidRenderStatus::Absent;
  std::shared_ptr<const flowscene::FlowScene> scene;  // set when Ready
  std::shared_ptr<const sequence::SequenceScene> sequenceScene;
  QSize naturalSize;                                   // scene.bounds size (logical px)
  QString errorMessage;                                // set when Error
};

class MermaidRenderCache : public QObject {
  Q_OBJECT
public:
  explicit MermaidRenderCache(QObject* parent = nullptr, int capacity = 64);

  // Build a key from the source. Extracts the mermaid theme the source declares
  // (%%{init:{theme}}%%, else "default"). The FlowScene is a vector model — it
  // depends ONLY on source + theme (not on editor width/DPR/font: those affect
  // only the paint transform, recomputed each paint), so the key is just those.
  static MermaidRenderKey makeKey(const QString& source);

  // Async (editor): returns the current entry for the key (Absent on first call,
  // which launches a worker). When the worker finishes, renderReady(key) fires on
  // the GUI thread; the next request() returns Ready/Error/Unsupported. Never blocks.
  MermaidRenderEntry request(const MermaidRenderKey& key, const QString& source);

  // Sync (export/print): render-or-fetch NOW (blocks the calling thread). Used by
  // one-shot export where blocking is acceptable and there is no event loop to
  // receive renderReady.
  MermaidRenderEntry getSync(const MermaidRenderKey& key, const QString& source);

  void clear();
  int size() const { return static_cast<int>(entries_.size()); }

  // One-shot render of a mermaid source to a `data:image/png;base64,…` URL (or
  // empty on Error/Unsupported/Loading). Used by HTML export (milestone I-5): the
  // serializer embeds the PNG inline. Renders at `dpr` for crispness; synchronous.
  static QString renderMermaidSourceToPngDataUrl(const QString& source, qreal dpr = 2.0);

signals:
  void renderReady(MermaidRenderKey key);

private:
  // Pure render worker (no `this` state read — thread-safe). Runs the full
  // pipeline: preprocess → detect → (flowchart only) parse+measure+layout+theme+
  // buildFlowScene. Non-flowchart → Unsupported; FlowchartParseError → Error.
  static MermaidRenderEntry renderSource(const QString& source, const QString& theme);

  void touch(const MermaidRenderKey& key);  // LRU: mark key most-recent
  void evict();
  void commit(const MermaidRenderKey& key, const MermaidRenderEntry& entry);

  int capacity_;
  QHash<MermaidRenderKey, MermaidRenderEntry> entries_;
  QList<MermaidRenderKey> lru_;  // front = least-recently-used
  int generation_ = 0;           // bumped per in-flight request to drop stale commits
  QHash<QFutureWatcher<MermaidRenderEntry>*, MermaidRenderKey> watchers_;
};

}  // namespace muffin::mermaid::editor
