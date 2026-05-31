#pragma once

#include "document/NodeId.h"

#include <QObject>

namespace muffin {

class BrushQueue final : public QObject {
  Q_OBJECT

public:
  explicit BrushQueue(QObject* parent = nullptr);

  void requestBlockRefresh(NodeId blockId);
  void requestFullRefresh();

signals:
  void blockRefreshRequested(NodeId blockId);
  void fullRefreshRequested();
};

}  // namespace muffin
