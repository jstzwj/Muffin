#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"

#include <QPointF>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace muffin::mermaid::flowchart {
namespace {

// --- point generators (1:1 ports of mermaid's generateFullSineWavePoints,
// generateCirclePoints[2/3/4], generateArcPoints, and the SVG `a` arc sampler
// used by bang/cloud) ---

// generateFullSineWavePoints(x1,y1,x2,y2,amplitude,numCycles): 51 points (0..50).
// y = midY + amplitude * sin(freq * (x - x1)). Used by every wave-edged shape.
QVector<QPointF> sineWavePoints(qreal x1, qreal y1, qreal x2, qreal y2,
                                qreal amplitude, qreal numCycles) {
  QVector<QPointF> pts;
  const int steps = 50;
  const qreal deltaX = x2 - x1;
  const qreal deltaY = y2 - y1;
  const qreal cycleLength = deltaX / numCycles;
  const qreal frequency = 2.0 * M_PI / cycleLength;
  const qreal midY = y1 + deltaY / 2.0;
  for (int i = 0; i <= steps; ++i) {
    const qreal t = qreal(i) / qreal(steps);
    const qreal x = x1 + t * deltaX;
    const qreal y = midY + amplitude * std::sin(frequency * (x - x1));
    pts.append(QPointF(x, y));
  }
  return pts;
}

// generateCirclePoints[2/3/4](cx,cy,r,n,startDeg,endDeg): n points along a
// circular arc. The upstream variants differ only in whether they negate the
// returned (x,y) — 2/4 negate, 3 does not. `negate` selects.
QVector<QPointF> circleArcPoints(qreal cx, qreal cy, qreal r, int n,
                                 qreal startDeg, qreal endDeg, bool negate) {
  QVector<QPointF> pts;
  const qreal start = startDeg * M_PI / 180.0;
  const qreal end = endDeg * M_PI / 180.0;
  const qreal range = end - start;
  const qreal step = range / qreal(n - 1);
  for (int i = 0; i < n; ++i) {
    const qreal a = start + i * step;
    const qreal x = cx + r * std::cos(a);
    const qreal y = cy + r * std::sin(a);
    pts.append(negate ? QPointF(-x, -y) : QPointF(x, y));
  }
  return pts;
}

// generateArcPoints(x1,y1,x2,y2,rx,ry,clockwise): a 20-point elliptical arc
// between two points (bowTieRect's pinched sides). Ported verbatim.
QVector<QPointF> ellipseArcPoints(qreal x1, qreal y1, qreal x2, qreal y2,
                                  qreal rx, qreal ry, bool clockwise) {
  const int numPoints = 20;
  const qreal midX = (x1 + x2) / 2.0;
  const qreal midY = (y1 + y2) / 2.0;
  const qreal angle = std::atan2(y2 - y1, x2 - x1);
  const qreal dx = (x2 - x1) / 2.0;
  const qreal dy = (y2 - y1) / 2.0;
  const qreal transformedX = dx / rx;
  const qreal transformedY = dy / ry;
  const qreal distance = std::sqrt(transformedX * transformedX + transformedY * transformedY);
  // upstream throws if distance > 1; the shapes that reach here (bowTieRect)
  // guarantee distance == 1, so this branch is never taken.
  const qreal scaledCenterDistance = std::sqrt(1.0 - distance * distance);
  const qreal cx = midX + scaledCenterDistance * ry * std::sin(angle) * (clockwise ? -1.0 : 1.0);
  const qreal cy = midY - scaledCenterDistance * rx * std::cos(angle) * (clockwise ? -1.0 : 1.0);
  const qreal startAngle = std::atan2((y1 - cy) / ry, (x1 - cx) / rx);
  const qreal endAngle = std::atan2((y2 - cy) / ry, (x2 - cx) / rx);
  qreal angleRange = endAngle - startAngle;
  if (clockwise && angleRange < 0.0) angleRange += 2.0 * M_PI;
  if (!clockwise && angleRange > 0.0) angleRange -= 2.0 * M_PI;
  QVector<QPointF> pts;
  for (int i = 0; i < numPoints; ++i) {
    const qreal t = qreal(i) / qreal(numPoints - 1);
    const qreal a = startAngle + t * angleRange;
    pts.append(QPointF(cx + rx * std::cos(a), cy + ry * std::sin(a)));
  }
  return pts;
}

// Append an SVG `a rx,ry rot large sweep dx,dy` arc (x-axis-rotation ~0 for
// bang/cloud, which is all mermaid uses). Samples n points (excluding the
// start point, which the caller already holds as the previous arc's end).
// Implements the SVG endpoint->center parameterisation.
void appendEllipticalArc(QVector<QPointF>& pts, qreal x1, qreal y1, qreal x2, qreal y2,
                         qreal rx, qreal ry, bool large, bool sweep, int n) {
  const qreal X = (x1 - x2) / 2.0;
  const qreal Y = (y1 - y2) / 2.0;
  qreal Rx = rx, Ry = ry;
  qreal lambda = (X * X) / (Rx * Rx) + (Y * Y) / (Ry * Ry);
  if (lambda > 1.0) {  // radii too small: scale up (SVG spec). Mermaid shouldn't hit.
    const qreal s = std::sqrt(lambda);
    Rx *= s; Ry *= s;
    lambda = 1.0;
  }
  const qreal sign = (large != sweep) ? 1.0 : -1.0;
  const qreal under = std::max(0.0, 1.0 - lambda);
  const qreal denom = lambda > 0.0 ? lambda : 1.0;
  const qreal coef = sign * std::sqrt(under / denom);
  const qreal cxp = coef * (Rx * Y / Ry);
  const qreal cyp = coef * (-Ry * X / Rx);
  const qreal cx = cxp + (x1 + x2) / 2.0;
  const qreal cy = cyp + (y1 + y2) / 2.0;
  qreal theta1 = std::atan2((y1 - cy) / Ry, (x1 - cx) / Rx);
  const qreal theta2 = std::atan2((y2 - cy) / Ry, (x2 - cx) / Rx);
  qreal dtheta = theta2 - theta1;
  if (!sweep && dtheta > 0.0) dtheta -= 2.0 * M_PI;
  if (sweep && dtheta < 0.0) dtheta += 2.0 * M_PI;
  if (large) {
    if (sweep && dtheta < M_PI) dtheta += 2.0 * M_PI;
    if (!sweep && dtheta > -M_PI) dtheta -= 2.0 * M_PI;
  }
  for (int i = 1; i <= n; ++i) {
    const qreal t = qreal(i) / n;
    const qreal a = theta1 + t * dtheta;
    pts.append(QPointF(cx + Rx * std::cos(a), cy + Ry * std::sin(a)));
  }
}

// Shift points so their bbox is centred on the origin. The centred polygon's
// bbox then equals the node bbox, so intersectPolygon (which aligns the points'
// bbox min-corner to the node's top-left) places it correctly, and the
// silhouette test (which draws points centred at the bounds centre) matches
// mermaid's golden (whose viewBox compensates the handler's translate transform).
QVector<QPointF> centreOnBbox(QVector<QPointF> pts) {
  if (pts.isEmpty()) return pts;
  qreal minX = std::numeric_limits<qreal>::infinity();
  qreal maxX = -std::numeric_limits<qreal>::infinity();
  qreal minY = std::numeric_limits<qreal>::infinity();
  qreal maxY = -std::numeric_limits<qreal>::infinity();
  for (const QPointF& p : pts) {
    minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
    minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
  }
  const qreal cx = (minX + maxX) / 2.0;
  const qreal cy = (minY + maxY) / 2.0;
  for (QPointF& p : pts) { p.rx() -= cx; p.ry() -= cy; }
  return pts;
}

QVector<QPointF> translateFromPathOrigin(QVector<QPointF> pts,
                                         qreal width, qreal height) {
  for (QPointF& point : pts) {
    point.rx() -= width / 2.0;
    point.ry() -= height / 2.0;
  }
  return pts;
}

QSizeF bboxOf(const QVector<QPointF>& pts) {
  qreal minX = std::numeric_limits<qreal>::infinity();
  qreal maxX = -std::numeric_limits<qreal>::infinity();
  qreal minY = std::numeric_limits<qreal>::infinity();
  qreal maxY = -std::numeric_limits<qreal>::infinity();
  for (const QPointF& p : pts) {
    minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
    minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
  }
  return QSizeF(maxX - minX, maxY - minY);
}

// bang outline: the explosion/burst path from bang.ts, sampled into a polyline.
// M0 0 then 14 `a` arcs (x-axis-rotation ignored — all arcs are circular or
// near-circular), closing back to (0,0) via the polygon's implicit Z.
QVector<QPointF> bangOutline(qreal ew, qreal eh) {
  const qreal r = 0.15 * ew;
  const qreal r8 = 0.8 * r;
  QVector<QPointF> pts;
  pts.append(QPointF(0.0, 0.0));
  qreal x = 0.0, y = 0.0;
  auto step = [&](qreal dx, qreal dy, qreal rr) {
    const qreal nx = x + dx, ny = y + dy;
    appendEllipticalArc(pts, x, y, nx, ny, rr, rr, false, false, 16);
    x = nx; y = ny;
  };
  step(0.25 * ew, -0.1 * eh, r);
  step(0.25 * ew, 0.0, r);
  step(0.25 * ew, 0.0, r);
  step(0.25 * ew, 0.1 * eh, r);
  step(0.15 * ew, 0.33 * eh, r);
  step(0.0, 0.34 * eh, r8);
  step(-0.15 * ew, 0.33 * eh, r);
  step(-0.25 * ew, 0.15 * eh, r);
  step(-0.25 * ew, 0.0, r);
  step(-0.25 * ew, 0.0, r);
  step(-0.25 * ew, -0.15 * eh, r);
  step(-0.1 * ew, -0.33 * eh, r);
  step(0.0, -0.34 * eh, r8);
  step(0.1 * ew, -0.33 * eh, r);
  // Path closes H0 V0 Z -> the polygon's Z draws (x,y) back to (0,0).
  return pts;
}

// cloud outline: the cloud path from cloud.ts. One arc is elliptical (r2,r1);
// the rest are circular. Same M0 0 ... Z structure.
QVector<QPointF> cloudOutline(qreal w, qreal h) {
  const qreal r1 = 0.15 * w, r2 = 0.25 * w, r3 = 0.35 * w, r4 = 0.2 * w;
  QVector<QPointF> pts;
  pts.append(QPointF(0.0, 0.0));
  qreal x = 0.0, y = 0.0;
  auto step = [&](qreal dx, qreal dy, qreal rx, qreal ry) {
    const qreal nx = x + dx, ny = y + dy;
    appendEllipticalArc(pts, x, y, nx, ny, rx, ry, false, true, 16);
    x = nx; y = ny;
  };
  step(0.25 * w, -0.1 * w, r1, r1);
  step(0.4 * w, -0.1 * w, r3, r3);
  step(0.35 * w, 0.2 * w, r2, r2);
  step(0.15 * w, 0.35 * h, r1, r1);
  step(-0.15 * w, 0.65 * h, r4, r4);
  step(-0.25 * w, 0.15 * w, r2, r1);  // elliptical (rx=r2, ry=r1)
  step(-0.5 * w, 0.0, r3, r3);
  step(-0.25 * w, -0.15 * w, r1, r1);
  step(-0.1 * w, -0.35 * h, r1, r1);
  step(0.1 * w, -0.65 * h, r4, r4);
  return pts;
}

}  // namespace

// 1:1 port of each upstream polygon shape handler's `points` array, expressed in
// the node's centred coordinate system (origin = node centre, bbox = node bbox).
// See flowShapePolygonPoints docs in the header for the transform-derivation
// rationale. Constants (NOTCH_SIZE, gap, ro, etc.) mirror the upstream sources.
QVector<QPointF> flowShapePolygonPoints(const QString& type, qreal w, qreal h, FlowLook look) {
  const qreal W = w, H = h;
  if (type == QLatin1String("diamond")) {
    // question.ts replaces insertPolygonShape's centred transform with
    // translate(-s/2 + 0.5, s/2). The visible diamond is therefore shifted
    // exactly half a pixel to the right of the dagre node centre.
    return {QPointF(0.5, H / 2.0), QPointF(W / 2.0 + 0.5, 0.0),
            QPointF(0.5, -H / 2.0), QPointF(-W / 2.0 + 0.5, 0.0)};
  }
  if (type == QLatin1String("odd")) {
    const qreal baseWidth = W - H / 4.0;
    return {QPointF(-W / 2.0, -H / 2.0), QPointF(-baseWidth / 2.0 + H / 8.0, 0.0),
            QPointF(-W / 2.0, H / 2.0), QPointF(W / 2.0, H / 2.0),
            QPointF(W / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("hexagon")) {
    const qreal inset = H / 4.0;
    return {QPointF(-W / 2.0 + inset, -H / 2.0), QPointF(W / 2.0 - inset, -H / 2.0),
            QPointF(W / 2.0, 0.0), QPointF(W / 2.0 - inset, H / 2.0),
            QPointF(-W / 2.0 + inset, H / 2.0), QPointF(-W / 2.0, 0.0)};
  }
  if (type == QLatin1String("trapezoid")) {
    const qreal baseWidth = W - H;
    return {QPointF(-W / 2.0, H / 2.0), QPointF(W / 2.0, H / 2.0),
            QPointF(baseWidth / 2.0, -H / 2.0), QPointF(-baseWidth / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("inv_trapezoid")) {
    const qreal baseWidth = W - H;
    return {QPointF(-baseWidth / 2.0, H / 2.0), QPointF(baseWidth / 2.0, H / 2.0),
            QPointF(W / 2.0, -H / 2.0), QPointF(-W / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("lean_right")) {
    const qreal baseWidth = W - H;
    return {QPointF(-W / 2.0, H / 2.0), QPointF(baseWidth / 2.0, H / 2.0),
            QPointF(W / 2.0, -H / 2.0), QPointF(-baseWidth / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("lean_left")) {
    const qreal baseWidth = W - H;
    return {QPointF(-baseWidth / 2.0, H / 2.0), QPointF(W / 2.0, H / 2.0),
            QPointF(baseWidth / 2.0, -H / 2.0), QPointF(-W / 2.0, -H / 2.0)};
  }
  // Expanded shapes (milestone E). Each branch derives from the handler's raw
  // `points` + its centring transform, simplified to the centred bbox.
  if (type == QLatin1String("triangle")) {
    return {QPointF(-W / 2.0, H / 2.0), QPointF(W / 2.0, H / 2.0), QPointF(0.0, -H / 2.0)};
  }
  if (type == QLatin1String("flipped_triangle")) {
    return {QPointF(-W / 2.0, -H / 2.0), QPointF(W / 2.0, -H / 2.0), QPointF(0.0, H / 2.0)};
  }
  if (type == QLatin1String("hourglass")) {
    return {QPointF(-W / 2.0, -H / 2.0), QPointF(W / 2.0, -H / 2.0),
            QPointF(-W / 2.0, H / 2.0), QPointF(W / 2.0, H / 2.0)};
  }
  if (type == QLatin1String("notched_pentagon")) {
    return {QPointF(-W * 0.4, -H / 2.0), QPointF(W * 0.4, -H / 2.0),
            QPointF(W / 2.0, -H * 0.3), QPointF(W / 2.0, H / 2.0),
            QPointF(-W / 2.0, H / 2.0), QPointF(-W / 2.0, -H * 0.3)};
  }
  if (type == QLatin1String("card")) {
    const qreal notch = 12.0;
    return {QPointF(notch - W / 2.0, -H / 2.0), QPointF(W / 2.0, -H / 2.0),
            QPointF(W / 2.0, H / 2.0), QPointF(-W / 2.0, H / 2.0),
            QPointF(-W / 2.0, notch - H / 2.0), QPointF(notch - W / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("sloped_rect")) {
    return {QPointF(-W / 2.0, -H / 6.0), QPointF(-W / 2.0, H / 2.0),
            QPointF(W / 2.0, H / 2.0), QPointF(W / 2.0, -H / 2.0)};
  }
  if (type == QLatin1String("lightning_bolt")) {
    const qreal gap = 7.0;
    return {QPointF(W / 2.0, -H / 2.0), QPointF(-W / 2.0, gap / 2.0),
            QPointF(W / 2.0 - 2.0 * gap, gap / 2.0), QPointF(-W / 2.0, H / 2.0),
            QPointF(W / 2.0, -gap / 2.0), QPointF(2.0 * gap - W / 2.0, -gap / 2.0)};
  }
  if (type == QLatin1String("stacked_rect")) {
    const qreal ro = look == FlowLook::Neo ? 10.0 : 5.0;
    return {QPointF(-W / 2.0, -H / 2.0 + 2.0 * ro), QPointF(-W / 2.0, H / 2.0),
            QPointF(W / 2.0 - 2.0 * ro, H / 2.0), QPointF(W / 2.0 - 2.0 * ro, H / 2.0 - ro),
            QPointF(W / 2.0 - ro, H / 2.0 - ro), QPointF(W / 2.0 - ro, H / 2.0 - 2.0 * ro),
            QPointF(W / 2.0, H / 2.0 - 2.0 * ro), QPointF(W / 2.0, -H / 2.0),
            QPointF(-W / 2.0 + 2.0 * ro, -H / 2.0), QPointF(-W / 2.0 + 2.0 * ro, -H / 2.0 + ro),
            QPointF(-W / 2.0 + ro, -H / 2.0 + ro), QPointF(-W / 2.0 + ro, -H / 2.0 + 2.0 * ro)};
  }

  // --- wave-edged shapes (generateFullSineWavePoints) ---
  // document (waveEdgedRectangle): node.height = h + 2*waveAmp (h internal +
  // sine bulge both handled by the +waveAmp in finalH and the transform), so
  // h = H/1.25, waveAmp = h/8 = 0.1*H, finalH = h + waveAmp = 0.9*H.
  if (type == QLatin1String("document")) {
    const qreal heightRatio = look == FlowLook::Neo ? 1.5 : 1.25;
    const qreal hw = W, hh = H / heightRatio, waveAmp = hh / 8.0, finalH = hh + waveAmp;
    QVector<QPointF> pts;
    pts.append(QPointF(-hw / 2.0, finalH / 2.0));
    pts += sineWavePoints(-hw / 2.0, finalH / 2.0, hw / 2.0, finalH / 2.0, waveAmp, 0.8);
    pts.append(QPointF(hw / 2.0, -finalH / 2.0));
    pts.append(QPointF(-hw / 2.0, -finalH / 2.0));
    return centreOnBbox(pts);
  }
  // flag (waveRectangle): node.height = h + 4*waveAmp = 1.5*h (sine bulges both
  // top and bottom). h = H/1.5, waveAmp = h/8 = H/12, finalH = h + 2*waveAmp = 5H/6.
  // Symmetric about both axes (already centred).
  if (type == QLatin1String("flag")) {
    const qreal hw = W, hh = H / 1.5, waveAmp = hh / 8.0, finalH = hh + 2.0 * waveAmp;
    QVector<QPointF> pts;
    pts.append(QPointF(-hw / 2.0, finalH / 2.0));
    pts += sineWavePoints(-hw / 2.0, finalH / 2.0, hw / 2.0, finalH / 2.0, waveAmp, 1.0);
    pts.append(QPointF(hw / 2.0, -finalH / 2.0));
    pts += sineWavePoints(hw / 2.0, -finalH / 2.0, -hw / 2.0, -finalH / 2.0, waveAmp, -1.0);
    return centreOnBbox(pts);
  }
  // multi_document (multiWaveEdgedRectangle): node.width = w + 2*rectOffset,
  // node.height = finalH + 2*rectOffset + waveAmp. rectOffset=10.
  // w = W - 20, h = (H - 20)/1.1875, waveAmp = h/8, finalH = h + waveAmp/2.
  if (type == QLatin1String("multi_document")) {
    const qreal rectOffset = 10.0;
    const qreal hw = W - 2.0 * rectOffset;
    const qreal hh = (H - 2.0 * rectOffset) / 1.1875;
    const qreal waveAmp = hh / 8.0, finalH = hh + waveAmp / 2.0;
    const qreal x = -hw / 2.0, y = -finalH / 2.0;
    QVector<QPointF> wavePts = sineWavePoints(x - rectOffset, y + finalH + rectOffset,
                                               x + hw - rectOffset, y + finalH + rectOffset,
                                               waveAmp, 0.8);
    const qreal lastY = wavePts.last().y();
    QVector<QPointF> pts;
    pts.append(QPointF(x - rectOffset, y + rectOffset));
    pts.append(QPointF(x - rectOffset, y + finalH + rectOffset));
    pts += wavePts;
    pts.append(QPointF(x + hw - rectOffset, lastY - rectOffset));
    pts.append(QPointF(x + hw, lastY - rectOffset));
    pts.append(QPointF(x + hw, lastY - 2.0 * rectOffset));
    pts.append(QPointF(x + hw + rectOffset, lastY - 2.0 * rectOffset));
    pts.append(QPointF(x + hw + rectOffset, y - rectOffset));
    pts.append(QPointF(x + rectOffset, y - rectOffset));
    pts.append(QPointF(x + rectOffset, y));
    pts.append(QPointF(x, y));
    pts.append(QPointF(x, y + rectOffset));
    return centreOnBbox(pts);
  }
  // tagged_document (taggedWaveEdgedRectangle): the wave rect is 1.1*w wide
  // (w/2*0.1 extra each side). node.width = 1.1*w, node.height = finalH +
  // waveAmp = h + 2*waveAmp = 1.25*h. The folded-corner tag is a separate small
  // path; its area is negligible vs the wave rect so the silhouette omits it
  // (within tolerance). intersect uses this wave-rect polygon (matches handler).
  if (type == QLatin1String("tagged_document")) {
    const qreal hw = W / 1.1, hh = H / 1.25, waveAmp = hh / 8.0, finalH = hh + waveAmp;
    const qreal x0 = -hw / 2.0 - hw / 2.0 * 0.1, x1 = hw / 2.0 + hw / 2.0 * 0.1;
    QVector<QPointF> pts;
    pts.append(QPointF(x0, finalH / 2.0));
    pts += sineWavePoints(x0, finalH / 2.0, x1, finalH / 2.0, waveAmp, 0.8);
    pts.append(QPointF(x1, -finalH / 2.0));
    pts.append(QPointF(x0, -finalH / 2.0));
    return centreOnBbox(pts);
  }
  // lined_document (linedWaveEdgedRect): wave rect 1.1*w wide (w/2*0.1 extra
  // each side), node.height = finalH + waveAmp (transform -waveAmp/2). rc.polygon
  // closes P_n->P_2 so the fill collapses to the wave-rect bbox (like
  // divided_rect). h = H/1.25, waveAmp = h/8, finalH = h + waveAmp.
  if (type == QLatin1String("lined_document")) {
    const qreal heightRatio = look == FlowLook::Neo ? 1.5 : 1.25;
    const qreal hw = W / 1.1, hh = H / heightRatio, waveAmp = hh / 8.0, finalH = hh + waveAmp;
    const qreal x0 = -hw / 2.0 - hw / 2.0 * 0.1, x1 = hw / 2.0 + hw / 2.0 * 0.1;
    QVector<QPointF> pts;
    pts.append(QPointF(x0, finalH / 2.0));
    pts.append(QPointF(x0, -finalH / 2.0));
    pts += sineWavePoints(x0, finalH / 2.0, x1, finalH / 2.0, waveAmp, 0.8);
    pts.append(QPointF(x1, -finalH / 2.0));
    pts.append(QPointF(x0, -finalH / 2.0));
    pts.append(QPointF(-hw / 2.0, -finalH / 2.0));
    pts.append(QPointF(-hw / 2.0, finalH / 2.0 * 1.1));
    pts.append(QPointF(-hw / 2.0, -finalH / 2.0));
    return centreOnBbox(pts);
  }

  // --- arc-edged shapes (generateArcPoints / generateCirclePoints) ---
  // bow_tie_rect (bowTieRect): left arc bulges OUT (to -w/2-rx), right arc
  // bulges IN (to w/2-rx) -> bbox right edge = w/2, so node.width = w + rx.
  // rx = ry/(2.5+h/50), ry = h/2 = H/2.
  if (type == QLatin1String("bow_tie_rect")) {
    const qreal hh = H, ry = hh / 2.0, rx = ry / (2.5 + hh / 50.0);
    const qreal hw = W - rx;
    QVector<QPointF> pts;
    pts.append(QPointF(hw / 2.0, -hh / 2.0));
    pts.append(QPointF(-hw / 2.0, -hh / 2.0));
    pts += ellipseArcPoints(-hw / 2.0, -hh / 2.0, -hw / 2.0, hh / 2.0, rx, ry, false);
    pts.append(QPointF(hw / 2.0, hh / 2.0));
    pts += ellipseArcPoints(hw / 2.0, hh / 2.0, hw / 2.0, -hh / 2.0, rx, ry, true);
    return centreOnBbox(pts);
  }
  // half_rounded_rect (halfRoundedRectangle): rectangle with a semicircular
  // right side (radius = h/2). bbox = w x h exactly. Points already centred.
  if (type == QLatin1String("half_rounded_rect")) {
    const qreal hw = W, hh = H, radius = hh / 2.0;
    QVector<QPointF> pts;
    pts.append(QPointF(-hw / 2.0, -hh / 2.0));
    pts.append(QPointF(hw / 2.0 - radius, -hh / 2.0));
    pts += circleArcPoints(-hw / 2.0 + radius, 0.0, radius, 50, 90.0, 270.0, true);
    pts.append(QPointF(hw / 2.0 - radius, hh / 2.0));
    pts.append(QPointF(-hw / 2.0, hh / 2.0));
    return centreOnBbox(pts);
  }
  // curved_trapezoid (curvedTrapezoid): trapezoid with a curved right side that
  // bulges OUT to x=w, so node.width = w, node.height = h. radius = h/2 = H/2.
  if (type == QLatin1String("curved_trapezoid")) {
    const qreal hh = H, radius = hh / 2.0, hw = W;
    const qreal rw = hw - radius, tw = hh / 4.0;
    QVector<QPointF> pts;
    pts.append(QPointF(rw, 0.0));
    pts.append(QPointF(tw, 0.0));
    pts.append(QPointF(0.0, hh / 2.0));
    pts.append(QPointF(tw, hh));
    pts.append(QPointF(rw, hh));
    pts += circleArcPoints(-rw, -hh / 2.0, radius, 50, 270.0, 90.0, true);
    return centreOnBbox(pts);
  }

  // --- curly braces (rectPoints = the filled background outline) ---
  // brace_left (curlyBraceLeft): node.width = w + 2*radius (rectPoints' second
  // arc reaches x = -(w/2+2*radius)), node.height = h + 2*radius.
  if (type == QLatin1String("brace_left")) {
    qreal hh, radius;
    if (H >= 60.0) { hh = H / 1.2; radius = hh / 10.0; }
    else { radius = 5.0; hh = H - 2.0 * radius; }
    const qreal hw = W - 2.0 * radius;
    QVector<QPointF> pts;
    pts.append(QPointF(hw / 2.0, -hh / 2.0 - radius));
    pts.append(QPointF(-hw / 2.0, -hh / 2.0 - radius));
    pts += circleArcPoints(hw / 2.0, -hh / 2.0, radius, 20, -90.0, 0.0, true);
    pts.append(QPointF(-hw / 2.0 - radius, -radius));
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, -radius, radius, 20, -180.0, -270.0, true);
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, radius, radius, 20, -90.0, -180.0, true);
    pts.append(QPointF(-hw / 2.0 - radius, hh / 2.0));
    pts += circleArcPoints(hw / 2.0, hh / 2.0, radius, 20, 0.0, 90.0, true);
    return centreOnBbox(pts);
  }
  // brace_right (curlyBraceRight): node.width = w + 2*radius, node.height =
  // h + 2*radius. Uses generateCirclePoints3 (no negate).
  if (type == QLatin1String("brace_right")) {
    qreal hh, radius;
    if (H >= 60.0) { hh = H / 1.2; radius = hh / 10.0; }
    else { radius = 5.0; hh = H - 2.0 * radius; }
    const qreal hw = W - 2.0 * radius;
    QVector<QPointF> pts;
    pts.append(QPointF(-hw / 2.0, -hh / 2.0 - radius));
    pts.append(QPointF(hw / 2.0, -hh / 2.0 - radius));
    pts += circleArcPoints(hw / 2.0, -hh / 2.0, radius, 20, -90.0, 0.0, false);
    pts.append(QPointF(hw / 2.0 + radius, -radius));
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, -radius, radius, 20, -180.0, -270.0, false);
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, radius, radius, 20, -90.0, -180.0, false);
    pts.append(QPointF(hw / 2.0 + radius, hh / 2.0));
    pts += circleArcPoints(hw / 2.0, hh / 2.0, radius, 20, 0.0, 90.0, false);
    return centreOnBbox(pts);
  }
  // braces (curlyBraces): both braces; rectPoints = left + right outline. The
  // right brace's outer arc reaches x = w/2 + radius/2, so node.width =
  // w + 2.5*radius, node.height = h + 2*radius.
  if (type == QLatin1String("braces")) {
    qreal hh, radius;
    if (H >= 60.0) { hh = H / 1.2; radius = hh / 10.0; }
    else { radius = 5.0; hh = H - 2.0 * radius; }
    const qreal hw = W - 2.0 * radius - radius / 2.0;
    QVector<QPointF> pts;
    // left brace outline
    pts.append(QPointF(hw / 2.0, -hh / 2.0 - radius));
    pts.append(QPointF(-hw / 2.0, -hh / 2.0 - radius));
    pts += circleArcPoints(hw / 2.0, -hh / 2.0, radius, 20, -90.0, 0.0, true);
    pts.append(QPointF(-hw / 2.0 - radius, -radius));
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, -radius, radius, 20, -180.0, -270.0, true);
    pts += circleArcPoints(hw / 2.0 + radius * 2.0, radius, radius, 20, -90.0, -180.0, true);
    pts.append(QPointF(-hw / 2.0 - radius, hh / 2.0));
    pts += circleArcPoints(hw / 2.0, hh / 2.0, radius, 20, 0.0, 90.0, true);
    pts.append(QPointF(-hw / 2.0, hh / 2.0 + radius));
    pts.append(QPointF(hw / 2.0 - radius - radius / 2.0, hh / 2.0 + radius));
    // right brace outline
    pts += circleArcPoints(-hw / 2.0 + radius + radius / 2.0, -hh / 2.0, radius, 20, -90.0, -180.0, true);
    pts.append(QPointF(hw / 2.0 - radius / 2.0, radius));
    pts += circleArcPoints(-hw / 2.0 - radius / 2.0, -radius, radius, 20, 0.0, 90.0, true);
    pts += circleArcPoints(-hw / 2.0 - radius / 2.0, radius, radius, 20, -90.0, 0.0, true);
    pts.append(QPointF(hw / 2.0 - radius / 2.0, -radius));
    pts += circleArcPoints(-hw / 2.0 + radius + radius / 2.0, hh / 2.0, radius, 30, -180.0, -270.0, true);
    return centreOnBbox(pts);
  }

  return {};
}

QSizeF flowShapeArcShapeSize(const QString& type, qreal labelW, qreal labelH) {
  // bang/cloud outlines are sampled directly at the label-derived path params
  // (no affine approximation: some arcs hit the SVG radius scale-up region,
  // which is non-linear and breaks a fit-at-large-(w,h) model).
  if (type == QLatin1String("bang")) {
    return bboxOf(bangOutline(labelW + 75.0, labelH + 60.0));
  }
  if (type == QLatin1String("cloud")) {
    return bboxOf(cloudOutline(labelW + 15.0, labelH + 15.0));
  }
  return {};
}

QPainterPath flowShapeHorizontalCylinderPath(const QRectF& bounds, qreal rx, qreal ry) {
  // tiltedCylinder's createCylinderPathD3, ported to a QPainterPath by sampling
  // each SVG `a` arc in traversal order (Qt's arcTo reverses arc direction,
  // which flips the subpath winding and breaks the nonzero fill). Two subpaths:
  //   S1: (-w/2,h/2) -> left cap [sweep1, out] -> (-w/2,-h/2) -> top edge ->
  //       (w/2,-h/2) -> right cap [sweep1, out] -> (w/2,h/2) -> close (bottom)
  //   S2: (w/2,-h/2) -> right cap [sweep0, in] -> (w/2,h/2) -> bottom edge ->
  //       (-w/2,h/2) -> close (diagonal)
  // WindingFill == SVG nonzero, so the overlapping right-cap region cancels and
  // the fill is the tilted-cylinder silhouette (smaller than the path bbox).
  const qreal w = bounds.width() - 2.0 * rx;
  const qreal h = bounds.height();
  const int samples = 24;
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  auto addSubpath = [&](const QVector<QPointF>& pts) {
    if (pts.isEmpty()) return;
    path.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i) path.lineTo(pts.at(i));
    path.closeSubpath();
  };
  QVector<QPointF> s1;
  s1.append(QPointF(-w / 2.0, h / 2.0));
  appendEllipticalArc(s1, -w / 2.0, h / 2.0, -w / 2.0, -h / 2.0, rx, ry, false, true, samples);
  s1.append(QPointF(w / 2.0, -h / 2.0));
  appendEllipticalArc(s1, w / 2.0, -h / 2.0, w / 2.0, h / 2.0, rx, ry, false, true, samples);
  addSubpath(s1);
  QVector<QPointF> s2;
  s2.append(QPointF(w / 2.0, -h / 2.0));
  appendEllipticalArc(s2, w / 2.0, -h / 2.0, w / 2.0, h / 2.0, rx, ry, false, false, samples);
  s2.append(QPointF(-w / 2.0, h / 2.0));
  addSubpath(s2);
  return path;
}

FlowShapeGeometry flowShapeGeometry(const FlowVertex& vertex, const QSizeF& size,
                                    FlowLook look, qreal shapeRadius) {
  FlowShapeGeometry result;
  result.bounds = QRectF(-size.width() / 2.0, -size.height() / 2.0,
                        size.width(), size.height());
  // Collapse legacy bracket names, @{ shape: shortName }, and aliases onto one
  // canonical key so each shape has a single geometry implementation.
  const QString type = canonicalShape(vertex.type);
  const QVector<QPointF> pts = flowShapePolygonPoints(type, size.width(), size.height(), look);
  if (!pts.isEmpty()) {
    result.kind = QStringLiteral("polygon");
    result.points = pts;
    return result;
  }
  // bang/cloud: SVG-arc paths whose outline depends on the label (not on W,H
  // alone — some arcs hit the radius scale-up region). Measure the label, sample
  // the outline at the label-derived params, and centre. intersectNodeForShape
  // routes these to rect (matching the handler), so the points are for the
  // silhouette only.
  if (type == QLatin1String("bang") || type == QLatin1String("cloud")) {
    const QSizeF label = measureLabel(vertex.text, FlowTextOptions{});
    const qreal pathWidth = label.width() +
        (type == QLatin1String("bang") ? 75.0 : 15.0);
    const qreal pathHeight = label.height() +
        (type == QLatin1String("bang") ? 60.0 : 15.0);
    QVector<QPointF> raw = type == QLatin1String("bang")
        ? bangOutline(pathWidth, pathHeight)
        : cloudOutline(pathWidth, pathHeight);
    result.kind = QStringLiteral("polygon");
    // bang.ts/cloud.ts translate the path by (-w/2,-h/2), not by its actual
    // arc bbox centre. SVG arc correction makes that distinction visible:
    // the bang silhouette in the 11.16 fixture is +2.38632965px off-centre.
    result.points = translateFromPathOrigin(std::move(raw), pathWidth,
                                            pathHeight);
    return result;
  }
  if (type == QLatin1String("round")) {
    result.kind = QStringLiteral("roundedRect");
    result.cornerRadius = shapeRadius;
  } else if (type == QLatin1String("circle") ||
             type == QLatin1String("double_circle") ||
             type == QLatin1String("filled_circle") ||
             type == QLatin1String("crossed_circle") ||
             type == QLatin1String("small_circle") ||
             type == QLatin1String("framed_circle")) {
    // double_circle's silhouette is the outer disk (the inner circle is the same
    // fill); filled_circle/crossed_circle/small_circle/framed_circle share the
    // circle geometry. small_circle/framed_circle are fixed 14x14 stop points.
    result.kind = QStringLiteral("ellipse");
  } else if (type == QLatin1String("stadium")) {
    result.kind = QStringLiteral("stadium");
    result.cornerRadius = size.height() / 2.0;
  } else if (type == QLatin1String("subroutine")) {
    result.kind = QStringLiteral("subroutine");
  } else if (type == QLatin1String("cylinder") ||
             type == QLatin1String("lined_cylinder")) {
    // lined_cylinder adds a horizontal stroke line (zero fill area) to the
    // legacy cylinder, so the silhouette is identical.
    result.kind = QStringLiteral("cylinder");
    result.radiusX = size.width() / 2.0;
    result.radiusY = result.radiusX / (2.5 + size.width() / 50.0);
  } else if (type == QLatin1String("horizontal_cylinder")) {
    // tiltedCylinder: a cylinder laid on its side (caps on left/right). The cap
    // ellipse has rx = ry/(2.5+h/50), ry = h/2 (node.height = h).
    result.kind = QStringLiteral("horizontalCylinder");
    result.radiusY = size.height() / 2.0;
    result.radiusX = result.radiusY / (2.5 + size.height() / 50.0);
  } else {
    // rect fallback: also covers datastore (dashed rect, fill = solid rect),
    // text, tagged_rect (tag is interior, silhouette = outer rect),
    // lined_process (shaded-process frame fills to its outer rect), and
    // window_pane (divider lines are stroke-only, fill = outer rect).
    result.kind = QStringLiteral("rect");
  }
  return result;
}

}  // namespace muffin::mermaid::flowchart
