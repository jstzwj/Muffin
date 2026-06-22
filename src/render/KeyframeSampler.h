#pragma once

#include "theme/ThemeDefinition.h"

#include <QColor>

namespace muffin {

// Interpolated values for one host at one animation phase (0..1, already eased
// + direction-applied by the driver). Each flag says whether the keyframes
// animate that property; absent → the painter uses the theme's static value.
// Supported: opacity, box-shadow glow (colour + blur), transform: scale().
// Deferred: background-position/size, text-shadow, translate/rotate.
struct AnimatedSample {
  bool hasOpacity = false;
  qreal opacity = 1.0;
  bool hasGlow = false;
  QColor glowColor;
  qreal glowBlur = 0.0;
  bool hasScale = false;
  qreal scale = 1.0;
};

namespace KeyframeSampler {

// Sample `kf` at `phase` (0..1), interpolating the supported properties between
// the surrounding stops. Stop values are pre-resolved at map time (var() gone),
// so no variable table is needed. Pure — no timing/iteration/easing (that's the
// driver's job); this just interpolates.
AnimatedSample sampleAtPhase(const KeyframesDef& kf, qreal phase);

}  // namespace KeyframeSampler

}  // namespace muffin
