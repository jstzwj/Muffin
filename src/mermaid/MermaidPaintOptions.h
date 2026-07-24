#pragma once

#include <QRectF>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid {

// Deterministic paint-work counters used by the large-scene regression gate.
// A primitive is one independently cullable scene item (node, edge path,
// label, cluster, note, activation, etc.).
struct MermaidPaintStats {
  qsizetype consideredPrimitives = 0;
  qsizetype paintedPrimitives = 0;
  qsizetype culledPrimitives = 0;
};

struct MermaidPaintOptions {
  // Disabled by default so image/PDF/print callers always paint the full scene.
  bool cullToVisibleRect = false;
  QRectF visibleSceneRect;
  // Covers strokes, markers, rough jitter and shadows crossing the clip edge.
  qreal overscan = 24.0;
  MermaidPaintStats* stats = nullptr;

  // A non-negative timestamp enables live animation. The default keeps the
  // deterministic CSS initial frame used by PNG/PDF/print export.
  qreal animationTimeSeconds = -1.0;

  // Runtime-only Sequence Diagram menu state. `forceMenus` is stored in the
  // immutable scene; this set contains actor ids toggled open by the editor.
  const QSet<QString>* openSequenceMenus = nullptr;
};

inline bool mermaidPrimitiveIsVisible(
    QRectF primitiveBounds, const MermaidPaintOptions& options) {
  if (options.stats) ++options.stats->consideredPrimitives;

  bool visible = true;
  const QRectF viewport = options.visibleSceneRect.normalized();
  if (options.cullToVisibleRect && viewport.width() > 0.0 &&
      viewport.height() > 0.0 &&
      std::isfinite(primitiveBounds.x()) &&
      std::isfinite(primitiveBounds.y()) &&
      std::isfinite(primitiveBounds.width()) &&
      std::isfinite(primitiveBounds.height())) {
    primitiveBounds = primitiveBounds.normalized();
    if (primitiveBounds.width() <= 0.0)
      primitiveBounds.adjust(-0.5, 0.0, 0.5, 0.0);
    if (primitiveBounds.height() <= 0.0)
      primitiveBounds.adjust(0.0, -0.5, 0.0, 0.5);
    const qreal margin = std::max<qreal>(0.0, options.overscan);
    visible = viewport.adjusted(-margin, -margin, margin, margin)
                  .intersects(primitiveBounds);
  }

  if (options.stats) {
    if (visible) ++options.stats->paintedPrimitives;
    else ++options.stats->culledPrimitives;
  }
  return visible;
}

}  // namespace muffin::mermaid
