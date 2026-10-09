#include "render/GradientPainter.h"

#include <QConicalGradient>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::GradientPainter {
namespace {
QColor interpolate(QColor a, QColor b, qreal t) {
  const qreal alpha = a.alphaF() * (1 - t) + b.alphaF() * t;
  if (alpha <= 0) return Qt::transparent;
  const auto channel = [&](qreal x, qreal y) { return (x * a.alphaF() * (1 - t) + y * b.alphaF() * t) / alpha; };
  return QColor::fromRgbF(channel(a.redF(), b.redF()), channel(a.greenF(), b.greenF()), channel(a.blueF(), b.blueF()), alpha);
}

QGradientStops usedStops(const GradientSpec& spec, qreal referenceLength, qreal scale) {
  std::vector<qreal> positions;
  for (const auto& stop : spec.stops)
    positions.push_back(stop.position.status == CssLengthStatus::Valid
                            ? stop.position.used(referenceLength / scale) * scale / referenceLength
                            : std::numeric_limits<qreal>::quiet_NaN());
  if (std::isnan(positions.front())) positions.front() = 0;
  if (std::isnan(positions.back())) positions.back() = 1;
  // CSS color-stop fixup happens after lengths and percentages share an axis.
  qreal previous = -std::numeric_limits<qreal>::infinity();
  for (auto& position : positions)
    if (!std::isnan(position)) { position = qMax(previous, position); previous = position; }
  for (size_t first = 0; first < positions.size() - 1;) {
    size_t last = first + 1;
    while (std::isnan(positions[last])) ++last;
    for (size_t i = first + 1; i < last; ++i)
      positions[i] = positions[first] + (positions[last] - positions[first]) * qreal(i - first) / qreal(last - first);
    first = last;
  }
  const auto colorAt = [&](qreal position) {
    if (position < positions.front()) return spec.stops.front().color;
    for (size_t i = 1; i < positions.size(); ++i)
      if (position < positions[i])
        return interpolate(spec.stops[i - 1].color, spec.stops[i].color,
                           (position - positions[i - 1]) / (positions[i] - positions[i - 1]));
    return spec.stops.back().color;
  };
  QGradientStops stops{{0, colorAt(0)}};
  for (size_t i = 0; i < positions.size();) {
    size_t last = i;
    while (last + 1 < positions.size() && positions[last + 1] == positions[i]) ++last;
    const auto position = positions[i];
    if (position > 0 && position <= 1) {
      // Qt replaces duplicate positions. Separate a CSS hard stop by a tiny
      // interval, below a device pixel, instead of losing its left-hand color.
      if (last > i && spec.stops[i].color != spec.stops[last].color) {
        const qreal left = qMax(stops.back().first, position - 0.00001);
        if (left > stops.back().first) stops.push_back({left, spec.stops[i].color});
      }
      stops.push_back({position, spec.stops[last].color});
    }
    i = last + 1;
  }
  if (stops.back().first < 1) stops.push_back({1, colorAt(1)});
  return stops;
}

QSizeF radialRadii(const GradientSpec& spec, const QRectF& target, QPointF center, qreal scale) {
  const qreal nearX = qMin(qAbs(center.x() - target.left()), qAbs(target.right() - center.x()));
  const qreal nearY = qMin(qAbs(center.y() - target.top()), qAbs(target.bottom() - center.y()));
  const qreal farX = qMax(qAbs(center.x() - target.left()), qAbs(target.right() - center.x()));
  const qreal farY = qMax(qAbs(center.y() - target.top()), qAbs(target.bottom() - center.y()));
  if (spec.radialExtent == GradientSpec::RadialExtent::Explicit)
    return {qMax<qreal>(.00001, spec.radialRadiusX.used(target.width() / scale) * scale),
            qMax<qreal>(.00001, spec.radialRadiusY.used(target.height() / scale) * scale)};
  const bool closest = spec.radialExtent == GradientSpec::RadialExtent::ClosestSide ||
                       spec.radialExtent == GradientSpec::RadialExtent::ClosestCorner;
  qreal x = closest ? nearX : farX, y = closest ? nearY : farY;
  const bool corner = spec.radialExtent == GradientSpec::RadialExtent::ClosestCorner ||
                      spec.radialExtent == GradientSpec::RadialExtent::FarthestCorner;
  if (spec.radialShape == GradientSpec::RadialShape::Circle) {
    const auto radius = corner ? std::hypot(x, y) : closest ? qMin(x, y) : qMax(x, y);
    x = y = radius;
  } else if (corner) {
    x *= qSqrt(2.0); y *= qSqrt(2.0);
  }
  return {qMax<qreal>(.00001, x), qMax<qreal>(.00001, y)};
}
}  // namespace

bool isGradient(const GradientSpec& spec) {
  return spec.kind != GradientSpec::Kind::None && spec.stops.size() >= 2;
}

QBrush makeBrush(const GradientSpec& spec, const QRectF& target, qreal lengthScale) {
  if (!isGradient(spec)) return {};
  lengthScale = qMax<qreal>(.00001, lengthScale);
  if (spec.kind == GradientSpec::Kind::Linear) {
    const qreal rad = qDegreesToRadians(spec.angleDeg);
    const QPointF direction(qSin(rad), -qCos(rad));
    const qreal projection = qAbs(direction.x()) * target.width() / 2 + qAbs(direction.y()) * target.height() / 2;
    QLinearGradient gradient(target.center() - direction * projection, target.center() + direction * projection);
    gradient.setStops(usedStops(spec, qMax<qreal>(.00001, 2 * projection), lengthScale));
    return QBrush(gradient);
  }
  if (spec.kind == GradientSpec::Kind::Conic) {
    const QPointF center(target.left() + spec.conicCenter.x() * target.width(), target.top() + spec.conicCenter.y() * target.height());
    QConicalGradient gradient(center, 90 - spec.conicStartDeg);
    const auto cssStops = usedStops(spec, 1, lengthScale);
    QGradientStops stops;
    for (auto i = cssStops.crbegin(); i != cssStops.crend(); ++i) stops.push_back({1 - i->first, i->second});
    gradient.setStops(stops);
    return QBrush(gradient);
  }
  const QPointF center(target.left() + spec.radialCenter.x() * target.width(), target.top() + spec.radialCenter.y() * target.height());
  const auto radii = radialRadii(spec, target, center, lengthScale);
  QRadialGradient gradient(center, radii.width());
  gradient.setStops(usedStops(spec, radii.width(), lengthScale));
  QBrush brush(gradient);
  QTransform ellipse;
  ellipse.translate(center.x(), center.y());
  ellipse.scale(1, radii.height() / radii.width());
  ellipse.translate(-center.x(), -center.y());
  brush.setTransform(ellipse);
  return brush;
}

}  // namespace muffin::GradientPainter
