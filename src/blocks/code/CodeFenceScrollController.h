#pragma once

#include "document/NodeId.h"

#include <QHash>
#include <QPair>
#include <QVector>

namespace muffin {

// Per-code-fence horizontal scroll state, keyed by NodeId. Lives outside BlockLayout because
// BlockLayout is rebuilt on every edit; this survives those rebuilds. The typing path uses
// CloneMode::PreserveIds, so an offset persists while the user edits the block it belongs to.
// BlockLayout reads offset/contentWidth at paint/hit time (as a const*); EditorView mutates the
// offset on wheel/drag; BlockLayoutBuilder writes contentWidth (the longest source line's width)
// so the scrollbar thumb geometry and the scroll clamp all agree on the same number.
class CodeFenceScrollController final {
public:
  struct Entry {
    qreal offset = 0.0;
    qreal contentWidth = 0.0;  // pixel width of the longest source line; 0 until the builder measures it
  };

  qreal offsetFor(NodeId id) const;
  qreal contentWidthFor(NodeId id) const;
  Entry entryFor(NodeId id) const;  // default-constructed when absent

  void setOffset(NodeId id, qreal offset);
  void setContentWidth(NodeId id, qreal width);

  // Clamp an offset into the valid [0, max(0, contentWidth - visibleWidth)] range for a block.
  qreal clampedOffset(NodeId id, qreal visibleWidth) const;

  // Remap entries to new ids after a structural reparse (oldId -> newId). Mirrors the
  // remap-by-index pattern in CodeFenceController::setLanguageForCodeFence; most edit paths
  // preserve ids so this is rarely needed.
  void remapAfterReparse(const QVector<QPair<NodeId, NodeId>>& oldToNew);

  void clear();

private:
  QHash<NodeId, Entry> entries_;
};

}  // namespace muffin
