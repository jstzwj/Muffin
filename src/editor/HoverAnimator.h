#pragma once

#include "document/NodeId.h"

#include <QObject>

#include <functional>

class QElapsedTimer;
class QTimer;

namespace muffin {

// Animates a single block's hover-glow phase 0..1, idle-gated: the timer runs
// only while a fade is in flight, so a static document costs nothing. Phase 1 =
// fully hovered, 0 = not. On each tick the owner-supplied `repaintBlock` fires
// for the animated block so its rect is repainted with the new phase.
//
// MVP: one block at a time. When hover moves from A to B, A's glow snaps off (its
// rect is repainted once without glow) while B fades in; unhover fades B out.
class HoverAnimator : public QObject {
  Q_OBJECT
public:
  explicit HoverAnimator(QObject* parent = nullptr);

  // Begin hover on `blockId` (fade 0→1) or end it (blockId invalid → fade →0).
  // `durationMs` is the fade time; 0 → snap to target with no animation.
  void setHovered(NodeId blockId, qreal durationMs);

  NodeId animatedBlockId() const { return blockId_; }
  qreal phase() const { return phase_; }
  bool isAnimating() const;

  // Owner installs this; called with the block whose rect needs repainting.
  std::function<void(NodeId)> repaintBlock;

private slots:
  void onTick();

private:
  QTimer* timer_ = nullptr;
  QElapsedTimer* clock_ = nullptr;
  NodeId blockId_;
  qreal phase_ = 0.0;
  qreal target_ = 0.0;
  qreal durationMs_ = 0.0;
  qint64 lastTickMs_ = 0;
};

}  // namespace muffin
