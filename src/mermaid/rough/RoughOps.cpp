#include "mermaid/rough/RoughOps.h"

#include <QLineF>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

// Algorithm ported from RoughJS 4.6.6 (MIT), renderer.ts and hachure-fill 0.5.2.
namespace muffin::mermaid::rough {
namespace {

constexpr qreal kTau = M_PI * 2.0;

struct Random {
  quint32 seed = 0;
  qreal next() {
    seed = static_cast<quint32>((quint64{48271} * seed) & 0x7fffffffU);
    return static_cast<qreal>(seed) / 2147483648.0;
  }
};

struct State {
  Options options;
  std::optional<Random> randomizer;

  qreal random() {
    if (!randomizer) randomizer = Random{options.seed};
    return randomizer->next();
  }
  qreal offset(qreal min, qreal max, qreal gain = 1.0) {
    return options.roughness * gain * (random() * (max - min) + min);
  }
  qreal offsetOpt(qreal extent, qreal gain = 1.0) {
    return offset(-extent, extent, gain);
  }
};

Op move(QPointF p) { return {OpType::Move, {p.x(), p.y()}}; }
Op lineTo(QPointF p) { return {OpType::LineTo, {p.x(), p.y()}}; }
Op bcurve(QPointF c1, QPointF c2, QPointF end) {
  return {OpType::BcurveTo, {c1.x(), c1.y(), c2.x(), c2.y(), end.x(), end.y()}};
}

QPointF randomOffset(State& state, qreal extent) {
  const qreal x = state.offsetOpt(extent);
  const qreal y = state.offsetOpt(extent);
  return QPointF(x, y);
}

QVector<Op> singleLine(QPointF p1, QPointF p2, State& state,
                       bool shouldMove, bool overlay) {
  const qreal lengthSq = std::pow(p1.x() - p2.x(), 2) + std::pow(p1.y() - p2.y(), 2);
  const qreal length = std::sqrt(lengthSq);
  const qreal gain = length < 200.0 ? 1.0
      : length > 500.0 ? 0.4 : -0.0016668 * length + 1.233334;
  qreal randomness = state.options.maxRandomnessOffset;
  if (randomness * randomness * 100.0 > lengthSq) randomness = length / 10.0;
  const qreal half = randomness / 2.0;
  const qreal diverge = 0.2 + state.random() * 0.2;
  qreal midX = state.options.bowing * state.options.maxRandomnessOffset *
               (p2.y() - p1.y()) / 200.0;
  qreal midY = state.options.bowing * state.options.maxRandomnessOffset *
               (p1.x() - p2.x()) / 200.0;
  midX = state.offsetOpt(midX, gain);
  midY = state.offsetOpt(midY, gain);
  const qreal extent = overlay ? half : randomness;
  auto jitter = [&] { return state.offsetOpt(extent, gain); };
  QVector<Op> ops;
  if (shouldMove) {
    const qreal mx = state.options.preserveVertices ? 0.0 : jitter();
    const qreal my = state.options.preserveVertices ? 0.0 : jitter();
    ops.append(move(p1 + QPointF(mx, my)));
  }
  const qreal c1x = jitter(), c1y = jitter();
  const qreal c2x = jitter(), c2y = jitter();
  const qreal endX = state.options.preserveVertices ? 0.0 : jitter();
  const qreal endY = state.options.preserveVertices ? 0.0 : jitter();
  const QPointF c1(midX + p1.x() + (p2.x() - p1.x()) * diverge + c1x,
                   midY + p1.y() + (p2.y() - p1.y()) * diverge + c1y);
  const QPointF c2(midX + p1.x() + 2.0 * (p2.x() - p1.x()) * diverge + c2x,
                   midY + p1.y() + 2.0 * (p2.y() - p1.y()) * diverge + c2y);
  const QPointF end = p2 + QPointF(endX, endY);
  ops.append(bcurve(c1, c2, end));
  return ops;
}

QVector<Op> doubleLine(QPointF p1, QPointF p2, State& state, bool filling = false) {
  QVector<Op> ops = singleLine(p1, p2, state, true, false);
  const bool single = filling ? state.options.disableMultiStrokeFill
                              : state.options.disableMultiStroke;
  if (!single) ops += singleLine(p1, p2, state, true, true);
  return ops;
}

OpSet linearPath(const QVector<QPointF>& points, bool close, State& state) {
  OpSet result;
  if (points.size() == 2) {
    result.ops = doubleLine(points[0], points[1], state);
    return result;
  }
  for (qsizetype i = 0; i + 1 < points.size(); ++i)
    result.ops += doubleLine(points[i], points[i + 1], state);
  if (close && points.size() > 2)
    result.ops += doubleLine(points.last(), points.first(), state);
  return result;
}

QVector<Op> curveOps(const QVector<QPointF>& points, State& state,
                     const std::optional<QPointF>& closePoint = std::nullopt) {
  QVector<Op> ops;
  if (points.size() > 3) {
    const qreal s = 1.0 - state.options.curveTightness;
    ops.append(move(points[1]));
    for (qsizetype i = 1; i + 2 < points.size(); ++i) {
      const QPointF c1 = points[i] + s * (points[i + 1] - points[i - 1]) / 6.0;
      const QPointF c2 = points[i + 1] + s * (points[i] - points[i + 2]) / 6.0;
      ops.append(bcurve(c1, c2, points[i + 1]));
    }
    if (closePoint)
      ops.append(lineTo(*closePoint + randomOffset(state, state.options.maxRandomnessOffset)));
  } else if (points.size() == 3) {
    ops.append(move(points[1]));
    ops.append(bcurve(points[1], points[2], points[2]));
  } else if (points.size() == 2) {
    ops += singleLine(points[0], points[1], state, true, true);
  }
  return ops;
}

QVector<Op> curveWithOffset(const QVector<QPointF>& points, qreal offset, State& state) {
  if (points.isEmpty()) return {};
  QVector<QPointF> shifted;
  shifted.reserve(points.size() + 2);
  auto shiftedPoint = [&](QPointF point) {
    return point + randomOffset(state, offset);
  };
  shifted.append(shiftedPoint(points.first()));
  shifted.append(shiftedPoint(points.first()));
  for (qsizetype i = 1; i < points.size(); ++i) {
    shifted.append(shiftedPoint(points[i]));
    if (i == points.size() - 1) shifted.append(shiftedPoint(points[i]));
  }
  return curveOps(shifted, state);
}

struct EllipseParams { qreal increment; qreal rx; qreal ry; };

EllipseParams ellipseParams(qreal width, qreal height, State& state) {
  const qreal psq = std::sqrt(M_PI * 2.0 * std::sqrt(
      (std::pow(width / 2.0, 2) + std::pow(height / 2.0, 2)) / 2.0));
  const int steps = static_cast<int>(std::ceil(std::max<qreal>(
      state.options.curveStepCount,
      state.options.curveStepCount / std::sqrt(200.0) * psq)));
  qreal rx = std::abs(width / 2.0), ry = std::abs(height / 2.0);
  const qreal randomness = 1.0 - state.options.curveFitting;
  rx += state.offsetOpt(rx * randomness);
  ry += state.offsetOpt(ry * randomness);
  return {kTau / steps, rx, ry};
}

QPair<QVector<QPointF>, QVector<QPointF>> ellipsePoints(
    qreal increment, qreal cx, qreal cy, qreal rx, qreal ry,
    qreal offset, qreal overlap, State& state) {
  QVector<QPointF> all, core;
  if (state.options.roughness == 0) {
    increment /= 4.0;
    all.append(QPointF(cx + rx * std::cos(-increment), cy + ry * std::sin(-increment)));
    for (qreal angle = 0; angle <= kTau; angle += increment) {
      const QPointF point(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
      core.append(point); all.append(point);
    }
    all.append(QPointF(cx + rx, cy));
    all.append(QPointF(cx + rx * std::cos(increment), cy + ry * std::sin(increment)));
  } else {
    const qreal radialOffset = state.offsetOpt(0.5) - M_PI / 2.0;
    auto noisy = [&](qreal scale, qreal angle) {
      const qreal x = state.offsetOpt(offset) + cx + scale * rx * std::cos(angle);
      const qreal y = state.offsetOpt(offset) + cy + scale * ry * std::sin(angle);
      return QPointF(x, y);
    };
    all.append(noisy(0.9, radialOffset - increment));
    const qreal end = kTau + radialOffset - 0.01;
    for (qreal angle = radialOffset; angle < end; angle += increment) {
      const QPointF point = noisy(1.0, angle);
      core.append(point); all.append(point);
    }
    all.append(noisy(1.0, radialOffset + kTau + overlap * 0.5));
    all.append(noisy(0.98, radialOffset + overlap));
    all.append(noisy(0.9, radialOffset + overlap * 0.5));
  }
  return {all, core};
}

QPair<OpSet, QVector<QPointF>> ellipseWithParams(
    qreal x, qreal y, const EllipseParams& params, State& state) {
  const qreal inner = state.offset(0.4, 1.0);
  const qreal overlap = params.increment * state.offset(0.1, inner);
  auto first = ellipsePoints(params.increment, x, y, params.rx, params.ry,
                             1.0, overlap, state);
  OpSet set;
  set.ops = curveOps(first.first, state);
  if (!state.options.disableMultiStroke && state.options.roughness != 0) {
    auto second = ellipsePoints(params.increment, x, y, params.rx, params.ry,
                                1.5, 0, state);
    set.ops += curveOps(second.first, state);
  }
  return {set, first.second};
}

qreal jsRound(qreal value) { return std::floor(value + 0.5); }

void rotatePoints(QVector<QPointF>& points, qreal degrees) {
  if (degrees == 0) return;
  const qreal angle = qDegreesToRadians(degrees);
  const qreal cosine = std::cos(angle), sine = std::sin(angle);
  for (QPointF& point : points)
    point = QPointF(point.x() * cosine - point.y() * sine,
                    point.x() * sine + point.y() * cosine);
}

QVector<QPair<QPointF, QPointF>> hachureLines(
    QVector<QVector<QPointF>> polygons, qreal gap, qreal angle, int stepOffset) {
  for (auto& polygon : polygons) rotatePoints(polygon, angle);
  struct Edge { qreal ymin; qreal ymax; qreal x; qreal inverseSlope; };
  QVector<Edge> edges;
  for (auto polygon : polygons) {
    if (polygon.isEmpty()) continue;
    if (polygon.first() != polygon.last()) polygon.append(polygon.first());
    for (qsizetype i = 0; i + 1 < polygon.size(); ++i) {
      const QPointF p1 = polygon[i], p2 = polygon[i + 1];
      if (p1.y() == p2.y()) continue;
      const qreal ymin = std::min(p1.y(), p2.y());
      edges.append({ymin, std::max(p1.y(), p2.y()),
                    ymin == p1.y() ? p1.x() : p2.x(),
                    (p2.x() - p1.x()) / (p2.y() - p1.y())});
    }
  }
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    if (a.ymin != b.ymin) return a.ymin < b.ymin;
    if (a.x != b.x) return a.x < b.x;
    return a.ymax < b.ymax;
  });
  QVector<QPair<QPointF, QPointF>> lines;
  if (edges.isEmpty()) return lines;
  QVector<Edge> active;
  qreal y = edges.first().ymin;
  int iteration = 0;
  while (!active.isEmpty() || !edges.isEmpty()) {
    qsizetype count = 0;
    while (count < edges.size() && edges[count].ymin <= y) ++count;
    if (count) {
      for (qsizetype i = 0; i < count; ++i) active.append(edges[i]);
      edges.remove(0, count);
    }
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&](const Edge& edge) { return edge.ymax <= y; }),
                 active.end());
    std::sort(active.begin(), active.end(), [](const Edge& a, const Edge& b) {
      return a.x < b.x;
    });
    if (stepOffset != 1 || iteration % static_cast<int>(gap) == 0) {
      for (qsizetype i = 0; i + 1 < active.size(); i += 2)
        lines.append({QPointF(jsRound(active[i].x), y),
                      QPointF(jsRound(active[i + 1].x), y)});
    }
    y += stepOffset;
    for (Edge& edge : active) edge.x += stepOffset * edge.inverseSlope;
    ++iteration;
  }
  for (auto& line : lines) {
    QVector<QPointF> pair{line.first, line.second};
    rotatePoints(pair, -angle);
    line = {pair[0], pair[1]};
  }
  return lines;
}

OpSet patternFill(QVector<QVector<QPointF>> polygons, State& state) {
  qreal gap = state.options.hachureGap;
  if (gap < 0) gap = state.options.strokeWidth * 4.0;
  gap = std::round(std::max(gap, 0.1));
  int step = 1;
  if (state.options.roughness >= 1 && state.random() > 0.7) step = static_cast<int>(gap);
  const auto lines = hachureLines(std::move(polygons), gap,
                                  state.options.hachureAngle + 90.0, step);
  OpSet set{OpSetType::FillSketch, {}};
  for (const auto& line : lines) set.ops += doubleLine(line.first, line.second, state, true);
  return set;
}

OpSet patternFillArc(qreal x, qreal y, qreal width, qreal height,
                     qreal start, qreal stop, State& state) {
  qreal rx = std::abs(width / 2.0);
  qreal ry = std::abs(height / 2.0);
  rx += state.offsetOpt(rx * 0.01);
  ry += state.offsetOpt(ry * 0.01);
  while (start < 0) {
    start += kTau;
    stop += kTau;
  }
  if (stop - start > kTau) {
    start = 0;
    stop = kTau;
  }
  const qreal increment = (stop - start) / state.options.curveStepCount;
  QVector<QPointF> points;
  for (qreal angle = start; angle <= stop; angle += increment)
    points.append(QPointF(x + rx * std::cos(angle), y + ry * std::sin(angle)));
  points.append(QPointF(x + rx * std::cos(stop), y + ry * std::sin(stop)));
  points.append(QPointF(x, y));
  return patternFill({points}, state);
}

OpSet solidFill(const QVector<QVector<QPointF>>& polygons, State& state) {
  OpSet set{OpSetType::FillPath, {}};
  for (const auto& points : polygons) {
    if (points.size() <= 2) continue;
    set.ops.append(move(points.first() + randomOffset(state, state.options.maxRandomnessOffset)));
    for (qsizetype i = 1; i < points.size(); ++i)
      set.ops.append(lineTo(points[i] + randomOffset(state, state.options.maxRandomnessOffset)));
  }
  return set;
}

QVector<Op> bezierTo(QPointF c1, QPointF c2, QPointF end,
                     QPointF current, State& state) {
  QVector<Op> ops;
  const int iterations = state.options.disableMultiStroke ? 1 : 2;
  const qreal extents[2] = {state.options.maxRandomnessOffset != 0
                                ? state.options.maxRandomnessOffset : 1.0,
                            (state.options.maxRandomnessOffset != 0
                                 ? state.options.maxRandomnessOffset : 1.0) + 0.3};
  for (int i = 0; i < iterations; ++i) {
    if (i == 0) ops.append(move(current));
    else ops.append(move(current + (state.options.preserveVertices ? QPointF()
                                                               : randomOffset(state, extents[0]))));
    const QPointF finalPoint = state.options.preserveVertices
        ? end : end + randomOffset(state, extents[i]);
    const QPointF firstControl = c1 + randomOffset(state, extents[i]);
    const QPointF secondControl = c2 + randomOffset(state, extents[i]);
    ops.append(bcurve(firstControl, secondControl, finalPoint));
  }
  return ops;
}

OpSet svgPath(const QPainterPath& source, State& state, bool closed) {
  OpSet set;
  QPointF current, first;
  for (int i = 0; i < source.elementCount(); ++i) {
    const auto element = source.elementAt(i);
    const QPointF point(element.x, element.y);
    if (element.isMoveTo()) { current = first = point; continue; }
    if (element.isLineTo()) {
      set.ops += doubleLine(current, point, state);
      current = point;
    } else if (element.type == QPainterPath::CurveToElement && i + 2 < source.elementCount()) {
      const auto c2 = source.elementAt(++i), end = source.elementAt(++i);
      set.ops += bezierTo(point, QPointF(c2.x, c2.y), QPointF(end.x, end.y), current, state);
      current = QPointF(end.x, end.y);
    }
  }
  const bool closingLineMaterialized =
      source.elementCount() > 1 && source.elementAt(source.elementCount() - 1).isLineTo() &&
      current == first;
  if (closed && !closingLineMaterialized)
    set.ops += doubleLine(current, first, state);
  return set;
}

qreal pointDistanceSquared(QPointF left, QPointF right) {
  const QPointF delta = left - right;
  return delta.x() * delta.x() + delta.y() * delta.y();
}

QPointF lerp(QPointF left, QPointF right, qreal amount) {
  return left + (right - left) * amount;
}

qreal segmentDistanceSquared(QPointF point, QPointF start, QPointF end) {
  const qreal lengthSquared = pointDistanceSquared(start, end);
  if (lengthSquared == 0.0) return pointDistanceSquared(point, start);
  const QPointF span = end - start;
  const qreal projection = std::clamp(QPointF::dotProduct(point - start, span) /
                                          lengthSquared,
                                      0.0, 1.0);
  return pointDistanceSquared(point, lerp(start, end, projection));
}

qreal bezierFlatness(const QVector<QPointF>& points) {
  qreal ux = 3.0 * points[1].x() - 2.0 * points[0].x() - points[3].x();
  qreal uy = 3.0 * points[1].y() - 2.0 * points[0].y() - points[3].y();
  qreal vx = 3.0 * points[2].x() - 2.0 * points[3].x() - points[0].x();
  qreal vy = 3.0 * points[2].y() - 2.0 * points[3].y() - points[0].y();
  ux *= ux; uy *= uy; vx *= vx; vy *= vy;
  return std::max(ux, vx) + std::max(uy, vy);
}

void splitBezier(const QVector<QPointF>& points, qreal tolerance,
                 QVector<QPointF>& output) {
  if (bezierFlatness(points) < tolerance) {
    if (output.isEmpty() || QLineF(output.last(), points[0]).length() > 1.0)
      output.append(points[0]);
    output.append(points[3]);
    return;
  }
  const QPointF q1 = lerp(points[0], points[1], 0.5);
  const QPointF q2 = lerp(points[1], points[2], 0.5);
  const QPointF q3 = lerp(points[2], points[3], 0.5);
  const QPointF r1 = lerp(q1, q2, 0.5);
  const QPointF r2 = lerp(q2, q3, 0.5);
  const QPointF midpoint = lerp(r1, r2, 0.5);
  splitBezier({points[0], q1, r1, midpoint}, tolerance, output);
  splitBezier({midpoint, r2, q3, points[3]}, tolerance, output);
}

void simplifyPoints(const QVector<QPointF>& points, qsizetype start, qsizetype end,
                    qreal epsilon, QVector<QPointF>& output) {
  const QPointF first = points[start], last = points[end - 1];
  qreal maximum = 0.0;
  qsizetype index = 1;
  for (qsizetype i = start + 1; i < end - 1; ++i) {
    const qreal distance = segmentDistanceSquared(points[i], first, last);
    if (distance > maximum) { maximum = distance; index = i; }
  }
  if (std::sqrt(maximum) > epsilon) {
    simplifyPoints(points, start, index + 1, epsilon, output);
    simplifyPoints(points, index, end, epsilon, output);
  } else {
    if (output.isEmpty()) output.append(first);
    output.append(last);
  }
}

QVector<QVector<QPointF>> pointsOnPath(const QPainterPath& source,
                                      qreal tolerance, qreal distance,
                                      bool closed) {
  QVector<QVector<QPointF>> sets;
  QVector<QPointF> currentPoints, pendingCurve;
  QPointF start;
  auto appendCurve = [&] {
    for (qsizetype i = 0; i + 3 < pendingCurve.size(); i += 3)
      splitBezier({pendingCurve[i], pendingCurve[i + 1],
                   pendingCurve[i + 2], pendingCurve[i + 3]},
                  tolerance, currentPoints);
    pendingCurve.clear();
  };
  auto appendPoints = [&] {
    appendCurve();
    if (!currentPoints.isEmpty()) {
      sets.append(currentPoints);
      currentPoints.clear();
    }
  };
  for (int i = 0; i < source.elementCount(); ++i) {
    const auto element = source.elementAt(i);
    const QPointF point(element.x, element.y);
    if (element.isMoveTo()) {
      appendPoints();
      start = point;
      currentPoints.append(start);
    } else if (element.isLineTo()) {
      appendCurve();
      currentPoints.append(point);
    } else if (element.type == QPainterPath::CurveToElement && i + 2 < source.elementCount()) {
      if (pendingCurve.isEmpty())
        pendingCurve.append(currentPoints.isEmpty() ? start : currentPoints.last());
      const auto control2 = source.elementAt(++i);
      const auto end = source.elementAt(++i);
      pendingCurve.append(point);
      pendingCurve.append(QPointF(control2.x, control2.y));
      pendingCurve.append(QPointF(end.x, end.y));
    }
  }
  appendCurve();
  const bool closingLineMaterialized =
      source.elementCount() > 1 && source.elementAt(source.elementCount() - 1).isLineTo() &&
      !currentPoints.isEmpty() && currentPoints.last() == start;
  if (closed && !closingLineMaterialized && !currentPoints.isEmpty())
    currentPoints.append(start);
  if (!currentPoints.isEmpty()) sets.append(currentPoints);
  if (distance <= 0.0) return sets;
  QVector<QVector<QPointF>> simplified;
  for (const auto& set : sets) {
    if (set.isEmpty()) continue;
    QVector<QPointF> points;
    simplifyPoints(set, 0, set.size(), distance, points);
    if (!points.isEmpty()) simplified.append(points);
  }
  return simplified;
}

QVector<Op> arcCurve(qreal increment, qreal cx, qreal cy, qreal rx, qreal ry,
                     qreal start, qreal stop, qreal offset, State& state) {
  const qreal radialOffset = start + state.offsetOpt(0.1);
  QVector<QPointF> points;
  auto noisy = [&](qreal angle) {
    const qreal px = state.offsetOpt(offset) + cx + rx * std::cos(angle);
    const qreal py = state.offsetOpt(offset) + cy + ry * std::sin(angle);
    return QPointF(px, py);
  };
  const qreal firstX = state.offsetOpt(offset) + cx + 0.9 * rx * std::cos(radialOffset - increment);
  const qreal firstY = state.offsetOpt(offset) + cy + 0.9 * ry * std::sin(radialOffset - increment);
  points.append(QPointF(firstX, firstY));
  for (qreal angle = radialOffset; angle <= stop; angle += increment) points.append(noisy(angle));
  const QPointF end(cx + rx * std::cos(stop), cy + ry * std::sin(stop));
  points.append(end); points.append(end);
  return curveOps(points, state);
}

OpSet arcOutline(qreal x, qreal y, qreal width, qreal height, qreal start,
                 qreal stop, bool closed, bool roughClosure, State& state) {
  qreal rx = std::abs(width / 2.0), ry = std::abs(height / 2.0);
  rx += state.offsetOpt(rx * 0.01); ry += state.offsetOpt(ry * 0.01);
  while (start < 0) { start += kTau; stop += kTau; }
  if (stop - start > kTau) { start = 0; stop = kTau; }
  const qreal ellipseIncrement = kTau / state.options.curveStepCount;
  const qreal increment = std::min(ellipseIncrement / 2.0, (stop - start) / 2.0);
  OpSet set;
  set.ops = arcCurve(increment, x, y, rx, ry, start, stop, 1.0, state);
  if (!state.options.disableMultiStroke)
    set.ops += arcCurve(increment, x, y, rx, ry, start, stop, 1.5, state);
  if (closed) {
    const QPointF first(x + rx * std::cos(start), y + ry * std::sin(start));
    const QPointF last(x + rx * std::cos(stop), y + ry * std::sin(stop));
    if (roughClosure) {
      set.ops += doubleLine(QPointF(x, y), first, state);
      set.ops += doubleLine(QPointF(x, y), last, state);
    } else {
      set.ops.append(lineTo(QPointF(x, y)));
      set.ops.append(lineTo(first));
    }
  }
  return set;
}

}  // namespace

Drawable line(qreal x1, qreal y1, qreal x2, qreal y2, Options options) {
  State state{options};
  return {QStringLiteral("line"), {{OpSetType::Path,
          doubleLine(QPointF(x1, y1), QPointF(x2, y2), state)}}, options};
}

Drawable rectangle(qreal x, qreal y, qreal width, qreal height, Options options) {
  State state{options};
  const QVector<QPointF> points{{x, y}, {x + width, y},
                                {x + width, y + height}, {x, y + height}};
  const OpSet outline = linearPath(points, true, state);
  QVector<OpSet> sets;
  if (!options.fill.isEmpty())
    sets.append(options.fillStyle == QLatin1String("solid")
                    ? solidFill({points}, state) : patternFill({points}, state));
  if (options.stroke != QLatin1String("none")) sets.append(outline);
  return {QStringLiteral("rectangle"), sets, options};
}

Drawable polygon(const QVector<QPointF>& points, Options options) {
  State state{options};
  const OpSet outline = linearPath(points, true, state);
  QVector<OpSet> sets;
  if (!options.fill.isEmpty())
    sets.append(options.fillStyle == QLatin1String("solid")
                    ? solidFill({points}, state) : patternFill({points}, state));
  if (options.stroke != QLatin1String("none")) sets.append(outline);
  return {QStringLiteral("polygon"), sets, options};
}

Drawable ellipse(qreal x, qreal y, qreal width, qreal height, Options options) {
  State state{options};
  const EllipseParams params = ellipseParams(width, height, state);
  auto outline = ellipseWithParams(x, y, params, state);
  QVector<OpSet> sets;
  if (!options.fill.isEmpty()) {
    if (options.fillStyle == QLatin1String("solid")) {
      OpSet fill = ellipseWithParams(x, y, params, state).first;
      fill.type = OpSetType::FillPath;
      sets.append(fill);
    } else {
      sets.append(patternFill({outline.second}, state));
    }
  }
  if (options.stroke != QLatin1String("none")) sets.append(outline.first);
  return {QStringLiteral("ellipse"), sets, options};
}

Drawable arc(qreal x, qreal y, qreal width, qreal height,
             qreal start, qreal stop, bool closed, Options options) {
  State state{options};
  const OpSet outline = arcOutline(x, y, width, height, start, stop, closed, true, state);
  QVector<OpSet> sets;
  if (closed && !options.fill.isEmpty() && options.fillStyle == QLatin1String("solid")) {
    State fillState = state;
    fillState.options.disableMultiStroke = true;
    OpSet fill = arcOutline(x, y, width, height, start, stop, true, false, fillState);
    fill.type = OpSetType::FillPath;
    sets.append(fill);
  } else if (closed && !options.fill.isEmpty()) {
    sets.append(patternFillArc(x, y, width, height, start, stop, state));
  }
  if (options.stroke != QLatin1String("none")) sets.append(outline);
  return {QStringLiteral("arc"), sets, options};
}

Drawable path(const QPainterPath& source, Options options, bool closed) {
  State state{options};
  const QVector<QVector<QPointF>> pointSets =
      pointsOnPath(source, 1.0, (1.0 + options.roughness) / 2.0, closed);
  const OpSet outline = svgPath(source, state, closed);
  QVector<OpSet> sets;
  if (!options.fill.isEmpty() && options.fill != QLatin1String("transparent") &&
      options.fill != QLatin1String("none")) {
    if (options.fillStyle == QLatin1String("solid")) {
      if (pointSets.size() == 1) {
        State fillState = state;
        fillState.options.disableMultiStroke = true;
        fillState.options.roughness = options.roughness == 0 ? 0
            : options.roughness + options.fillShapeRoughnessGain;
        OpSet fill = svgPath(source, fillState, closed);
        fill.type = OpSetType::FillPath;
        bool first = true;
        fill.ops.erase(std::remove_if(fill.ops.begin(), fill.ops.end(), [&](const Op& op) {
                         if (first) { first = false; return false; }
                         return op.type == OpType::Move;
                       }), fill.ops.end());
        sets.append(fill);
      } else {
        sets.append(solidFill(pointSets, state));
      }
    } else {
      sets.append(patternFill(pointSets, state));
    }
  }
  if (options.stroke != QLatin1String("none")) sets.append(outline);
  return {QStringLiteral("path"), sets, options};
}

QPainterPath toPainterPath(const OpSet& set) {
  QPainterPath path;
  for (const Op& op : set.ops) {
    if (op.type == OpType::Move && op.data.size() == 2)
      path.moveTo(op.data[0], op.data[1]);
    else if (op.type == OpType::LineTo && op.data.size() == 2)
      path.lineTo(op.data[0], op.data[1]);
    else if (op.type == OpType::BcurveTo && op.data.size() == 6)
      path.cubicTo(op.data[0], op.data[1], op.data[2], op.data[3], op.data[4], op.data[5]);
  }
  return path;
}

namespace {

qreal cubicCoordinate(qreal p0, qreal p1, qreal p2, qreal p3, qreal t) {
  const qreal u = 1.0 - t;
  return u * u * u * p0 + 3.0 * u * u * t * p1 +
         3.0 * u * t * t * p2 + t * t * t * p3;
}

void includePoint(QRectF& bounds, bool& initialized, const QPointF& point) {
  if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return;
  if (!initialized) {
    bounds = QRectF(point, QSizeF());
    initialized = true;
    return;
  }
  const qreal left = std::min(bounds.left(), point.x());
  const qreal top = std::min(bounds.top(), point.y());
  const qreal right = std::max(bounds.right(), point.x());
  const qreal bottom = std::max(bounds.bottom(), point.y());
  bounds = QRectF(left, top, right - left, bottom - top);
}

void includeCubic(QRectF& bounds, bool& initialized, const QPointF& p0,
                  const QPointF& p1, const QPointF& p2,
                  const QPointF& p3) {
  includePoint(bounds, initialized, p0);
  includePoint(bounds, initialized, p3);
  auto includeRoots = [&](qreal v0, qreal v1, qreal v2, qreal v3) {
    const qreal a = -v0 + 3.0 * v1 - 3.0 * v2 + v3;
    const qreal b = 2.0 * (v0 - 2.0 * v1 + v2);
    const qreal c = v1 - v0;
    auto include = [&](qreal t) {
      if (!(t > 0.0 && t < 1.0)) return;
      includePoint(bounds, initialized,
                   QPointF(cubicCoordinate(p0.x(), p1.x(), p2.x(), p3.x(), t),
                           cubicCoordinate(p0.y(), p1.y(), p2.y(), p3.y(), t)));
    };
    if (std::abs(a) <= std::numeric_limits<qreal>::epsilon()) {
      if (std::abs(b) > std::numeric_limits<qreal>::epsilon()) include(-c / b);
      return;
    }
    const qreal discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return;
    const qreal root = std::sqrt(discriminant);
    include((-b + root) / (2.0 * a));
    if (root != 0.0) include((-b - root) / (2.0 * a));
  };
  includeRoots(p0.x(), p1.x(), p2.x(), p3.x());
  includeRoots(p0.y(), p1.y(), p2.y(), p3.y());
}

}  // namespace

QRectF tightBounds(const OpSet& set) {
  QRectF bounds;
  bool initialized = false;
  QPointF current;
  for (const Op& op : set.ops) {
    if (op.type == OpType::Move && op.data.size() == 2) {
      current = QPointF(op.data[0], op.data[1]);
      includePoint(bounds, initialized, current);
    } else if (op.type == OpType::LineTo && op.data.size() == 2) {
      current = QPointF(op.data[0], op.data[1]);
      includePoint(bounds, initialized, current);
    } else if (op.type == OpType::BcurveTo && op.data.size() == 6) {
      const QPointF control1(op.data[0], op.data[1]);
      const QPointF control2(op.data[2], op.data[3]);
      const QPointF end(op.data[4], op.data[5]);
      includeCubic(bounds, initialized, current, control1, control2, end);
      current = end;
    }
  }
  return bounds;
}

QRectF tightBounds(const Drawable& drawable) {
  QRectF bounds;
  bool initialized = false;
  for (const OpSet& set : drawable.sets) {
    const QRectF setBounds = tightBounds(set);
    if (!setBounds.isValid() && set.ops.isEmpty()) continue;
    includePoint(bounds, initialized, setBounds.topLeft());
    includePoint(bounds, initialized, setBounds.bottomRight());
  }
  return bounds;
}

QString opTypeName(OpType type) {
  if (type == OpType::Move) return QStringLiteral("move");
  if (type == OpType::LineTo) return QStringLiteral("lineTo");
  return QStringLiteral("bcurveTo");
}

QString opSetTypeName(OpSetType type) {
  if (type == OpSetType::FillPath) return QStringLiteral("fillPath");
  if (type == OpSetType::FillSketch) return QStringLiteral("fillSketch");
  return QStringLiteral("path");
}

}  // namespace muffin::mermaid::rough
