#pragma once

#include <QImage>

namespace muffin {

// In-place separable box blur on a Format_ARGB32_Premultiplied QImage. `radius`
// is the half-window in px (full window = 2·radius+1). `passes` ≈ 3 approximates a
// Gaussian (a box blur repeated N times tends to Gaussian). No-op for radius<=0.
// Premultiplied alpha is required so channels average correctly across transparency.
void boxBlur(QImage& image, int radius, int passes = 3);

// Convenience: return a blurred copy of `src` (any format; converted to
// premultiplied, blurred, returned premultiplied). radius<=0 returns a copy.
// Used by text-shadow and filter rendering.
QImage blurred(const QImage& src, qreal radius);

}  // namespace muffin
