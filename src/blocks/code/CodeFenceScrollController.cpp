#include "blocks/code/CodeFenceScrollController.h"

namespace muffin {

qreal CodeFenceScrollController::offsetFor(NodeId id) const {
  const auto it = entries_.constFind(id);
  return it != entries_.constEnd() ? it->offset : 0.0;
}

qreal CodeFenceScrollController::contentWidthFor(NodeId id) const {
  const auto it = entries_.constFind(id);
  return it != entries_.constEnd() ? it->contentWidth : 0.0;
}

CodeFenceScrollController::Entry CodeFenceScrollController::entryFor(NodeId id) const {
  const auto it = entries_.constFind(id);
  return it != entries_.constEnd() ? it.value() : Entry{};
}

void CodeFenceScrollController::setOffset(NodeId id, qreal offset) {
  entries_[id].offset = offset;
}

void CodeFenceScrollController::setContentWidth(NodeId id, qreal width) {
  entries_[id].contentWidth = width;
}

qreal CodeFenceScrollController::clampedOffset(NodeId id, qreal visibleWidth) const {
  const Entry e = entryFor(id);
  const qreal maxOffset = qMax<qreal>(0.0, e.contentWidth - visibleWidth);
  return qBound<qreal>(0.0, e.offset, maxOffset);
}

void CodeFenceScrollController::remapAfterReparse(const QVector<QPair<NodeId, NodeId>>& oldToNew) {
  if (oldToNew.isEmpty()) {
    return;
  }
  QHash<NodeId, Entry> remapped;
  remapped.reserve(entries_.size());
  for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
    NodeId key = it.key();
    for (const auto& pair : oldToNew) {
      if (pair.first == key) {
        key = pair.second;
        break;
      }
    }
    remapped.insert(key, it.value());
  }
  entries_ = std::move(remapped);
}

void CodeFenceScrollController::clear() {
  entries_.clear();
}

}  // namespace muffin
