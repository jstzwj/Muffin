#pragma once

#include "mermaid/MermaidPaintOptions.h"

#include <QImage>

class QPainter;

namespace muffin::mermaid::er {

struct ErScene;

// Paints the scene with QPainter. Honors MermaidPaintOptions (visible-rect
// culling using each primitive's bounds, paint-stats accounting). Antialiasing
// is always enabled; crow's-foot markers are drawn rotated to the edge angle.
void paintErScene(const ErScene& scene, QPainter& painter,
                  const MermaidPaintOptions& options = {});

// Rasterizes the scene to a device-pixel-ratio-aware QImage with `padding`
// device pixels of margin. Used by the render cache for the "er" dispatch.
QImage renderErSceneToImage(const ErScene& scene, qreal dpr = 1.0, qreal padding = 16.0);

}  // namespace muffin::mermaid::er
