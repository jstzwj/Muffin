#include "editor/FocusAnimator.h"

#include <QElapsedTimer>
#include <QTimer>

namespace muffin {

FocusAnimator::FocusAnimator(QObject* parent) : QObject(parent) {
  clock_.start();
  timer_ = new QTimer(this);
  timer_->setInterval(16);  // ~60 fps
  connect(timer_, &QTimer::timeout, this, &FocusAnimator::onTick);
}

bool FocusAnimator::isAnimating() const {
  return timer_ != nullptr && timer_->isActive();
}

void FocusAnimator::setFocused(NodeId blockId, qreal durationMs) {
  // Already fully focused on this block (or fading it in) — nothing to do.
  if (blockId == blockId_ && target_ == 1.0) { return; }

  // Erase the previously-animated block's focus effect when focus moves elsewhere.
  if (repaintBlock && blockId_.isValid() && blockId_ != blockId) {
    repaintBlock(blockId_);
  }

  blockId_ = blockId;
  target_ = blockId.isValid() ? 1.0 : 0.0;
  durationMs_ = durationMs;
  if (target_ == 1.0) { phase_ = 0.0; }  // fade in from cold

  if (durationMs_ <= 0.0) {
    phase_ = target_;  // snap
    if (repaintBlock && blockId_.isValid()) { repaintBlock(blockId_); }
    if (target_ == 0.0) { blockId_ = NodeId(); }
    return;
  }
  lastTickMs_ = clock_.elapsed();
  if (!timer_->isActive()) { timer_->start(); }
  if (repaintBlock && blockId_.isValid()) { repaintBlock(blockId_); }
}

void FocusAnimator::onTick() {
  if (!blockId_.isValid()) { timer_->stop(); return; }
  const qint64 now = clock_.elapsed();
  const qreal dt = static_cast<qreal>(now - lastTickMs_);
  lastTickMs_ = now;
  const qreal step = durationMs_ > 0.0 ? dt / durationMs_ : 1.0;
  if (target_ == 1.0) {
    phase_ = qMin(qreal(1.0), phase_ + step);
  } else {
    phase_ = qMax(qreal(0.0), phase_ - step);
  }
  if (repaintBlock && blockId_.isValid()) { repaintBlock(blockId_); }
  if (phase_ == target_) {
    if (target_ == 0.0) { blockId_ = NodeId(); }
    timer_->stop();
  }
}

}  // namespace muffin
