#pragma once

#include "document/NodeId.h"
#include "document/TopLevelRangeChange.h"

#include <QObject>
#include <QVector>

namespace muffin {

class BrushQueue final : public QObject {
  Q_OBJECT

public:
  struct RefreshRequest {
    QVector<NodeId> layoutDirtyBlocks;
    TopLevelRangeChange topLevelRangeDirty;
    bool fullLayoutDirty = false;
  };

  explicit BrushQueue(QObject* parent = nullptr);

  void requestBlockRefresh(NodeId blockId);
  void requestBlocksRefresh(QVector<NodeId> blockIds);
  void requestTopLevelRangeRefresh(TopLevelRangeChange range);
  void requestFullRefresh();
  void flush();

signals:
  void refreshRequested(RefreshRequest request);

private:
  void scheduleFlush();

  RefreshRequest pending_;
  bool flushScheduled_ = false;
};

}  // namespace muffin
