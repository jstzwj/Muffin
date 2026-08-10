// Flat-tree port derived from:
//   layout-base 2.0.1, cose-base 2.2.0 (Copyright i-Vis Research Group,
//   Bilkent University) and cytoscape-cose-bilkent 4.1.0.
// All three upstream packages are distributed under the MIT License.
#include "mermaid/mindmap/MindmapCoseLayout.h"

#include <bit>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace muffin::mermaid::mindmap {
namespace {

constexpr qreal kIdealEdgeLength = 50.0;
constexpr qreal kSpringStrength = 0.45;
constexpr qreal kRepulsionStrength = 4500.0;
constexpr qreal kRepulsionRange = 100.0;
constexpr qreal kMinRepulsionDistance = 5.0;
constexpr qreal kMaxNodeDisplacement = 300.0;
constexpr qreal kGraphMargin = 15.0;
constexpr int kMaxIterations = 2500;
constexpr int kConvergencePeriod = 100;

// V8 evaluates Math.sin/cos through fdlibm. The CoSE flat-tree seed positions
// are chaotic enough that a one-ulp platform-libm difference can select a
// different stable layout. These kernels and the medium-size pi/2 reduction
// cover exactly the radial seed's finite [0, 2*pi] input domain. They are
// adapted from Chromium's fdlibm fork; see THIRD_PARTY_NOTICES.md.
uint32_t highWord(double value) {
  return uint32_t(std::bit_cast<uint64_t>(value) >> 32);
}

double withWords(uint32_t high, uint32_t low = 0) {
  return std::bit_cast<double>((uint64_t(high) << 32) | uint64_t(low));
}

double fdlibmKernelSin(double x, double y, bool hasTail) {
  constexpr double half = 5.00000000000000000000e-01;
  constexpr double s1 = -1.66666666666666324348e-01;
  constexpr double s2 = 8.33333333332248946124e-03;
  constexpr double s3 = -1.98412698298579493134e-04;
  constexpr double s4 = 2.75573137070700676789e-06;
  constexpr double s5 = -2.50507602534068634195e-08;
  constexpr double s6 = 1.58969099521155010221e-10;
  const uint32_t ix = highWord(x) & 0x7fffffffu;
  if (ix < 0x3e400000u && int(x) == 0) return x;
  const double z = x * x;
  const double v = z * x;
  const double r = s2 + z * (s3 + z * (s4 + z * (s5 + z * s6)));
  if (!hasTail) return x + v * (s1 + z * r);
  return x - ((z * (half * y - v * r) - y) - v * s1);
}

double fdlibmKernelCos(double x, double y) {
  constexpr double one = 1.00000000000000000000e+00;
  constexpr double c1 = 4.16666666666666019037e-02;
  constexpr double c2 = -1.38888888888741095749e-03;
  constexpr double c3 = 2.48015872894767294178e-05;
  constexpr double c4 = -2.75573143513906633035e-07;
  constexpr double c5 = 2.08757232129817482790e-09;
  constexpr double c6 = -1.13596475577881948265e-11;
  const uint32_t ix = highWord(x) & 0x7fffffffu;
  if (ix < 0x3e400000u && int(x) == 0) return one;
  const double z = x * x;
  const double r = z * (c1 + z * (c2 + z * (c3 + z * (c4 + z * (c5 + z * c6)))));
  if (ix < 0x3fd33333u)
    return one - (0.5 * z - (z * r - x * y));
  const double qx = ix > 0x3fe90000u
      ? 0.28125
      : withWords(ix - 0x00200000u);
  const double iz = 0.5 * z - qx;
  const double a = one - qx;
  return a - (iz - (z * r - x * y));
}

int fdlibmReducePiOverTwo(double x, double (&y)[2]) {
  constexpr uint32_t npio2High[] = {
      0x3ff921fbu, 0x400921fbu, 0x4012d97cu, 0x401921fbu};
  constexpr double half = 5.00000000000000000000e-01;
  constexpr double invpio2 = 6.36619772367581382433e-01;
  constexpr double pio2_1 = 1.57079632673412561417e+00;
  constexpr double pio2_1t = 6.07710050650619224932e-11;
  constexpr double pio2_2 = 6.07710050630396597660e-11;
  constexpr double pio2_2t = 2.02226624879595063154e-21;
  constexpr double pio2_3 = 2.02226624871116645580e-21;
  constexpr double pio2_3t = 8.47842766036889956997e-32;

  const int32_t hx = int32_t(highWord(x));
  const uint32_t ix = uint32_t(hx) & 0x7fffffffu;
  if (ix <= 0x3fe921fbu) {
    y[0] = x;
    y[1] = 0.0;
    return 0;
  }
  if (ix < 0x4002d97cu) {
    double z;
    if (hx > 0) {
      z = x - pio2_1;
      if (ix != 0x3ff921fbu) {
        y[0] = z - pio2_1t;
        y[1] = (z - y[0]) - pio2_1t;
      } else {
        z -= pio2_2;
        y[0] = z - pio2_2t;
        y[1] = (z - y[0]) - pio2_2t;
      }
      return 1;
    }
    z = x + pio2_1;
    if (ix != 0x3ff921fbu) {
      y[0] = z + pio2_1t;
      y[1] = (z - y[0]) + pio2_1t;
    } else {
      z += pio2_2;
      y[0] = z + pio2_2t;
      y[1] = (z - y[0]) + pio2_2t;
    }
    return -1;
  }

  // Radial layout never exceeds 2*pi, so only fdlibm's medium path is
  // reachable here (n is at most four).
  double t = std::abs(x);
  const int n = int(t * invpio2 + half);
  const double fn = double(n);
  double r = t - fn * pio2_1;
  double w = fn * pio2_1t;
  if (ix != npio2High[n - 1]) {
    y[0] = r - w;
  } else {
    const int j = int(ix >> 20);
    y[0] = r - w;
    uint32_t high = highWord(y[0]);
    int i = j - int((high >> 20) & 0x7ffu);
    if (i > 16) {
      t = r;
      w = fn * pio2_2;
      r = t - w;
      w = fn * pio2_2t - ((t - r) - w);
      y[0] = r - w;
      high = highWord(y[0]);
      i = j - int((high >> 20) & 0x7ffu);
      if (i > 49) {
        t = r;
        w = fn * pio2_3;
        r = t - w;
        w = fn * pio2_3t - ((t - r) - w);
        y[0] = r - w;
      }
    }
  }
  y[1] = (r - y[0]) - w;
  if (hx < 0) {
    y[0] = -y[0];
    y[1] = -y[1];
    return -n;
  }
  return n;
}

double v8Sin(double x) {
  const uint32_t ix = highWord(x) & 0x7fffffffu;
  if (ix <= 0x3fe921fbu) return fdlibmKernelSin(x, 0.0, false);
  double y[2];
  const int n = fdlibmReducePiOverTwo(x, y);
  switch (n & 3) {
    case 0: return fdlibmKernelSin(y[0], y[1], true);
    case 1: return fdlibmKernelCos(y[0], y[1]);
    case 2: return -fdlibmKernelSin(y[0], y[1], true);
    default: return -fdlibmKernelCos(y[0], y[1]);
  }
}

double v8Cos(double x) {
  const uint32_t ix = highWord(x) & 0x7fffffffu;
  if (ix <= 0x3fe921fbu) return fdlibmKernelCos(x, 0.0);
  double y[2];
  const int n = fdlibmReducePiOverTwo(x, y);
  switch (n & 3) {
    case 0: return fdlibmKernelCos(y[0], y[1]);
    case 1: return -fdlibmKernelSin(y[0], y[1], true);
    case 2: return -fdlibmKernelCos(y[0], y[1]);
    default: return fdlibmKernelSin(y[0], y[1], true);
  }
}

struct Node {
  int id = -1;
  QSizeF size;
  QPointF topLeft;
  QVector<int> edges;
  QVector<int> surrounding;
  QPointF spring;
  QPointF repulsion;

  QPointF center() const {
    return {topLeft.x() + size.width() / 2.0,
            topLeft.y() + size.height() / 2.0};
  }
  void setCenter(const QPointF& value) {
    topLeft = {value.x() - size.width() / 2.0,
               value.y() - size.height() / 2.0};
  }
  void moveBy(const QPointF& delta) { topLeft += delta; }
};

struct Edge { int start = -1; int end = -1; };

QRectF rect(const Node& n) {
  return QRectF(n.topLeft, n.size);
}

bool inclusiveIntersects(const QRectF& a, const QRectF& b) {
  // layout-base RectangleD::intersects uses strict `<` separation tests, so
  // touching edges still count as overlap (QRectF::intersects does not).
  return !(a.right() < b.left() || a.bottom() < b.top() ||
           b.right() < a.left() || b.bottom() < a.top());
}

qreal sign(qreal v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

struct RectIntersection {
  QPointF a;
  QPointF b;
  bool overlapping = false;
};

int cardinalDirection(qreal slope, qreal lineSlope, int line) {
  return slope > lineSlope ? line : 1 + line % 4;
}

RectIntersection intersectRects(const QRectF& a, const QRectF& b) {
  RectIntersection result;
  const qreal p1x = a.center().x();
  const qreal p1y = a.center().y();
  const qreal p2x = b.center().x();
  const qreal p2y = b.center().y();
  if (inclusiveIntersects(a, b))
    return {{p1x, p1y}, {p2x, p2y}, true};

  if (p1x == p2x) {
    if (p1y > p2y) return {{p1x, a.top()}, {p2x, b.bottom()}, false};
    if (p1y < p2y) return {{p1x, a.bottom()}, {p2x, b.top()}, false};
  } else if (p1y == p2y) {
    if (p1x > p2x) return {{a.left(), p1y}, {b.right(), p2y}, false};
    if (p1x < p2x) return {{a.right(), p1y}, {b.left(), p2y}, false};
  } else {
    const qreal slopeA = a.height() / a.width();
    const qreal slopeB = b.height() / b.width();
    const qreal lineSlope = (p2y - p1y) / (p2x - p1x);
    bool foundA = false;
    bool foundB = false;

    if (-slopeA == lineSlope) {
      result.a = p1x > p2x ? QPointF(a.left(), a.bottom())
                            : QPointF(a.right(), a.top());
      foundA = true;
    } else if (slopeA == lineSlope) {
      result.a = p1x > p2x ? QPointF(a.left(), a.top())
                            : QPointF(a.right(), a.bottom());
      foundA = true;
    }
    if (-slopeB == lineSlope) {
      result.b = p2x > p1x ? QPointF(b.left(), b.bottom())
                            : QPointF(b.right(), b.top());
      foundB = true;
    } else if (slopeB == lineSlope) {
      result.b = p2x > p1x ? QPointF(b.left(), b.top())
                            : QPointF(b.right(), b.bottom());
      foundB = true;
    }
    if (foundA && foundB) return result;

    int directionA = 0;
    int directionB = 0;
    if (p1x > p2x) {
      if (p1y > p2y) {
        directionA = cardinalDirection(slopeA, lineSlope, 4);
        directionB = cardinalDirection(slopeB, lineSlope, 2);
      } else {
        directionA = cardinalDirection(-slopeA, lineSlope, 3);
        directionB = cardinalDirection(-slopeB, lineSlope, 1);
      }
    } else if (p1y > p2y) {
      directionA = cardinalDirection(-slopeA, lineSlope, 1);
      directionB = cardinalDirection(-slopeB, lineSlope, 3);
    } else {
      directionA = cardinalDirection(slopeA, lineSlope, 2);
      directionB = cardinalDirection(slopeB, lineSlope, 4);
    }

    if (!foundA) {
      switch (directionA) {
        case 1: result.a = {p1x + (-a.height() / 2.0) / lineSlope, a.top()}; break;
        case 2: result.a = {a.right(), p1y + a.width() / 2.0 * lineSlope}; break;
        case 3: result.a = {p1x + a.height() / 2.0 / lineSlope, a.bottom()}; break;
        case 4: result.a = {a.left(), p1y + (-a.width() / 2.0) * lineSlope}; break;
      }
    }
    if (!foundB) {
      switch (directionB) {
        case 1: result.b = {p2x + (-b.height() / 2.0) / lineSlope, b.top()}; break;
        case 2: result.b = {b.right(), p2y + b.width() / 2.0 * lineSlope}; break;
        case 3: result.b = {p2x + b.height() / 2.0 / lineSlope, b.bottom()}; break;
        case 4: result.b = {b.left(), p2y + (-b.width() / 2.0) * lineSlope}; break;
      }
    }
  }
  return result;
}

QPointF separationAmount(const QRectF& a, const QRectF& b) {
  qreal overlapX = std::min(a.right(), b.right()) - std::max(a.left(), b.left());
  qreal overlapY = std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top());
  if (a.left() <= b.left() && a.right() >= b.right())
    overlapX += std::min(b.left() - a.left(), a.right() - b.right());
  else if (b.left() <= a.left() && b.right() >= a.right())
    overlapX += std::min(a.left() - b.left(), b.right() - a.right());
  if (a.top() <= b.top() && a.bottom() >= b.bottom())
    overlapY += std::min(b.top() - a.top(), a.bottom() - b.bottom());
  else if (b.top() <= a.top() && b.bottom() >= a.bottom())
    overlapY += std::min(a.top() - b.top(), b.bottom() - a.bottom());

  const qreal dx = b.center().x() - a.center().x();
  const qreal dy = b.center().y() - a.center().y();
  qreal slope = std::abs(dy / dx);
  if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) slope = 1.0;
  qreal moveY = slope * overlapX;
  qreal moveX = overlapY / slope;
  if (overlapX < moveX) moveX = overlapX;
  else moveY = overlapY;
  const qreal directionX = a.center().x() < b.center().x() ? -1.0 : 1.0;
  const qreal directionY = a.center().y() < b.center().y() ? -1.0 : 1.0;
  return QPointF(-directionX * (moveX / 2.0 + kIdealEdgeLength / 2.0),
                 -directionY * (moveY / 2.0 + kIdealEdgeLength / 2.0));
}

int otherEnd(const Edge& e, int n) { return e.start == n ? e.end : e.start; }

int treeCenter(const QVector<Node>& nodes, const QVector<Edge>& edges) {
  if (nodes.size() <= 2) return 0;
  // Preserve layout-base 2.0.1's observable splice-while-iterating behavior.
  // It intentionally does not implement the mathematical tree centre: for a
  // three-node star it returns the first leaf. CoSE's radial orientation and
  // the deterministic proof layout depend on that exact choice.
  QVector<int> list;
  QVector<bool> visited(nodes.size(), false);
  QVector<int> queue{0};
  visited[0] = true;
  while (!queue.isEmpty()) {
    const int node = queue.takeFirst();
    list.append(node);
    for (int edgeIndex : nodes[node].edges) {
      const int neighbor = otherEnd(edges[edgeIndex], node);
      if (!visited[neighbor]) {
        visited[neighbor] = true;
        queue.append(neighbor);
      }
    }
  }
  // getFlatForest exposes the BFS Set insertion order. A connected mindmap
  // visits every node; retain insertion order defensively for malformed input.
  for (int i = 0; i < nodes.size(); ++i)
    if (!visited[i]) list.append(i);
  QVector<int> removed;
  QVector<int> degree(nodes.size());
  QVector<int> pending;
  for (int i = 0; i < nodes.size(); ++i) {
    degree[i] = nodes[i].edges.size();
    if (degree[i] == 1) removed.append(i);
  }
  pending = removed;
  while (list.size() > 2) {
    const QVector<int> previousPending = pending;
    Q_UNUSED(previousPending);
    pending.clear();
    // Upstream indexes the shrinking list directly, thereby visiting every
    // other entry in this pass.
    for (int i = 0; i < list.size(); ++i) {
      const int node = list.at(i);
      list.removeAt(i);
      for (int edgeIndex : nodes[node].edges) {
        const int neighbor = otherEnd(edges[edgeIndex], node);
        if (removed.contains(neighbor)) continue;
        const int nextDegree = degree[neighbor] - 1;
        if (nextDegree == 1) pending.append(neighbor);
        degree[neighbor] = nextDegree;
      }
    }
    removed += pending;
    if (list.size() <= 2) break;
  }
  return list.isEmpty() ? 0 : list.first();
}

void radialBranch(QVector<Node>& nodes, const QVector<Edge>& edges, int node,
                  int parent, qreal startAngle, qreal endAngle, qreal distance,
                  qreal radialSeparation) {
  qreal halfInterval = ((endAngle - startAngle) + 1.0) / 2.0;
  if (halfInterval < 0) halfInterval += 180.0;
  const qreal angle = std::fmod(halfInterval + startAngle, 360.0);
  const qreal theta = angle * 2.0 * std::numbers::pi_v<qreal> / 360.0;
  nodes[node].setCenter(QPointF(distance * v8Cos(theta),
                                distance * v8Sin(theta)));

  QVector<int> neighborEdges = nodes[node].edges;
  int childCount = neighborEdges.size() - (parent >= 0 ? 1 : 0);
  if (childCount <= 0) return;
  int startIndex = 0;
  if (parent >= 0) {
    int parentEdge = -1;
    for (int i = 0; i < neighborEdges.size(); ++i)
      if (otherEnd(edges[neighborEdges[i]], node) == parent) { parentEdge = i; break; }
    startIndex = (parentEdge + 1) % neighborEdges.size();
  }
  const qreal step = std::abs(endAngle - startAngle) / childCount;
  int branch = 0;
  for (int i = startIndex; branch != childCount;
       i = (i + 1) % neighborEdges.size()) {
    const int neighbor = otherEnd(edges[neighborEdges[i]], node);
    if (neighbor == parent) continue;
    const qreal childStart = std::fmod(startAngle + branch * step, 360.0);
    const qreal childEnd = std::fmod(childStart + step, 360.0);
    radialBranch(nodes, edges, neighbor, node, childStart, childEnd,
                 distance + radialSeparation, radialSeparation);
    ++branch;
  }
}

QRectF nodeBounds(const QVector<Node>& nodes) {
  if (nodes.isEmpty()) return {};
  qreal left = std::numeric_limits<qreal>::max();
  qreal right = -std::numeric_limits<qreal>::max();
  qreal top = std::numeric_limits<qreal>::max();
  qreal bottom = -std::numeric_limits<qreal>::max();
  for (const Node& n : nodes) {
    const qreal nodeLeft = n.topLeft.x();
    const qreal nodeRight = n.topLeft.x() + n.size.width();
    const qreal nodeTop = n.topLeft.y();
    const qreal nodeBottom = n.topLeft.y() + n.size.height();
    if (left > nodeLeft) left = nodeLeft;
    if (right < nodeRight) right = nodeRight;
    if (top > nodeTop) top = nodeTop;
    if (bottom < nodeBottom) bottom = nodeBottom;
  }
  // LGraph.calculateBounds stores RectangleD(left, top, right-left,
  // bottom-top). Callers later recover max as x+width, so preserving this
  // two-step rounding is significant to CoSE's radial world transform.
  return QRectF(left, top, right - left, bottom - top);
}

void updateSurrounding(QVector<Node>& nodes) {
  if (nodes.isEmpty()) return;
  QRectF graphBounds = nodeBounds(nodes).adjusted(-kGraphMargin, -kGraphMargin,
                                                   kGraphMargin, kGraphMargin);
  const int columns = std::max(1, int(std::ceil(graphBounds.width() /
                                                kRepulsionRange)));
  const int rows = std::max(1, int(std::ceil(graphBounds.height() /
                                             kRepulsionRange)));
  QVector<QVector<QVector<int>>> grid(
      columns, QVector<QVector<int>>(rows));
  struct GridSpan { int sx=0, fx=0, sy=0, fy=0; };
  QVector<GridSpan> spans(nodes.size());
  for (int index = 0; index < nodes.size(); ++index) {
    const QRectF r = rect(nodes[index]);
    GridSpan span;
    span.sx = int(std::floor((r.left() - graphBounds.left()) / kRepulsionRange));
    span.fx = int(std::floor((r.right() - graphBounds.left()) / kRepulsionRange));
    span.sy = int(std::floor((r.top() - graphBounds.top()) / kRepulsionRange));
    span.fy = int(std::floor((r.bottom() - graphBounds.top()) / kRepulsionRange));
    span.sx = std::clamp(span.sx, 0, columns - 1);
    span.fx = std::clamp(span.fx, 0, columns - 1);
    span.sy = std::clamp(span.sy, 0, rows - 1);
    span.fy = std::clamp(span.fy, 0, rows - 1);
    spans[index] = span;
    for (int x = span.sx; x <= span.fx; ++x)
      for (int y = span.sy; y <= span.fy; ++y)
        grid[x][y].append(index);
  }

  QVector<bool> processed(nodes.size(), false);
  for (int a = 0; a < nodes.size(); ++a) {
    nodes[a].surrounding.clear();
    QVector<bool> seen(nodes.size(), false);
    const GridSpan span = spans[a];
    for (int x = span.sx - 1; x < span.fx + 2; ++x) {
      for (int y = span.sy - 1; y < span.fy + 2; ++y) {
        if (x < 0 || y < 0 || x >= columns || y >= rows) continue;
        for (int b : std::as_const(grid[x][y])) {
          if (a == b || processed[b] || seen[b]) continue;
          const qreal gapX = std::abs(nodes[a].center().x() - nodes[b].center().x())
              - (nodes[a].size.width() + nodes[b].size.width()) / 2.0;
          const qreal gapY = std::abs(nodes[a].center().y() - nodes[b].center().y())
              - (nodes[a].size.height() + nodes[b].size.height()) / 2.0;
          if (gapX <= kRepulsionRange && gapY <= kRepulsionRange) {
            seen[b] = true;
            nodes[a].surrounding.append(b);
          }
        }
      }
    }
    processed[a] = true;
  }
}

void springForces(QVector<Node>& nodes, const QVector<Edge>& edges) {
  for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
    const Edge& e = edges.at(edgeIndex);
    Node& source = nodes[e.start];
    Node& target = nodes[e.end];
    const QRectF a = rect(source), b = rect(target);
    if (inclusiveIntersects(a, b)) continue;
    // LEdge.updateLength calls getIntersection(target, source), then takes
    // result[A] - result[B]. Preserve that order for floating-point parity.
    const RectIntersection clips = intersectRects(b, a);
    QPointF delta = clips.a - clips.b;
    if (std::abs(delta.x()) < 1.0) delta.setX(sign(delta.x()));
    if (std::abs(delta.y()) < 1.0) delta.setY(sign(delta.y()));
    const qreal length = std::sqrt(delta.x() * delta.x()
                                   + delta.y() * delta.y());
    if (qFuzzyIsNull(length)) continue;
    const qreal magnitude = kSpringStrength * (length - kIdealEdgeLength);
    const QPointF force(magnitude * (delta.x() / length),
                        magnitude * (delta.y() / length));
    source.spring += force;
    target.spring -= force;
  }
}

void repulsionForces(QVector<Node>& nodes) {
  for (int i = 0; i < nodes.size(); ++i) {
    for (int j : std::as_const(nodes[i].surrounding)) {
      Node& a = nodes[i]; Node& b = nodes[j];
      const QRectF ar = rect(a), br = rect(b);
      QPointF force;
      if (inclusiveIntersects(ar, br)) {
        // Simple nodes have noOfChildren==1. Upstream multiplies the half
        // separation by two, then by 1*1/(1+1), so each endpoint receives
        // exactly the half separation amount.
        force = separationAmount(ar, br);
      } else {
        const RectIntersection clips = intersectRects(ar, br);
        QPointF delta = clips.b - clips.a;
        if (std::abs(delta.x()) < kMinRepulsionDistance)
          delta.setX(sign(delta.x()) * kMinRepulsionDistance);
        if (std::abs(delta.y()) < kMinRepulsionDistance)
          delta.setY(sign(delta.y()) * kMinRepulsionDistance);
        const qreal d2 = QPointF::dotProduct(delta, delta);
        const qreal d = std::sqrt(d2);
        const qreal magnitude = kRepulsionStrength / d2;
        force = QPointF(magnitude * delta.x() / d,
                        magnitude * delta.y() / d);
      }
      a.repulsion -= force;
      b.repulsion += force;
    }
  }
}

}  // namespace

MindmapCoseResult layoutMindmapCoseFlatTree(
    const QVector<MindmapCoseNodeInput>& inputNodes,
    const QVector<MindmapCoseEdgeInput>& inputEdges) {
  MindmapCoseResult result;
  if (inputNodes.isEmpty()) return result;
  QVector<Node> nodes;
  nodes.reserve(inputNodes.size());
  for (const auto& in : inputNodes) nodes.append({in.id, in.size});
  QVector<Edge> edges;
  for (const auto& in : inputEdges) {
    if (in.start < 0 || in.end < 0 || in.start >= nodes.size() || in.end >= nodes.size())
      continue;
    const int edgeIndex = edges.size();
    edges.append({in.start, in.end});
    nodes[in.start].edges.append(edgeIndex);
    nodes[in.end].edges.append(edgeIndex);
  }

  qreal radialSeparation = kIdealEdgeLength;
  for (const Node& n : nodes)
    radialSeparation = std::max(
        radialSeparation,
        std::sqrt(n.size.width() * n.size.width()
                  + n.size.height() * n.size.height()));
  radialBranch(nodes, edges, treeCenter(nodes, edges), -1, 0.0, 359.0, 0.0,
               radialSeparation);
  QRectF radialBounds = nodeBounds(nodes);
  // positionNodesRadially first maps the component to (0,0), then transforms
  // the complete forest around layout-base's fixed world centre. Besides
  // translation invariance this is numerically observable: at y~=900 the
  // tiny sin(pi) residue rounds away, keeping a horizontal radial branch
  // exactly horizontal during force calculation.
  // The root graph's bounds already include DEFAULT_GRAPH_MARGIN when
  // Layout.transform maps the tiled forest into the fixed world. Keeping
  // that +15 offset is numerically observable for nominally horizontal
  // radial edges: JS's tiny sin(pi) residue is subsequently promoted to
  // +/-1 by LEdge.updateLength's sub-pixel clamp.
  // radialLayout first maps its component to (0,0). Layout.transform then
  // performs a second translation around the fixed world centre, with the
  // root graph's -15 margin as the device origin. Keep both assignments:
  // algebraically folding them changes the final ulps and can send CoSE's
  // proof embedder into another stable basin.
  for (Node& n : nodes)
    n.topLeft = QPointF(0.0, 0.0) +
        (n.topLeft - radialBounds.topLeft());
  // radialLayout returns inverseTransformPoint(bounds.getMaxX/Y()). With the
  // unit transform this is (bounds.x + bounds.width) - bounds.x, not the
  // stored width directly; the extra add/subtract is observable in JS.
  const QPointF componentExtent(radialBounds.right() - radialBounds.left(),
                                radialBounds.bottom() - radialBounds.top());
  const QPointF worldOrigin(1200.0 - componentExtent.x() / 2.0,
                            900.0 - componentExtent.y() / 2.0);
  const QPointF graphLeftTop(-kGraphMargin, -kGraphMargin);
  for (Node& n : nodes)
    n.topLeft = worldOrigin + (n.topLeft - graphLeftTop);
  qreal cooling = 1.0;
  const qreal finalTemperature = qreal(kConvergencePeriod) / kMaxIterations;
  const qreal maxCoolingCycle = qreal(kMaxIterations) / kConvergencePeriod;
  qreal totalDisplacement = 0.0;
  qreal oldDisplacement = 0.0;
  int coolingCycle = 0;
  for (int iteration = 1; iteration < kMaxIterations; ++iteration) {
    // CoSELayout.tick performs convergence/cooling immediately after
    // totalIterations++, using the displacement produced by the PREVIOUS tick.
    if (iteration % kConvergencePeriod == 0) {
      const bool oscillating = iteration > kMaxIterations / 3
          && std::abs(totalDisplacement - oldDisplacement) < 2.0;
      const bool converged = totalDisplacement < 1.5 * nodes.size();
      oldDisplacement = totalDisplacement;
      if (converged || oscillating) {
        break;
      }
      ++coolingCycle;
      const qreal exponent = std::log(100.0 * (1.0 - finalTemperature))
          / std::log(maxCoolingCycle);
      cooling = std::max(1.0 - std::pow(qreal(coolingCycle), exponent) / 100.0,
                         finalTemperature);
    }
    if (iteration % 10 == 1) {
      updateSurrounding(nodes);
    }
    for (Node& n : nodes) { n.spring = {}; n.repulsion = {}; }
    springForces(nodes, edges);
    repulsionForces(nodes);
    totalDisplacement = 0.0;
    for (Node& n : nodes) {
      QPointF displacement = (n.spring + n.repulsion) * cooling;
      const qreal cap = cooling * kMaxNodeDisplacement;
      displacement.setX(std::clamp(displacement.x(), -cap, cap));
      displacement.setY(std::clamp(displacement.y(), -cap, cap));
      n.moveBy(displacement);
      totalDisplacement += std::abs(displacement.x()) + std::abs(displacement.y());
    }
  }

  const QRectF finalBounds = nodeBounds(nodes);
  result.centers.reserve(nodes.size());
  for (const Node& n : nodes)
    result.centers.append(n.center() - finalBounds.topLeft()
                          + QPointF(kGraphMargin, kGraphMargin));
  return result;
}

}  // namespace muffin::mermaid::mindmap
