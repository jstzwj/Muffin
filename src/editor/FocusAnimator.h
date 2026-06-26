#pragma once

#include "document/NodeId.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QTimer;

namespace muffin {

// Animates a single block's :focus phase 0..1, idle-gated (timer runs only while
// a fade is in flight). Mirrors HoverAnimator: phase 1 = fully focused, 0 = not.
// A separate animator from HoverAnimator because hover and focus are orthogonal —
// a block can be both under the cursor AND contain the caret, each animating
// independently. The owner-supplied `repaintBlock` fires on each tick for the
// animated block.
//
// Focus is "sticky": the caret block stays at phase 1 until the caret moves to a
// different top-level block, at which point the old block fades out and the new
// one fades in.
class FocusAnimator : public QObject {
  Q_OBJECT
public:
  explicit FocusAnimator(QObject* parent = nullptr);

  // Begin focus on `blockId` (fade 0→1) or end it (blockId invalid → fade →0).
  // `durationMs` is the fade time; 0 → snap to target with no animation.
  void setFocused(NodeId blockId, qreal durationMs);

  NodeId animatedBlockId() const { return blockId_; }
  qreal phase() const { return phase_; }
  bool isAnimating() const;

  // Owner installs this; called with the block whose rect needs repainting.
  std::function<void(NodeId)> repaintBlock;

private slots:
  void onTick();

private:
  QTimer* timer_ = nullptr;
  QElapsedTimer clock_;  // value member: QElapsedTimer is not a QObject, so a `new`-ed pointer would leak
  NodeId blockId_;
  qreal phase_ = 0.0;
  qreal target_ = 0.0;
  qreal durationMs_ = 0.0;
  qint64 lastTickMs_ = 0;
};

}  // namespace muffin
