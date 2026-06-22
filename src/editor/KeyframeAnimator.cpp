#include "editor/KeyframeAnimator.h"

#include "theme/RenderTheme.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QtMath>

#include <cmath>

namespace muffin {

KeyframeAnimator::KeyframeAnimator(QObject* parent) : QObject(parent) {
  clock_ = new QElapsedTimer();
  clock_->start();
  timer_ = new QTimer(this);
  timer_->setInterval(16);  // ~60 fps
  connect(timer_, &QTimer::timeout, this, &KeyframeAnimator::onTick);
}

void KeyframeAnimator::setTheme(const RenderTheme& theme) {
  anims_ = theme.decorations().animations;
  keyframes_ = theme.decorations().keyframes;
  samples_.clear();
  activeHosts_.clear();
  lastVisible_.clear();
  if (timer_->isActive()) { timer_->stop(); }
}

const AnimationDef* KeyframeAnimator::findAnim(const QString& host) const {
  for (const AnimationDef& a : anims_) {
    if (a.host == host) { return &a; }
  }
  return nullptr;
}

const KeyframesDef* KeyframeAnimator::findKeyframes(const QString& name) const {
  for (const KeyframesDef& k : keyframes_) {
    if (k.name == name) { return &k; }
  }
  return nullptr;
}

qreal KeyframeAnimator::ease(qreal x, const QString& e) {
  if (e.isEmpty() || e == QStringLiteral("linear")) { return x; }
  if (e == QStringLiteral("ease") || e == QStringLiteral("ease-out") || e.startsWith(QStringLiteral("cubic-bezier"))) {
    return 1.0 - std::pow(1.0 - x, 3.0);  // ease-out-cubic (CSS `ease` ≈ cubic-bezier(.25,.1,.25,1))
  }
  if (e == QStringLiteral("ease-in")) { return x * x * x; }
  if (e == QStringLiteral("ease-in-out")) { return x < 0.5 ? 4.0 * x * x * x : 1.0 - std::pow(-2.0 * x + 2.0, 3.0) / 2.0; }
  return x;
}

void KeyframeAnimator::setVisibleHosts(const QSet<QString>& hosts) {
  if (hosts == lastVisible_) { return; }
  lastVisible_ = hosts;
  // Keep only visible hosts that have an INFINITE animation with matching keyframes.
  QSet<QString> next;
  for (const QString& host : hosts) {
    const AnimationDef* a = findAnim(host);
    if (a && a->iterations == -1 && findKeyframes(a->name)) { next.insert(host); }
  }
  activeHosts_ = next;
  // Drop samples for hosts no longer active.
  for (auto it = samples_.begin(); it != samples_.end();) {
    if (!activeHosts_.contains(it.key())) { it = samples_.erase(it); } else { ++it; }
  }
  if (!activeHosts_.isEmpty()) {
    if (!timer_->isActive()) { clock_->restart(); timer_->start(); }
  } else if (timer_->isActive()) {
    timer_->stop();
  }
}

const AnimatedSample* KeyframeAnimator::sampleFor(const QString& host) const {
  return samples_.constFind(host) != samples_.constEnd() ? &samples_.constFind(host).value() : nullptr;
}

void KeyframeAnimator::onTick() {
  if (activeHosts_.isEmpty()) { timer_->stop(); return; }
  const qint64 elapsed = clock_->elapsed();
  for (const QString& host : activeHosts_) {
    const AnimationDef* a = findAnim(host);
    if (!a) { continue; }
    const KeyframesDef* kf = findKeyframes(a->name);
    if (!kf) { continue; }
    const qint64 t = elapsed - static_cast<qint64>(a->delayMs);
    if (t < 0) { continue; }
    const qreal dur = a->durationMs > 0.0 ? a->durationMs : 1.0;
    const qreal local = std::fmod(static_cast<qreal>(t) / dur, 1.0);
    const qint64 iterIdx = static_cast<qint64>(static_cast<qreal>(t) / dur);
    qreal raw = local;
    switch (a->direction) {
      case AnimationDef::Direction::Reverse: raw = 1.0 - local; break;
      case AnimationDef::Direction::Alternate: raw = (iterIdx % 2 == 0) ? local : (1.0 - local); break;
      case AnimationDef::Direction::AlternateReverse: raw = (iterIdx % 2 == 0) ? (1.0 - local) : local; break;
      default: break;
    }
    samples_[host] = KeyframeSampler::sampleAtPhase(*kf, ease(raw, a->easing));
  }
  if (repaintAnimated) { repaintAnimated(); }
}

}  // namespace muffin
