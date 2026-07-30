#pragma once

// Cached, async Mermaid scene renderer for the editor + export (milestone I).
//
// The editor renders a ```mermaid code fence as a native diagram. Producing a
// scene (parse + measure + layout + theme + immutable scene construction) is too
// expensive to do in the paint event, so it is run OFF the UI thread and cached.
// The cache stores immutable scene data (geometry + resolved colours + label
// text; NO QPainter state), so producing it on a worker thread is safe. Actual
// pixel painting stays on the GUI thread in BlockLayout.
//
// Key = (sha256(source), mermaidTheme). Width and DPR affect only the final paint
// transform. Old entries age out via LRU. Mirrors DocumentSession's async
// QtConcurrent + QFutureWatcher pattern.

#include "mermaid/classdiagram/ClassScenePainter.h"
#include "mermaid/erdiagram/ErScenePainter.h"
#include "mermaid/MermaidDiagnostic.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/MermaidScene.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/state/StateScenePainter.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QFutureWatcher>
#include <QTimer>

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
  // Uniform scene pointer (any family). Export and the on-screen editor consume
  // it through the MermaidScene virtuals (paint/sceneBounds/toJsonObject);
  // callers needing a typed view (e.g. tests inspecting family geometry)
  // dynamic_cast from this pointer.
  std::shared_ptr<const MermaidScene> scene;  // set when Ready
  MermaidRenderMetadata metadata;
  QSize naturalSize;                                   // scene.bounds size (logical px)
  QString errorMessage;                                // set for Error/Unsupported
  MermaidDiagnostic diagnostic;                        // family-neutral source diagnostic
  QJsonObject errorDiagnostic;                         // structured Error details when available
};

struct MermaidPngRenderResult {
  QString dataUrl;
  MermaidRenderMetadata metadata;
};

struct MermaidSvgRenderResult {
  QByteArray svg;
  QString dataUrl;
  MermaidRenderMetadata metadata;
};

class MermaidRenderCache : public QObject {
  Q_OBJECT
public:
  explicit MermaidRenderCache(QObject* parent = nullptr, int capacity = 64);

  // Build a key from the source. Extracts the mermaid theme the source declares
  // (%%{init:{theme}}%%, else "default"). Native scenes depend only on source
  // and theme; editor width/DPR affect the paint transform, recomputed per paint.
  static MermaidRenderKey makeKey(const QString& source);

  // Async (editor): returns Loading on the first call and launches a worker.
  // When the worker finishes, renderReady(key) fires on
  // the GUI thread; the next request() returns Ready/Error/Unsupported. Never blocks.
  MermaidRenderEntry request(const MermaidRenderKey& key, const QString& source);

  // Async validation while the caret is inside a Mermaid fence. Only the latest
  // source submitted during the debounce window is launched, preventing a full
  // parse/layout task for every keystroke.
  MermaidRenderEntry requestDebounced(
      const MermaidRenderKey& key, const QString& source, int delayMs = 250);

  // Sync (export/print): render-or-fetch NOW (blocks the calling thread). Used by
  // one-shot export where blocking is acceptable and there is no event loop to
  // receive renderReady.
  MermaidRenderEntry getSync(const MermaidRenderKey& key, const QString& source);

  void clear();
  int size() const { return static_cast<int>(entries_.size()); }

  // One-shot render of a mermaid source to a `data:image/png;base64,…` URL (or
  // empty on Error/Unsupported/Loading). Used by HTML export (milestone I-5): the
  // serializer embeds the PNG inline. Renders at `dpr` for crispness; synchronous.
  static MermaidPngRenderResult renderMermaidSourceToPng(
      const QString& source, qreal dpr = 2.0);
  static QString renderMermaidSourceToPngDataUrl(const QString& source, qreal dpr = 2.0);

  // Deterministic, vector-native SVG export. `instanceIndex` disambiguates
  // repeated equal diagrams embedded in one HTML document without changing
  // standalone output (the default index is zero).
  static MermaidSvgRenderResult renderMermaidSourceToSvg(
      const QString& source, qsizetype instanceIndex = 0);
  static QString renderMermaidSourceToSvgDataUrl(
      const QString& source, qsizetype instanceIndex = 0);

signals:
  void renderReady(MermaidRenderKey key);

private:
  // Pure render worker (no `this` state read — thread-safe). Runs the full
  // pipeline: preprocess -> detect -> parse/measure/layout/theme/scene for the
  // supported family. Unknown families become Unsupported; parser/layout
  // exceptions become Error.
  static MermaidRenderEntry renderSource(const QString& source, const QString& theme);

  void launchWorker(const MermaidRenderKey& key, const QString& source);
  void cancelDebouncedRequest(bool removeLoadingEntry);
  void touch(const MermaidRenderKey& key);  // LRU: mark key most-recent
  void evict();
  void commit(const MermaidRenderKey& key, const MermaidRenderEntry& entry);

  int capacity_;
  QHash<MermaidRenderKey, MermaidRenderEntry> entries_;
  QList<MermaidRenderKey> lru_;  // front = least-recently-used
  QHash<QFutureWatcher<MermaidRenderEntry>*, MermaidRenderKey> watchers_;
  QTimer debounceTimer_;
  bool debouncePending_ = false;
  MermaidRenderKey debouncedKey_;
  QString debouncedSource_;
};

}  // namespace muffin::mermaid::editor
