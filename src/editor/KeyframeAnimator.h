#pragma once

#include "render/KeyframeSampler.h"
#include "theme/ThemeDefinition.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class QTimer;

namespace muffin {

class RenderTheme;

// Drives continuous `@keyframes` animations: an idle-gated 60 fps timer that,
// each tick, advances every VISIBLE host's animation (delay/iteration/direction/
// easing), samples the keyframe, and stores the result for the painter to read.
// Idle-gated: the timer runs only while at least one visible host has an
// INFINITE animation with a matching @keyframes — a static document costs nothing.
//
// MVP: infinite iterations only (the always-on decorative case — throb/pulse/
// glow). Finite/hover/state-triggered animations need a start trigger and are
// deferred. Animated properties: opacity, box-shadow glow, transform: scale()
// (see KeyframeSampler).
class KeyframeAnimator : public QObject {
  Q_OBJECT
public:
  explicit KeyframeAnimator(QObject* parent = nullptr);

  // Capture the theme's animations + keyframes (call on theme change).
  void setTheme(const RenderTheme& theme);
  // True when the theme declares at least one animation with a matching keyframes.
  bool hasAnimations() const { return !anims_.empty(); }

  // The set of hosts currently visible (any block host). The driver filters to
  // those with an infinite animation + matching keyframes, and starts/stops the
  // timer accordingly. Idempotent if the set is unchanged.
  void setVisibleHosts(const QSet<QString>& hosts);

  // Latest animated sample for a host, or nullptr when it isn't animating.
  const AnimatedSample* sampleFor(const QString& host) const;

  // Owner installs this; fired each tick after samples update.
  std::function<void()> repaintAnimated;

private slots:
  void onTick();

private:
  const AnimationDef* findAnim(const QString& host) const;
  const KeyframesDef* findKeyframes(const QString& name) const;
  static qreal ease(qreal x, const QString& e);

  QTimer* timer_ = nullptr;
  QElapsedTimer clock_;  // value member: QElapsedTimer is not a QObject, so a `new`-ed pointer would leak
  std::vector<AnimationDef> anims_;
  std::vector<KeyframesDef> keyframes_;
  QHash<QString, AnimatedSample> samples_;
  QSet<QString> activeHosts_;   // visible hosts with an infinite animation
  QSet<QString> lastVisible_;   // for idempotent setVisibleHosts
};

}  // namespace muffin
