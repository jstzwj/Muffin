#pragma once

#include <QRectF>

namespace muffin::mermaid::flowchart {

struct BlinkFloatPoint {
  float x = 0.0f;
  float y = 0.0f;
};

struct BlinkPathBounds {
  bool empty = true;
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  void add(BlinkFloatPoint point);
  QRectF rect() const;
};

// Blink normalizes SVG endpoint arcs to float Skia rational conics. Both the
// float endpoint accumulation and the conic extrema affect SVG getBBox().
void addBlinkSvgArcBounds(BlinkPathBounds& bounds, BlinkFloatPoint start,
                          BlinkFloatPoint end, qreal radiusX, qreal radiusY,
                          qreal rotationDegrees, bool large, bool sweep);

}  // namespace muffin::mermaid::flowchart
