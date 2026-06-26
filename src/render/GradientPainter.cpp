#include "render/GradientPainter.h"

#include <QConicalGradient>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QRectF>
#include <QtMath>

namespace muffin {
namespace GradientPainter {

bool isGradient(const GradientSpec& spec) {
  return spec.kind != GradientSpec::Kind::None && !spec.stops.empty();
}

QBrush makeBrush(const GradientSpec& spec, const QRectF& target) {
  if (!isGradient(spec)) { return QBrush(); }
  const auto setStops = [&spec](QGradient& g) {
    for (const GradientStop& s : spec.stops) {
      g.setColorAt(qBound(0.0, s.position, 1.0), s.color);
    }
  };
  if (spec.kind == GradientSpec::Kind::Linear) {
    // CSS angle convention: 0 = to top, clockwise. Direction unit vector:
    // (sin θ, -cos θ) in screen coords (y grows downward).
    const qreal rad = qDegreesToRadians(spec.angleDeg);
    const qreal dx = qSin(rad);
    const qreal dy = -qCos(rad);
    // The gradient line spans the rect's projection onto the direction: from
    // center, extend by |dx|·halfW + |dy|·halfH each way (reaches the corners).
    const QPointF c = target.center();
    const qreal halfW = target.width() / 2.0;
    const qreal halfH = target.height() / 2.0;
    const qreal proj = qFabs(dx) * halfW + qFabs(dy) * halfH;
    QLinearGradient g(c - QPointF(dx, dy) * proj, c + QPointF(dx, dy) * proj);
    setStops(g);
    return QBrush(g);
  }
  if (spec.kind == GradientSpec::Kind::Conic) {
    // CSS conic-gradient: angle 0 = 12 o'clock, sweeping CLOCKWISE. QConicalGradient
    // uses 3 o'clock origin sweeping COUNTER-clockwise, in degrees. Convert:
    // qtAngle = 90 - cssAngle, negated for the sweep direction → cssStart - 90.
    const QPointF center(target.left() + spec.conicCenter.x() * target.width(),
                         target.top() + spec.conicCenter.y() * target.height());
    QConicalGradient g(center, spec.conicStartDeg - 90.0);
    setStops(g);
    return QBrush(g);
  }
  // Radial.
  const QPointF center(target.left() + spec.radialCenter.x() * target.width(),
                       target.top() + spec.radialCenter.y() * target.height());
  const qreal radius = spec.radialRadius * qMax(target.width(), target.height());
  QRadialGradient g(center, radius > 0.0 ? radius : 1.0);
  setStops(g);
  return QBrush(g);
}

}  // namespace GradientPainter
}  // namespace muffin
