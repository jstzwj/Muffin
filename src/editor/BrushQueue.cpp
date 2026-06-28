#include "editor/BrushQueue.h"

#include <QSet>
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
  // Do NOT short-circuit on a pending top-level range: the downstream handler
  // (EditorController) refreshes both the range and the remaining dirty blocks,
  // so blocks that fall outside the structural-change range must still be merged
  // here. Early-returning here silently drops them and leaves their visuals stale.
  QSet<NodeId> pendingIds;
  pendingIds.reserve(pending_.layoutDirtyBlocks.size() + blockIds.size());
  for (const NodeId& id : pending_.layoutDirtyBlocks) {
    pendingIds.insert(id);
  }

  bool sawValidBlock = false;
  for (const NodeId& id : blockIds) {
    if (!id.isValid()) {
      continue;
    }
    sawValidBlock = true;
    if (!pendingIds.contains(id)) {
      pendingIds.insert(id);
      pending_.layoutDirtyBlocks.push_back(id);
    }
  }
  if (!sawValidBlock) {
    requestFullRefresh();
    return;
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
    // A different range is already pending — the classic trigger is rapid successive structural
    // edits/undos (each records a range with a different block count: old=7→6→5…). Previously this
    // escalated to requestFullRefresh, a whole-document layout rebuild that is ≈22s on a 100MB doc
    // (the Ctrl+Z-rapid-press hang). Instead, flush the pending range NOW (synchronously, via
    // refreshRequested) so the layout catches up to it, then queue the new range. Each range then
    // flushes against the layout state produced by the previous range — exactly as if the edits had
    // been spaced out — so each satisfies rebuildTopLevelRange's
    // layoutCount-oldCount+newCount==documentCount invariant and rebuilds locally. Block-level
    // dirty ids pending alongside the flushed range are processed by the same consumer call.
    flush();
    if (pending_.fullLayoutDirty) {
      // flush() can itself escalate (e.g. an empty/invalid block batch); don't override that with
      // a range refresh on top.
      scheduleFlush();
      return;
    }
  }
  // Preserve any pending block-level refreshes.  The downstream handler
  // processes both the top-level range and remaining dirty blocks so that
  // visual updates for blocks outside the structural-change range are not
  // silently lost.
  pending_.topLevelRangeDirty = range;
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
