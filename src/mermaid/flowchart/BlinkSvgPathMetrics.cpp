#include "mermaid/flowchart/BlinkSvgPathMetrics.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace muffin::mermaid::flowchart {
namespace {

struct FloatConic {
  BlinkFloatPoint p0;
  BlinkFloatPoint p1;
  BlinkFloatPoint p2;
  float weight = 1.0f;
};

bool validUnitDivide(float numerator, float denominator, float* ratio) {
  if (numerator < 0.0f) {
    numerator = -numerator;
    denominator = -denominator;
  }
  if (denominator == 0.0f || numerator == 0.0f || numerator >= denominator)
    return false;
  const float value = numerator / denominator;
  if (std::isnan(value) || value == 0.0f) return false;
  *ratio = value;
  return true;
}

QVector<float> unitQuadraticRoots(float a, float b, float c) {
  QVector<float> roots;
  if (a == 0.0f) {
    float value = 0.0f;
    if (validUnitDivide(-c, b, &value)) roots.append(value);
    return roots;
  }
  const double discriminant = double(b) * b - 4.0 * double(a) * c;
  if (discriminant < 0.0) return roots;
  const float root = float(std::sqrt(discriminant));
  if (!std::isfinite(root)) return roots;
  const float q = b < 0.0f ? -(b - root) / 2.0f : -(b + root) / 2.0f;
  float value = 0.0f;
  if (validUnitDivide(q, a, &value)) roots.append(value);
  if (validUnitDivide(c, q, &value)) roots.append(value);
  if (roots.size() == 2) {
    if (roots[0] > roots[1])
      std::swap(roots[0], roots[1]);
    else if (roots[0] == roots[1])
      roots.removeLast();
  }
  return roots;
}

BlinkFloatPoint conicEval(const FloatConic& conic, float t) {
  const auto component = [&](float p0, float p1, float p2) {
    const float p1w = p1 * conic.weight;
    const float a = p2 - 2.0f * p1w + p0;
    const float b = 2.0f * (p1w - p0);
    const float numerator = (a * t + b) * t + p0;
    const float denominatorB = 2.0f * (conic.weight - 1.0f);
    const float denominator =
        ((-denominatorB) * t + denominatorB) * t + 1.0f;
    return numerator / denominator;
  };
  return {component(conic.p0.x, conic.p1.x, conic.p2.x),
          component(conic.p0.y, conic.p1.y, conic.p2.y)};
}

void addConicBounds(BlinkPathBounds& bounds, const FloatConic& conic) {
  bounds.add(conic.p0);
  bounds.add(conic.p2);
  const auto extrema = [&](float p0, float p1, float p2) {
    const float p20 = p2 - p0;
    const float p10 = p1 - p0;
    const float weighted = conic.weight * p10;
    return unitQuadraticRoots(conic.weight * p20 - p20,
                              p20 - 2.0f * weighted, weighted);
  };
  const QVector<float> xs = extrema(conic.p0.x, conic.p1.x, conic.p2.x);
  if (!xs.isEmpty()) bounds.add(conicEval(conic, xs.first()));
  const QVector<float> ys = extrema(conic.p0.y, conic.p1.y, conic.p2.y);
  if (!ys.isEmpty()) bounds.add(conicEval(conic, ys.first()));
}

}  // namespace

void BlinkPathBounds::add(BlinkFloatPoint point) {
  if (empty) {
    left = right = point.x;
    top = bottom = point.y;
    empty = false;
    return;
  }
  left = std::min(left, point.x);
  top = std::min(top, point.y);
  right = std::max(right, point.x);
  bottom = std::max(bottom, point.y);
}

QRectF BlinkPathBounds::rect() const {
  return empty ? QRectF() : QRectF(left, top, right - left, bottom - top);
}

void addBlinkSvgArcBounds(BlinkPathBounds& bounds, BlinkFloatPoint start,
                          BlinkFloatPoint end, qreal radiusX, qreal radiusY,
                          qreal rotationDegrees, bool large, bool sweep) {
  float rx = std::abs(float(radiusX));
  float ry = std::abs(float(radiusY));
  if (rx == 0.0f || ry == 0.0f ||
      (start.x == end.x && start.y == end.y)) {
    bounds.add(start);
    bounds.add(end);
    return;
  }

  constexpr float pi = std::numbers::pi_v<float>;
  const float radians = float(rotationDegrees) * (pi / 180.0f);
  const float cosine = std::cos(-radians);
  const float sine = std::sin(-radians);
  const BlinkFloatPoint middle{(start.x - end.x) * 0.5f,
                               (start.y - end.y) * 0.5f};
  const BlinkFloatPoint transformedMiddle{
      cosine * middle.x - sine * middle.y,
      sine * middle.x + cosine * middle.y};
  const float squareRx = rx * rx;
  const float squareRy = ry * ry;
  const float squareX = transformedMiddle.x * transformedMiddle.x;
  const float squareY = transformedMiddle.y * transformedMiddle.y;
  const float radiiScale = squareX / squareRx + squareY / squareRy;
  if (radiiScale > 1.0f) {
    const float scale = std::sqrt(radiiScale);
    rx *= scale;
    ry *= scale;
  }

  const float c = std::cos(-radians);
  const float s = std::sin(-radians);
  const float normA = (1.0f / rx) * c;
  const float normC = (1.0f / rx) * -s;
  const float normB = (1.0f / ry) * s;
  const float normD = (1.0f / ry) * c;
  const auto normalize = [&](BlinkFloatPoint point) {
    return BlinkFloatPoint{normA * point.x + normC * point.y,
                           normB * point.x + normD * point.y};
  };
  BlinkFloatPoint unit0 = normalize(start);
  BlinkFloatPoint unit1 = normalize(end);
  BlinkFloatPoint delta{unit1.x - unit0.x, unit1.y - unit0.y};
  const float d = delta.x * delta.x + delta.y * delta.y;
  const float factorSquared = std::max(1.0f / d - 0.25f, 0.0f);
  float factor = std::sqrt(factorSquared);
  if (sweep == large) factor = -factor;
  delta.x *= factor;
  delta.y *= factor;
  BlinkFloatPoint center{(unit0.x + unit1.x) * 0.5f - delta.y,
                         (unit0.y + unit1.y) * 0.5f + delta.x};
  unit0.x -= center.x;
  unit0.y -= center.y;
  unit1.x -= center.x;
  unit1.y -= center.y;

  float theta1 = std::atan2(unit0.y, unit0.x);
  const float theta2 = std::atan2(unit1.y, unit1.x);
  float thetaArc = theta2 - theta1;
  if (thetaArc < 0.0f && sweep)
    thetaArc += pi * 2.0f;
  else if (thetaArc > 0.0f && !sweep)
    thetaArc -= pi * 2.0f;
  if (std::abs(thetaArc) < pi / 1000000.0f) {
    bounds.add(start);
    bounds.add(end);
    return;
  }

  const float mapCos = std::cos(radians);
  const float mapSin = std::sin(radians);
  const float mapA = mapCos * rx;
  const float mapC = -mapSin * ry;
  const float mapB = mapSin * rx;
  const float mapD = mapCos * ry;
  const auto map = [&](BlinkFloatPoint point) {
    return BlinkFloatPoint{mapA * point.x + mapC * point.y,
                           mapB * point.x + mapD * point.y};
  };
  const int segments =
      int(std::ceil(std::abs(thetaArc / (2.0f * pi / 3.0f))));
  const float thetaWidth = thetaArc / segments;
  const float tangent = std::tan(0.5f * thetaWidth);
  const float weight =
      std::sqrt(0.5f + std::cos(thetaWidth) * 0.5f);
  float startTheta = theta1;
  BlinkFloatPoint conicStart = start;
  for (int index = 0; index < segments; ++index) {
    const float endTheta = startTheta + thetaWidth;
    const float sinEnd = std::sin(endTheta);
    const float cosEnd = std::cos(endTheta);
    BlinkFloatPoint target{cosEnd + center.x, sinEnd + center.y};
    BlinkFloatPoint control{target.x + tangent * sinEnd,
                            target.y - tangent * cosEnd};
    control = map(control);
    target = map(target);
    if (index + 1 == segments) target = end;
    addConicBounds(bounds, {conicStart, control, target, weight});
    conicStart = target;
    startTheta = endTheta;
  }
}

}  // namespace muffin::mermaid::flowchart
