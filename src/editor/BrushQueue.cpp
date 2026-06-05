#include "editor/BrushQueue.h"

#include <algorithm>

#include <QTimer>

namespace muffin {

BrushQueue::BrushQueue(QObject* parent) : QObject(parent) {}

void BrushQueue::requestBlockRefresh(NodeId blockId) {
  if (!blockId.isValid()) {
    requestFullRefresh();
    return;
  }

  requestBlocksRefresh({blockId});
}

void BrushQueue::requestBlocksRefresh(QVector<NodeId> blockIds) {
  if (pending_.fullLayoutDirty) {
    scheduleFlush();
    return;
  }
  if (pending_.topLevelRangeDirty.isValid()) {
    scheduleFlush();
    return;
  }
  blockIds.erase(std::remove_if(blockIds.begin(), blockIds.end(), [](const NodeId& id) { return !id.isValid(); }), blockIds.end());
  if (blockIds.isEmpty()) {
    requestFullRefresh();
    return;
  }
  QVector<NodeId> uniqueIds;
  uniqueIds.reserve(blockIds.size());
  for (const NodeId& id : blockIds) {
    if (!uniqueIds.contains(id)) {
      uniqueIds.push_back(id);
    }
  }
  blockIds = std::move(uniqueIds);
  for (const NodeId& id : blockIds) {
    if (!pending_.layoutDirtyBlocks.contains(id)) {
      pending_.layoutDirtyBlocks.push_back(id);
    }
  }
  scheduleFlush();
}

void BrushQueue::requestTopLevelRangeRefresh(TopLevelRangeChange range) {
  if (!range.isValid()) {
    requestFullRefresh();
    return;
  }
  if (pending_.fullLayoutDirty) {
    scheduleFlush();
    return;
  }
  if (pending_.topLevelRangeDirty.isValid() && pending_.topLevelRangeDirty != range) {
    requestFullRefresh();
    return;
  }
  pending_.topLevelRangeDirty = range;
  pending_.layoutDirtyBlocks.clear();
  scheduleFlush();
}

void BrushQueue::requestFullRefresh() {
  pending_.fullLayoutDirty = true;
  pending_.layoutDirtyBlocks.clear();
  pending_.topLevelRangeDirty = {};
  scheduleFlush();
}

void BrushQueue::flush() {
  if (!pending_.fullLayoutDirty && !pending_.topLevelRangeDirty.isValid() && pending_.layoutDirtyBlocks.isEmpty()) {
    flushScheduled_ = false;
    return;
  }

  RefreshRequest request = std::move(pending_);
  pending_ = {};
  flushScheduled_ = false;
  emit refreshRequested(std::move(request));
}

void BrushQueue::scheduleFlush() {
  if (flushScheduled_) {
    return;
  }
  flushScheduled_ = true;
  QTimer::singleShot(0, this, [this] { flush(); });
}

}  // namespace muffin
