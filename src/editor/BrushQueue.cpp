#include "editor/BrushQueue.h"

namespace muffin {

BrushQueue::BrushQueue(QObject* parent) : QObject(parent) {}

void BrushQueue::requestBlockRefresh(NodeId blockId) {
  if (!blockId.isValid()) {
    requestFullRefresh();
    return;
  }

  emit blockRefreshRequested(blockId);
}

void BrushQueue::requestFullRefresh() {
  emit fullRefreshRequested();
}

}  // namespace muffin
