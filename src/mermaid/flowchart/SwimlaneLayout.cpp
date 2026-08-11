#include "mermaid/flowchart/SwimlaneLayout.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/rough/RoughOps.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>

namespace muffin::mermaid::flowchart {
namespace {

constexpr qreal kEps = 1e-6;
constexpr qreal kLaneTitleHeight = 21.0;
constexpr qreal kMinLanePadding = 20.0;
constexpr qreal kRouterPadding = 8.0;
constexpr qreal kRoundedCornerRadius = 5.0;
constexpr qreal kCornerEpsilon = 1e-5;
constexpr qreal kMinimumJumpRadius = 1e-3;

struct LineJumpCrossing {
  int edgeIndex = -1;
  int segmentIndex = -1;
  qreal t = 0.0;
  QPointF point;
};

struct RoundedCorner {
  QPointF start;
  QPointF end;
  QPointF control;
  qreal cutLength = 0.0;
};

QString jumpNumber(qreal value) {
  qreal rounded = std::floor(value * 1000.0 + 0.5) / 1000.0;
  if (std::abs(rounded) < 0.0005) rounded = 0.0;
  QString text = QString::number(rounded, 'f', 3);
  while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
    text.chop(1);
  if (text.endsWith(QLatin1Char('.'))) text.chop(1);
  return text;
}

QString jumpPoint(const QPointF& point) {
  return jumpNumber(point.x()) + QLatin1Char(',') + jumpNumber(point.y());
}

bool horizontalSegment(const QPointF& a, const QPointF& b) {
  return std::abs(b.x() - a.x()) >= std::abs(b.y() - a.y());
}

std::optional<std::tuple<QPointF, qreal, qreal>> segmentIntersection(
    const QPointF& a1, const QPointF& a2,
    const QPointF& b1, const QPointF& b2) {
  const qreal dxA = a2.x() - a1.x();
  const qreal dyA = a2.y() - a1.y();
  const qreal dxB = b2.x() - b1.x();
  const qreal dyB = b2.y() - b1.y();
  const qreal denominator = dxA * dyB - dyA * dxB;
  if (denominator == 0.0) return std::nullopt;
  const qreal dx = b1.x() - a1.x();
  const qreal dy = b1.y() - a1.y();
  const qreal tA = (dx * dyB - dy * dxB) / denominator;
  const qreal tB = (dx * dyA - dy * dxA) / denominator;
  if (tA <= kEps || tA >= 1.0 - kEps ||
      tB <= kEps || tB >= 1.0 - kEps)
    return std::nullopt;
  return std::tuple<QPointF, qreal, qreal>(
      QPointF(a1.x() + tA * dxA, a1.y() + tA * dyA), tA, tB);
}

QVector<LineJumpCrossing> lineJumpCrossings(
    const QVector<FlowLayoutEdge>& edges) {
  QVector<LineJumpCrossing> crossings;
  for (int i = 0; i < edges.size(); ++i) {
    const QVector<QPointF>& a = edges.at(i).points;
    for (int j = i + 1; j < edges.size(); ++j) {
      const QVector<QPointF>& b = edges.at(j).points;
      for (int ai = 0; ai + 1 < a.size(); ++ai) {
        for (int bi = 0; bi + 1 < b.size(); ++bi) {
          const auto hit = segmentIntersection(
              a.at(ai), a.at(ai + 1), b.at(bi), b.at(bi + 1));
          if (!hit) continue;
          const auto& [point, tA, tB] = *hit;
          const bool aHorizontal = horizontalSegment(a.at(ai), a.at(ai + 1));
          const bool bHorizontal = horizontalSegment(b.at(bi), b.at(bi + 1));
          if (aHorizontal != bHorizontal && aHorizontal)
            crossings.append({i, ai, tA, point});
          else
            crossings.append({j, bi, tB, point});
        }
      }
    }
  }
  return crossings;
}

std::optional<RoundedCorner> roundedCorner(
    const QPointF& previous, const QPointF& current,
    const QPointF& next) {
  const QPointF incoming = current - previous;
  const QPointF outgoing = next - current;
  const qreal incomingLength = std::hypot(incoming.x(), incoming.y());
  const qreal outgoingLength = std::hypot(outgoing.x(), outgoing.y());
  if (incomingLength < kCornerEpsilon || outgoingLength < kCornerEpsilon)
    return std::nullopt;
  const QPointF inUnit = incoming / incomingLength;
  const QPointF outUnit = outgoing / outgoingLength;
  const qreal dot = std::clamp(
      QPointF::dotProduct(inUnit, outUnit), -1.0, 1.0);
  const qreal angle = std::acos(dot);
  if (angle < kCornerEpsilon ||
      std::abs(M_PI - angle) < kCornerEpsilon)
    return std::nullopt;
  const qreal cut = std::min({
      kRoundedCornerRadius / std::sin(angle / 2.0),
      incomingLength / 2.0, outgoingLength / 2.0});
  return RoundedCorner{
      current - inUnit * cut, current + outUnit * cut, current, cut};
}

bool curveSupportsLineHops(const QString& curve) {
  return curve == QLatin1String("linear") ||
         curve == QLatin1String("rounded") ||
         curve == QLatin1String("step") ||
         curve == QLatin1String("stepBefore") ||
         curve == QLatin1String("stepAfter");
}

QString rewriteLineJumpPath(
    const FlowLayoutEdge& edge, const FlowEdge& semantic,
    const QString& curve, const QVector<LineJumpCrossing>& crossings,
    bool gapStyle) {
  QVector<QPointF> points = edge.points;
  clipFlowEdgeForMarkers(points, semantic.type);
  if (points.size() < 2) return {};

  struct Jump {
    qreal t = 0.0;
    QPointF point;
    qreal distance = 0.0;
    qreal radius = 6.0;
  };
  QHash<int, QVector<Jump>> bySegment;
  for (const LineJumpCrossing& crossing : crossings) {
    if (crossing.segmentIndex < 0 ||
        crossing.segmentIndex + 1 >= points.size())
      continue;
    const QPointF a = points.at(crossing.segmentIndex);
    const QPointF b = points.at(crossing.segmentIndex + 1);
    const qreal length = std::hypot(b.x() - a.x(), b.y() - a.y());
    bySegment[crossing.segmentIndex].append(
        {crossing.t, crossing.point, crossing.t * length, 6.0});
  }

  const bool rounded = curve == QLatin1String("rounded");
  QStringList parts{QStringLiteral("M") + jumpPoint(points.first())};
  for (int i = 0; i + 1 < points.size(); ++i) {
    const QPointF a = points.at(i);
    const QPointF b = points.at(i + 1);
    const QPointF delta = b - a;
    const qreal length = std::hypot(delta.x(), delta.y());
    const QPointF unit = length == 0.0 ? QPointF() : delta / length;
    const int sweep = horizontalSegment(a, b)
        ? (delta.x() >= 0.0 ? 1 : 0)
        : (delta.y() >= 0.0 ? 1 : 0);

    qreal consumedStart = 0.0;
    if (rounded && i > 0) {
      const auto corner = roundedCorner(points.at(i - 1), a, b);
      if (corner) consumedStart = corner->cutLength;
    }
    qreal segmentEnd = length;
    std::optional<RoundedCorner> upcoming;
    if (rounded && i + 2 < points.size()) {
      upcoming = roundedCorner(a, b, points.at(i + 2));
      if (upcoming) segmentEnd = length - upcoming->cutLength;
    }

    QVector<Jump> jumps = bySegment.value(i);
    std::sort(jumps.begin(), jumps.end(),
              [](const Jump& left, const Jump& right) {
                return left.t < right.t;
              });
    for (Jump& jump : jumps)
      jump.radius = std::min({jump.radius,
                              jump.distance - consumedStart,
                              segmentEnd - jump.distance});
    for (int j = 0; j + 1 < jumps.size(); ++j) {
      const qreal distance = jumps.at(j + 1).distance - jumps.at(j).distance;
      if (jumps.at(j).radius + jumps.at(j + 1).radius > distance) {
        jumps[j].radius = std::min(jumps.at(j).radius, distance / 2.0);
        jumps[j + 1].radius =
            std::min(jumps.at(j + 1).radius, distance / 2.0);
      }
    }
    for (const Jump& jump : jumps) {
      if (jump.radius < kMinimumJumpRadius) continue;
      const QPointF before = jump.point - unit * jump.radius;
      const QPointF after = jump.point + unit * jump.radius;
      parts.append(QStringLiteral("L") + jumpPoint(before));
      if (gapStyle)
        parts.append(QStringLiteral("M") + jumpPoint(after));
      else
        parts.append(QStringLiteral("A%1,%1 0 0 %2 %3")
                         .arg(jumpNumber(jump.radius))
                         .arg(sweep)
                         .arg(jumpPoint(after)));
    }
    if (rounded && upcoming) {
      parts.append(QStringLiteral("L") + jumpPoint(upcoming->start));
      parts.append(QStringLiteral("Q%1 %2")
                       .arg(jumpPoint(upcoming->control),
                            jumpPoint(upcoming->end)));
    } else {
      parts.append(QStringLiteral("L") + jumpPoint(b));
    }
  }
  return parts.join(QLatin1Char(' '));
}

void applyLineJumps(FlowLayoutResult& result, const FlowchartData& data,
                    const SwimlaneLayoutOptions& options) {
  if (options.lineHops == QLatin1String("false")) return;
  const QVector<LineJumpCrossing> crossings = lineJumpCrossings(result.edges);
  if (crossings.isEmpty()) return;

  QHash<int, QVector<LineJumpCrossing>> byEdge;
  for (const LineJumpCrossing& crossing : crossings)
    byEdge[crossing.edgeIndex].append(crossing);
  QHash<QString, const FlowEdge*> semantics;
  for (const FlowEdge& edge : data.edges) semantics.insert(edge.id, &edge);

  for (auto it = byEdge.cbegin(); it != byEdge.cend(); ++it) {
    if (it.key() < 0 || it.key() >= result.edges.size()) continue;
    FlowLayoutEdge& edge = result.edges[it.key()];
    const FlowEdge* semantic = semantics.value(edge.id);
    if (!semantic) continue;
    QString curve = semantic->interpolate.isEmpty()
        ? options.curve : semantic->interpolate;
    if (curve.isEmpty() || curve == QLatin1String("basis"))
      curve = QStringLiteral("rounded");
    if (!curveSupportsLineHops(curve)) continue;
    edge.path = rewriteLineJumpPath(
        edge, *semantic, curve, it.value(),
        options.lineHops == QLatin1String("gap"));
  }
}

struct Node {
  QString id;
  QString parent;
  QSizeF size = QSizeF(0.0, 0.0);
  QSizeF renderSize = QSizeF(0.0, 0.0);
  bool group = false;
  bool dummy = false;
  int rank = 0;
  int order = 0;
  QPointF center;
  qreal padding = 20.0;
  qreal contentTop = 0.0;
};

struct EdgeRef {
  int sourceIndex = -1;
  QString id;
  QString src;
  QString dst;
  bool reversed = false;
};

struct Graph {
  QVector<QString> order;
  QHash<QString, Node> nodes;
  QVector<EdgeRef> edges;
};

QStringList sorted(QStringList values) {
  std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) {
    return a.localeAwareCompare(b) < 0;
  });
  return values;
}

QStringList asStringList(const QVector<QString>& values) {
  QStringList result;
  result.reserve(values.size());
  for (const QString& value : values) result.push_back(value);
  return result;
}

QString topLane(const Graph& graph, const QString& id) {
  QString current = id;
  QString lane;
  QSet<QString> seen;
  while (graph.nodes.contains(current) && !seen.contains(current)) {
    seen.insert(current);
    const QString parent = graph.nodes.value(current).parent;
    if (parent.isEmpty()) break;
    lane = parent;
    current = parent;
  }
  return lane;
}

QVector<EdgeRef> incoming(const Graph& graph, const QString& id) {
  QVector<EdgeRef> out;
  for (const EdgeRef& edge : graph.edges)
    if (edge.dst == id) out.push_back(edge);
  return out;
}

QStringList topologicalOrder(const Graph& graph) {
  QHash<QString, int> indegree;
  QHash<QString, QStringList> successors;
  for (const QString& id : graph.order) {
    indegree.insert(id, 0);
    successors.insert(id, {});
  }
  for (const EdgeRef& edge : graph.edges) {
    if (!indegree.contains(edge.src) || !indegree.contains(edge.dst)) continue;
    ++indegree[edge.dst];
    successors[edge.src].push_back(edge.dst);
  }
  for (auto it = successors.begin(); it != successors.end(); ++it)
    it.value() = sorted(it.value());
  QStringList queue;
  for (auto it = indegree.cbegin(); it != indegree.cend(); ++it)
    if (it.value() == 0) queue.push_back(it.key());
  queue = sorted(queue);
  QStringList result;
  while (!queue.isEmpty()) {
    const QString id = queue.takeFirst();
    result.push_back(id);
    for (const QString& next : successors.value(id)) {
      const int value = --indegree[next];
      if (value == 0) {
        queue.push_back(next);
        queue = sorted(queue);
      }
    }
  }
  return result.size() == graph.order.size() ? result : sorted(asStringList(graph.order));
}

void removeCycles(Graph& graph) {
  QHash<QString, int> color;
  QHash<QString, QVector<int>> outgoingEdges;
  for (const QString& id : graph.order) color.insert(id, 0);
  for (int i = 0; i < graph.edges.size(); ++i)
    outgoingEdges[graph.edges.at(i).src].push_back(i);
  for (auto it = outgoingEdges.begin(); it != outgoingEdges.end(); ++it) {
    std::sort(it.value().begin(), it.value().end(), [&](int a, int b) {
      const EdgeRef& ea = graph.edges.at(a);
      const EdgeRef& eb = graph.edges.at(b);
      const int dst = ea.dst.localeAwareCompare(eb.dst);
      return dst != 0 ? dst < 0 : ea.id.localeAwareCompare(eb.id) < 0;
    });
  }
  QVector<int> reverse;
  std::function<void(const QString&)> visit = [&](const QString& id) {
    color[id] = 1;
    for (int edgeIndex : outgoingEdges.value(id)) {
      const QString dst = graph.edges.at(edgeIndex).dst;
      if (color.value(dst) == 0) visit(dst);
      else if (color.value(dst) == 1) reverse.push_back(edgeIndex);
    }
    color[id] = 2;
  };
  for (const QString& id : sorted(asStringList(graph.order)))
    if (color.value(id) == 0) visit(id);
  for (int index : reverse) {
    std::swap(graph.edges[index].src, graph.edges[index].dst);
    graph.edges[index].reversed = true;
  }
}

QStringList sourceLaneOrder(const Graph& graph) {
  QStringList lanes;
  for (auto it = graph.order.crbegin(); it != graph.order.crend(); ++it) {
    const Node& node = graph.nodes.value(*it);
    if (node.group && node.parent.isEmpty() && !lanes.contains(node.id))
      lanes.push_back(node.id);
  }
  return lanes;
}

quint32 laneOrderHash(const QString& text) {
  quint32 hash = 2166136261u;
  for (const QChar ch : text) {
    hash ^= ch.unicode();
    hash *= 16777619u;
  }
  return hash;
}

qreal laneArrangementCost(const QStringList& order,
                          const QHash<QString, int>& weights) {
  QHash<QString, int> position;
  for (int i = 0; i < order.size(); ++i) position.insert(order.at(i), i);
  qreal cost = 0.0;
  for (auto it = weights.cbegin(); it != weights.cend(); ++it) {
    const QStringList pair = it.key().split(QChar(0));
    if (pair.size() != 2 || !position.contains(pair.at(0)) ||
        !position.contains(pair.at(1)))
      continue;
    cost += it.value() * std::abs(position.value(pair.at(0)) -
                                  position.value(pair.at(1)));
  }
  return cost;
}

QStringList optimizeLaneOrder(const Graph& graph) {
  const QStringList source = sourceLaneOrder(graph);
  if (source.size() < 2) return source;
  QHash<QString, int> sourceIndex;
  for (int i = 0; i < source.size(); ++i) sourceIndex.insert(source.at(i), i);
  QHash<QString, int> weights;
  for (const EdgeRef& edge : graph.edges) {
    if (edge.sourceIndex < 0) continue;
    QString a = topLane(graph, edge.src);
    QString b = topLane(graph, edge.dst);
    if (a.isEmpty() || b.isEmpty() || a == b ||
        !sourceIndex.contains(a) || !sourceIndex.contains(b))
      continue;
    if (sourceIndex.value(a) > sourceIndex.value(b)) std::swap(a, b);
    ++weights[a + QChar(0) + b];
  }
  if (weights.isEmpty()) return source;

  const auto sourceDistance = [&](const QStringList& order) {
    int distance = 0;
    for (int i = 0; i < order.size(); ++i)
      distance += std::abs(i - sourceIndex.value(order.at(i), i));
    return distance;
  };
  const auto improve = [&](QStringList order) {
    qreal cost = laneArrangementCost(order, weights);
    bool changed = true;
    int sweeps = 0;
    while (changed && sweeps++ < std::max<qsizetype>(1, order.size())) {
      changed = false;
      for (int i = 0; i + 1 < order.size(); ++i) {
        order.swapItemsAt(i, i + 1);
        const qreal next = laneArrangementCost(order, weights);
        if (next < cost) {
          cost = next;
          changed = true;
        } else {
          order.swapItemsAt(i, i + 1);
        }
      }
    }
    return std::tuple<QStringList, qreal, int>(order, cost,
                                               sourceDistance(order));
  };

  auto best = improve(source);
  QStringList signatureParts;
  QStringList weightKeys = weights.keys();
  std::sort(weightKeys.begin(), weightKeys.end());
  for (const QString& key : weightKeys) {
    const QStringList pair = key.split(QChar(0));
    signatureParts.push_back(pair.at(0) + QLatin1Char(':') + pair.at(1) +
                             QLatin1Char(':') + QString::number(weights.value(key)));
  }
  for (int restart = 0; restart < 8; ++restart) {
    quint32 state = laneOrderHash(source.join(QLatin1Char('|')) + QLatin1Char('#') +
                                  signatureParts.join(QLatin1Char('|')) +
                                  QLatin1Char('#') + QString::number(restart));
    auto random = [&]() {
      state += 0x6d2b79f5u;
      quint32 t = state;
      t = (t ^ (t >> 15)) * (t | 1u);
      t ^= t + (t ^ (t >> 7)) * (t | 61u);
      return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    };
    QStringList shuffled = source;
    for (int i = shuffled.size() - 1; i > 0; --i)
      shuffled.swapItemsAt(i, static_cast<int>(std::floor(random() * (i + 1))));
    auto candidate = improve(shuffled);
    if (std::get<1>(candidate) < std::get<1>(best) ||
        (std::get<1>(candidate) == std::get<1>(best) &&
         std::get<2>(candidate) < std::get<2>(best)))
      best = std::move(candidate);
  }
  return std::get<0>(best);
}

void assignRanks(Graph& graph, const QString& direction,
                 bool ignoreCrossLaneEdges) {
  QStringList order = topologicalOrder(graph);
  if (direction == QLatin1String("LR")) {
    // Upstream's LR path processes Kahn frontiers by generation, sorting each
    // frontier but not merging newly-zero nodes into the current one.
    QHash<QString, int> indegree;
    QHash<QString, QStringList> successors;
    for (const QString& id : graph.order) indegree.insert(id, 0);
    for (const EdgeRef& edge : graph.edges) {
      ++indegree[edge.dst];
      successors[edge.src].push_back(edge.dst);
    }
    QStringList frontier;
    for (auto it = indegree.cbegin(); it != indegree.cend(); ++it)
      if (it.value() == 0) frontier.push_back(it.key());
    frontier = sorted(frontier);
    order.clear();
    while (!frontier.isEmpty()) {
      QStringList next;
      for (const QString& id : frontier) {
        order.push_back(id);
        for (const QString& dst : sorted(successors.value(id)))
          if (--indegree[dst] == 0) next.push_back(dst);
      }
      frontier = sorted(next);
    }
  }

  QHash<QString, int> nextFree;
  for (const QString& id : order) {
    Node& node = graph.nodes[id];
    if (node.group) continue;
    int base = 0;
    for (const EdgeRef& edge : incoming(graph, id)) {
      const int weight = ignoreCrossLaneEdges && topLane(graph, edge.src) != topLane(graph, id)
                             ? 0 : 1;
      base = std::max(base, graph.nodes.value(edge.src).rank + weight);
    }
    const QString lane = topLane(graph, id).isEmpty() ? id : topLane(graph, id);
    node.rank = std::max(base, nextFree.value(lane));
    nextFree[lane] = node.rank + 1;
  }
}

struct DrivingTree {
  QHash<QString, QString> parent;
  QHash<QString, QStringList> children;
  QStringList roots;
};

DrivingTree buildDrivingTree(const Graph& graph,
                             const QHash<QString, int>& rank) {
  DrivingTree tree;
  QHash<QString, QStringList> predecessors;
  for (const QString& id : graph.order) {
    predecessors.insert(id, {});
    tree.children.insert(id, {});
  }
  for (const EdgeRef& edge : graph.edges)
    predecessors[edge.dst].push_back(edge.src);
  for (auto it = predecessors.begin(); it != predecessors.end(); ++it)
    it.value() = sorted(it.value());

  const QStringList topo = topologicalOrder(graph);
  QHash<QString, int> topoIndex;
  for (int i = 0; i < topo.size(); ++i) topoIndex.insert(topo.at(i), i);
  for (const QString& id : topo) {
    QStringList candidates;
    for (const QString& predecessor : predecessors.value(id))
      if (tree.parent.contains(predecessor)) candidates.push_back(predecessor);
    if (candidates.isEmpty()) {
      tree.parent.insert(id, {});
      continue;
    }
    const QString lane = topLane(graph, id);
    std::sort(candidates.begin(), candidates.end(), [&](const QString& a,
                                                         const QString& b) {
      const QString laneA = topLane(graph, a);
      const QString laneB = topLane(graph, b);
      const bool sameA = !laneA.isEmpty() && laneA == lane;
      const bool sameB = !laneB.isEmpty() && laneB == lane;
      if (sameA != sameB) return sameA;
      if (rank.value(a) != rank.value(b)) return rank.value(a) > rank.value(b);
      if (topoIndex.value(a) != topoIndex.value(b))
        return topoIndex.value(a) < topoIndex.value(b);
      return a.localeAwareCompare(b) < 0;
    });
    tree.parent.insert(id, candidates.first());
    tree.children[candidates.first()].push_back(id);
  }
  for (const QString& id : graph.order)
    if (!tree.parent.contains(id)) tree.parent.insert(id, {});
  for (const QString& id : graph.order)
    if (tree.parent.value(id).isEmpty()) tree.roots.push_back(id);
  std::sort(tree.roots.begin(), tree.roots.end(), [&](const QString& a,
                                                       const QString& b) {
    if (topoIndex.value(a) != topoIndex.value(b))
      return topoIndex.value(a) < topoIndex.value(b);
    return a.localeAwareCompare(b) < 0;
  });
  return tree;
}

QString drivingTreeLca(const DrivingTree& tree, QString a, QString b) {
  QSet<QString> ancestors;
  while (!a.isEmpty() && !ancestors.contains(a)) {
    ancestors.insert(a);
    a = tree.parent.value(a);
  }
  QSet<QString> visited;
  while (!b.isEmpty() && !visited.contains(b)) {
    if (ancestors.contains(b)) return b;
    visited.insert(b);
    b = tree.parent.value(b);
  }
  return {};
}

QVector<QStringList> buildMultitreeLayers(const Graph& graph,
                                          const QHash<QString, int>& rank) {
  DrivingTree tree = buildDrivingTree(graph, rank);
  QHash<QString, QHash<int, int>> ownCounts;
  for (const EdgeRef& edge : graph.edges) {
    QString upper = edge.src;
    QString lower = edge.dst;
    int upperRank = rank.value(upper);
    int lowerRank = rank.value(lower);
    if (upperRank > lowerRank) {
      std::swap(upper, lower);
      std::swap(upperRank, lowerRank);
    }
    if (upperRank == lowerRank) continue;
    const QString lca = drivingTreeLca(tree, upper, lower);
    if (lca.isEmpty()) continue;
    for (int layer = upperRank; layer < lowerRank; ++layer)
      ++ownCounts[lca][layer];
  }

  QHash<QString, QHash<QString, int>> crossCounts;
  QSet<QString> crossVisited;
  std::function<QHash<int, int>(const QString&)> collectCrossings =
      [&](const QString& id) {
        crossVisited.insert(id);
        QHash<int, int> accumulated = ownCounts.value(id);
        for (const QString& child : tree.children.value(id)) {
          const QHash<int, int> childCounts = collectCrossings(child);
          int value = childCounts.value(rank.value(id));
          if (rank.value(child) > rank.value(id)) ++value;
          crossCounts[id].insert(child, value);
          for (auto it = childCounts.cbegin(); it != childCounts.cend(); ++it)
            accumulated[it.key()] += it.value();
        }
        return accumulated;
      };
  for (const QString& root : tree.roots)
    if (!crossVisited.contains(root)) collectCrossings(root);
  for (const QString& id : graph.order)
    if (!crossVisited.contains(id)) collectCrossings(id);

  QHash<QString, int> minimumLayer;
  std::function<int(const QString&)> annotateMinimum = [&](const QString& id) {
    int minimum = rank.value(id);
    QStringList children = tree.children.value(id);
    std::sort(children.begin(), children.end(), [&](const QString& a,
                                                     const QString& b) {
      if (rank.value(a) != rank.value(b)) return rank.value(a) < rank.value(b);
      return a.localeAwareCompare(b) < 0;
    });
    for (const QString& child : children)
      minimum = std::min(minimum, annotateMinimum(child));
    minimumLayer.insert(id, minimum);
    return minimum;
  };
  for (const QString& root : tree.roots) annotateMinimum(root);

  int maximumRank = 0;
  for (const QString& id : graph.order)
    maximumRank = std::max(maximumRank, rank.value(id));
  QVector<QStringList> layers(maximumRank + 1);
  QSet<QString> emitted;
  std::function<void(const QString&)> emitNode = [&](const QString& id) {
    if (emitted.contains(id)) return;
    emitted.insert(id);
    layers[rank.value(id)].push_back(id);
    QStringList future;
    QStringList present;
    for (const QString& child : tree.children.value(id)) {
      if (minimumLayer.value(child, rank.value(id)) > rank.value(id))
        future.push_back(child);
      else
        present.push_back(child);
    }
    std::sort(future.begin(), future.end(), [&](const QString& a,
                                                const QString& b) {
      if (minimumLayer.value(a) != minimumLayer.value(b))
        return minimumLayer.value(a) < minimumLayer.value(b);
      return a.localeAwareCompare(b) < 0;
    });
    std::sort(present.begin(), present.end(), [&](const QString& a,
                                                  const QString& b) {
      const int crossingA = crossCounts.value(id).value(a);
      const int crossingB = crossCounts.value(id).value(b);
      if (crossingA != crossingB) return crossingA < crossingB;
      if (minimumLayer.value(a) != minimumLayer.value(b))
        return minimumLayer.value(a) < minimumLayer.value(b);
      return a.localeAwareCompare(b) < 0;
    });
    for (const QString& child : future) emitNode(child);
    for (const QString& child : present) emitNode(child);
  };
  QStringList roots = tree.roots;
  std::sort(roots.begin(), roots.end(), [&](const QString& a, const QString& b) {
    if (rank.value(a) != rank.value(b)) return rank.value(a) < rank.value(b);
    return a.localeAwareCompare(b) < 0;
  });
  for (const QString& root : roots) emitNode(root);
  for (const QString& id : graph.order) emitNode(id);
  for (QStringList& layer : layers) {
    QSet<QString> seen;
    QStringList deduplicated;
    for (const QString& id : layer)
      if (!seen.contains(id)) {
        seen.insert(id);
        deduplicated.push_back(id);
      }
    layer = deduplicated;
  }
  return layers;
}

int adjacentCrossingCount(const QVector<QStringList>& layers,
                          const Graph& graph,
                          const QHash<QString, int>& rank) {
  struct ExpandedEdge { QString src; QString dst; };
  QVector<ExpandedEdge> expanded;
  for (const EdgeRef& edge : graph.edges) {
    QString upper = edge.src;
    QString lower = edge.dst;
    int upperRank = rank.value(upper);
    int lowerRank = rank.value(lower);
    if (upperRank == lowerRank) continue;
    if (upperRank > lowerRank) {
      std::swap(upper, lower);
      std::swap(upperRank, lowerRank);
    }
    for (int layer = upperRank; layer < lowerRank; ++layer)
      expanded.push_back({upper, lower});
  }
  int total = 0;
  for (int layer = 0; layer + 1 < layers.size(); ++layer) {
    QSet<QString> upper(layers.at(layer).cbegin(), layers.at(layer).cend());
    QHash<QString, int> lowerIndex;
    for (int i = 0; i < layers.at(layer + 1).size(); ++i)
      lowerIndex.insert(layers.at(layer + 1).at(i), i);
    QVector<int> destinations;
    for (const ExpandedEdge& edge : expanded)
      if (upper.contains(edge.src) && lowerIndex.contains(edge.dst))
        destinations.push_back(lowerIndex.value(edge.dst));
    for (int i = 0; i < destinations.size(); ++i)
      for (int j = i + 1; j < destinations.size(); ++j)
        if (destinations.at(i) > destinations.at(j)) ++total;
  }
  return total;
}

void optimizeGravityRanks(const Graph& graph, QHash<QString, int>& rank) {
  QHash<QString, QStringList> predecessors;
  for (const QString& id : graph.order) predecessors.insert(id, {});
  for (const EdgeRef& edge : graph.edges)
    predecessors[edge.dst].push_back(edge.src);
  int best = adjacentCrossingCount(buildMultitreeLayers(graph, rank), graph, rank);
  for (int pass = 0; pass < 4; ++pass) {
    bool changed = false;
    QStringList nodes = graph.order;
    std::stable_sort(nodes.begin(), nodes.end(), [&](const QString& a,
                                                     const QString& b) {
      return rank.value(a) > rank.value(b);
    });
    for (const QString& id : nodes) {
      const int current = rank.value(id);
      if (current == 0) continue;
      int lower = 0;
      for (const QString& predecessor : predecessors.value(id))
        lower = std::max(lower, rank.value(predecessor) + 1);
      if (lower >= current) continue;
      rank[id] = lower;
      const int score = adjacentCrossingCount(
          buildMultitreeLayers(graph, rank), graph, rank);
      if (score < best) {
        best = score;
        changed = true;
      } else {
        rank[id] = current;
      }
    }
    if (!changed) break;
  }
}

void assignRanksGravity(Graph& graph, bool optimizeRanksByCrossings) {
  const QStringList order = topologicalOrder(graph);
  QHash<QString, int> rank;
  for (const QString& id : order) {
    const QVector<EdgeRef> predecessors = incoming(graph, id);
    if (predecessors.isEmpty()) {
      rank[id] = 0;
    } else if (predecessors.size() == 1) {
      const QString source = predecessors.first().src;
      rank[id] = topLane(graph, source) != topLane(graph, id)
          ? rank.value(source) : rank.value(source) + 1;
    } else {
      int maximum = 0;
      for (const EdgeRef& edge : predecessors)
        maximum = std::max(maximum, rank.value(edge.src) + 1);
      rank[id] = maximum;
    }
  }

  if (optimizeRanksByCrossings) optimizeGravityRanks(graph, rank);

  QHash<QString, QStringList> predecessors;
  QHash<QString, QStringList> successors;
  for (const QString& id : graph.order) {
    predecessors.insert(id, {});
    successors.insert(id, {});
  }
  for (const EdgeRef& edge : graph.edges) {
    predecessors[edge.dst].push_back(edge.src);
    successors[edge.src].push_back(edge.dst);
  }
  const auto relax = [&](const QStringList& sequence) {
    bool changed = false;
    for (const QString& id : sequence) {
      const QStringList preds = predecessors.value(id);
      const QStringList succs = successors.value(id);
      if (preds.isEmpty() && succs.isEmpty()) continue;
      qreal predAverage = rank.value(id);
      if (!preds.isEmpty()) {
        predAverage = 0.0;
        for (const QString& pred : preds) predAverage += rank.value(pred) + 1;
        predAverage /= preds.size();
      }
      qreal succAverage = rank.value(id);
      if (!succs.isEmpty()) {
        succAverage = 0.0;
        for (const QString& succ : succs) succAverage += rank.value(succ) - 1;
        succAverage /= succs.size();
      }
      const int desired = static_cast<int>(std::round((predAverage + succAverage) / 2.0));
      int lower = 0;
      for (const QString& pred : preds)
        lower = std::max(lower, rank.value(pred) + 1);
      int upper = std::numeric_limits<int>::max();
      for (const QString& succ : succs)
        upper = std::min(upper, rank.value(succ) - 1);
      if (upper == std::numeric_limits<int>::max()) upper = std::max(lower, desired);
      const int clamped = std::min(std::max(desired, lower), upper);
      if (clamped != rank.value(id)) {
        rank[id] = clamped;
        changed = true;
      }
    }
    return changed;
  };
  QStringList reverse = order;
  std::reverse(reverse.begin(), reverse.end());
  for (int iteration = 0; iteration < 8; ++iteration)
    if (!relax(order) && !relax(reverse)) break;
  for (const QString& id : order)
    for (const QString& pred : predecessors.value(id))
      rank[id] = std::max(rank.value(id), rank.value(pred) + 1);
  for (const QString& id : reverse)
    for (const QString& succ : successors.value(id))
      rank[id] = std::min(rank.value(id), rank.value(succ) - 1);
  for (const QString& id : graph.order) graph.nodes[id].rank = rank.value(id);
}

QVector<QStringList> buildLayers(const Graph& graph, bool includeGroups = false) {
  int maxRank = 0;
  for (const QString& id : graph.order)
    if (includeGroups || !graph.nodes.value(id).group)
      maxRank = std::max(maxRank, graph.nodes.value(id).rank);
  QVector<QStringList> layers(maxRank + 1);
  for (const QString& id : topologicalOrder(graph)) {
    const Node& node = graph.nodes.value(id);
    if (includeGroups || !node.group) layers[node.rank].push_back(id);
  }
  return layers;
}

void insertDummies(Graph& graph, QVector<QStringList>& layers) {
  QVector<EdgeRef> sortedEdges = graph.edges;
  std::sort(sortedEdges.begin(), sortedEdges.end(), [](const EdgeRef& a, const EdgeRef& b) {
    const int id = a.id.localeAwareCompare(b.id);
    if (id != 0) return id < 0;
    const int src = a.src.localeAwareCompare(b.src);
    return src != 0 ? src < 0 : a.dst.localeAwareCompare(b.dst) < 0;
  });
  QVector<EdgeRef> replacement;
  int sequence = 0;
  for (const EdgeRef& edge : sortedEdges) {
    const int fromRank = graph.nodes.value(edge.src).rank;
    const int toRank = graph.nodes.value(edge.dst).rank;
    if (toRank - fromRank <= 1) {
      replacement.push_back(edge);
      continue;
    }
    QString previous = edge.src;
    int segment = 0;
    for (int rank = fromRank + 1; rank < toRank; ++rank) {
      const QString id = QStringLiteral("placeholder-%1").arg(sequence++);
      Node dummy;
      dummy.id = id;
      dummy.rank = rank;
      dummy.dummy = true;
      // QSizeF defaults to (-1, -1), while Mermaid's layout-only nodes are
      // created with an explicit zero width/height. Negative dummy extents
      // shrink the null-lane column and shift every real lane.
      dummy.size = QSizeF(0.0, 0.0);
      dummy.renderSize = dummy.size;
      graph.nodes.insert(id, dummy);
      graph.order.push_back(id);
      while (layers.size() <= rank) layers.push_back({});
      layers[rank].push_back(id);
      EdgeRef part = edge;
      part.id = edge.id + QLatin1Char('#') + QString::number(segment++);
      part.src = previous;
      part.dst = id;
      replacement.push_back(part);
      previous = id;
    }
    EdgeRef part = edge;
    part.id = edge.id + QLatin1Char('#') + QString::number(segment);
    part.src = previous;
    replacement.push_back(part);
  }
  graph.edges = replacement;
}

qreal median(QVector<int> values) {
  if (values.isEmpty()) return std::numeric_limits<qreal>::infinity();
  std::sort(values.begin(), values.end());
  const int n = values.size();
  return n % 2 ? values.at(n / 2) : (values.at(n / 2 - 1) + values.at(n / 2)) / 2.0;
}

void orderLayers(Graph& graph, QVector<QStringList>& layers,
                 const QStringList& laneOrder) {
  const auto reorder = [&](const QStringList& fixed, const QStringList& target,
                           bool down) {
    QHash<QString, int> fixedIndex;
    QHash<QString, int> currentIndex;
    for (int i = 0; i < fixed.size(); ++i) fixedIndex.insert(fixed.at(i), i);
    for (int i = 0; i < target.size(); ++i) currentIndex.insert(target.at(i), i);
    QHash<QString, QVector<int>> neighbors;
    for (const QString& id : target) neighbors.insert(id, {});
    for (const EdgeRef& edge : graph.edges) {
      const QString fixedNode = down ? edge.src : edge.dst;
      const QString targetNode = down ? edge.dst : edge.src;
      if (fixedIndex.contains(fixedNode) && neighbors.contains(targetNode))
        neighbors[targetNode].push_back(fixedIndex.value(fixedNode));
    }
    QHash<QString, QStringList> byLane;
    QStringList noLane;
    for (const QString& id : target) {
      const QString lane = topLane(graph, id);
      if (lane.isEmpty()) noLane.push_back(id); else byLane[lane].push_back(id);
    }
    auto sortPart = [&](QStringList part) {
      std::stable_sort(part.begin(), part.end(), [&](const QString& a, const QString& b) {
        const qreal ma = median(neighbors.value(a));
        const qreal mb = median(neighbors.value(b));
        if (ma == mb) return currentIndex.value(a) < currentIndex.value(b);
        if (!std::isfinite(ma)) return false;
        if (!std::isfinite(mb)) return true;
        return ma < mb;
      });
      return part;
    };
    QStringList result;
    for (const QString& lane : laneOrder) result += sortPart(byLane.value(lane));
    result += sortPart(noLane);
    return result;
  };
  for (int sweep = 0; sweep < 3; ++sweep) {
    for (int i = 1; i < layers.size(); ++i)
      layers[i] = reorder(layers.at(i - 1), layers.at(i), true);
    for (int i = layers.size() - 2; i >= 0; --i)
      layers[i] = reorder(layers.at(i + 1), layers.at(i), false);
  }
  for (int rank = 0; rank < layers.size(); ++rank)
    for (int order = 0; order < layers.at(rank).size(); ++order)
      graph.nodes[layers.at(rank).at(order)].order = order;
}

void assignCoordinates(Graph& graph, const QVector<QStringList>& layers,
                       qreal nodeGap, qreal layerGap, const QString& direction,
                       const QStringList& laneOrder) {
  const bool horizontal = direction == QLatin1String("LR") || direction == QLatin1String("RL");
  bool hasNullLane = false;
  for (const QStringList& layer : layers) {
    for (const QString& id : layer) {
      if (topLane(graph, id).isEmpty()) {
        hasNullLane = true;
        break;
      }
    }
    if (hasNullLane) break;
  }
  QHash<QString, qreal> laneWidth;
  QVector<qreal> layerHeights;
  for (const QStringList& layer : layers) {
    qreal height = 0.0;
    QHash<QString, QStringList> byLane;
    for (const QString& id : layer) {
      const Node& node = graph.nodes.value(id);
      height = std::max(height, node.size.height());
      byLane[topLane(graph, id)].push_back(id);
    }
    layerHeights.push_back(height);
    for (auto it = byLane.cbegin(); it != byLane.cend(); ++it) {
      qreal width = nodeGap * std::max<qsizetype>(0, it.value().size() - 1);
      for (const QString& id : it.value()) width += graph.nodes.value(id).size.width();
      laneWidth[it.key()] = std::max(laneWidth.value(it.key()), width);
    }
  }
  qreal totalWidth = 0.0;
  for (const QString& lane : laneOrder) totalWidth += laneWidth.value(lane);
  if (hasNullLane) totalWidth += laneWidth.value(QString());
  const qsizetype columnCount = laneOrder.size() + (hasNullLane ? 1 : 0);
  totalWidth += nodeGap * 2.0 * std::max<qsizetype>(0, columnCount - 1);
  QHash<QString, qreal> laneCenter;
  qreal cursor = -totalWidth / 2.0;
  if (hasNullLane)
    cursor += laneWidth.value(QString()) + nodeGap * 2.0;
  for (const QString& lane : laneOrder) {
    laneCenter[lane] = cursor + laneWidth.value(lane) / 2.0;
    cursor += laneWidth.value(lane) + nodeGap * 2.0;
  }
  qreal y = 0.0;
  for (int rank = 0; rank < layers.size(); ++rank) {
    QHash<QString, QStringList> byLane;
    for (const QString& id : layers.at(rank)) byLane[topLane(graph, id)].push_back(id);
    for (const QString& lane : laneOrder) {
      const QStringList ids = byLane.value(lane);
      if (ids.isEmpty()) continue;
      qreal width = nodeGap * std::max<qsizetype>(0, ids.size() - 1);
      for (const QString& id : ids) width += graph.nodes.value(id).size.width();
      qreal x = laneCenter.value(lane) - width / 2.0;
      for (const QString& id : ids) {
        Node& node = graph.nodes[id];
        node.center = QPointF(x + node.size.width() / 2.0,
                              y + layerHeights.at(rank) / 2.0);
        x += node.size.width() + nodeGap;
      }
    }
    qreal extra = 0.0;
    if (horizontal && rank + 1 < layers.size()) {
      qreal thisWidth = 0.0, nextWidth = 0.0, nextHeight = 0.0;
      for (const QString& id : layers.at(rank))
        thisWidth = std::max(thisWidth, graph.nodes.value(id).size.width());
      for (const QString& id : layers.at(rank + 1)) {
        nextWidth = std::max(nextWidth, graph.nodes.value(id).size.width());
        nextHeight = std::max(nextHeight, graph.nodes.value(id).size.height());
      }
      const qreal normalSeparation =
          layerHeights.at(rank) / 2.0 + nextHeight / 2.0;
      extra = std::max<qreal>(
          0.0, (thisWidth + nextWidth) / 2.0 - normalSeparation - layerGap);
    }
    y += layerHeights.at(rank) + layerGap + extra;
  }

  // Long-edge dummies are centered between the original endpoints.
  QHash<int, QSet<QString>> involved;
  for (const EdgeRef& edge : graph.edges) {
    if (edge.sourceIndex < 0) continue;
    involved[edge.sourceIndex].insert(edge.src);
    involved[edge.sourceIndex].insert(edge.dst);
  }
  for (auto it = involved.cbegin(); it != involved.cend(); ++it) {
    const int source = it.key();
    QString src, dst;
    for (const EdgeRef& edge : graph.edges)
      if (edge.sourceIndex == source) { src = edge.src; break; }
    for (auto edge = graph.edges.crbegin(); edge != graph.edges.crend(); ++edge)
      if (edge->sourceIndex == source) { dst = edge->dst; break; }
    const qreal mid = std::round((graph.nodes.value(src).center.x() +
                                  graph.nodes.value(dst).center.x()) / 2.0);
    for (const QString& id : it.value())
      if (graph.nodes.value(id).dummy) graph.nodes[id].center.setX(mid);
  }
}

QRectF nodeRect(const Node& node) {
  return QRectF(node.center.x() - node.size.width() / 2.0,
                node.center.y() - node.size.height() / 2.0,
                node.size.width(), node.size.height());
}

QPointF portFor(const Node& node, const QPointF& toward, bool source) {
  const qreal dx = toward.x() - node.center.x();
  const qreal dy = toward.y() - node.center.y();
  if (std::abs(dy) * 3.0 >= std::abs(dx) && std::abs(dy) > kEps)
    return QPointF(node.center.x(), dy > 0 ? nodeRect(node).bottom() : nodeRect(node).top());
  if (std::abs(dx) > kEps)
    return QPointF(dx > 0 ? nodeRect(node).right() : nodeRect(node).left(), node.center.y());
  return QPointF(node.center.x(), source ? nodeRect(node).bottom() : nodeRect(node).top());
}

bool segmentHits(const QPointF& a, const QPointF& b, const QRectF& obstacle) {
  if (std::abs(a.x() - b.x()) < kEps)
    return a.x() > obstacle.left() && a.x() < obstacle.right() &&
           std::max(a.y(), b.y()) > obstacle.top() &&
           std::min(a.y(), b.y()) < obstacle.bottom();
  return a.y() > obstacle.top() && a.y() < obstacle.bottom() &&
         std::max(a.x(), b.x()) > obstacle.left() &&
         std::min(a.x(), b.x()) < obstacle.right();
}

QVector<QPointF> routeEdge(const Graph& graph, const QString& srcId,
                           const QString& dstId,
                           const std::optional<QPointF>& sourcePort = std::nullopt,
                           const std::optional<QPointF>& targetPort = std::nullopt) {
  const Node& src = graph.nodes.value(srcId);
  const Node& dst = graph.nodes.value(dstId);
  const QPointF start = sourcePort.value_or(portFor(src, dst.center, true));
  const QPointF end = targetPort.value_or(portFor(dst, src.center, false));
  if (std::abs(start.y() - end.y()) < kEps) {
    bool blocked = false;
    for (const QString& id : graph.order) {
      const Node& obstacle = graph.nodes.value(id);
      if (obstacle.group || obstacle.dummy || id == srcId || id == dstId)
        continue;
      if (segmentHits(start, end, nodeRect(obstacle))) {
        blocked = true;
        break;
      }
    }
    int sourceDegree = 0;
    int targetDegree = 0;
    for (const EdgeRef& edge : graph.edges) {
      if (edge.sourceIndex < 0) continue;
      if (edge.src == srcId || edge.dst == srcId) ++sourceDegree;
      if (edge.src == dstId || edge.dst == dstId) ++targetDegree;
    }
    // The upstream router's uncontested-face path sends an isolated aligned
    // edge around an intervening node on the first exterior horizontal pipe.
    // Contested endpoints continue through track assignment below, where
    // their sibling edges determine the selected outer channel.
    if (blocked && sourceDegree == 1 && targetDegree == 1) {
      constexpr qreal kAnchorOffset = 20.0;
      const QPointF topStart(src.center.x(), nodeRect(src).top());
      const QPointF topEnd(dst.center.x(), nodeRect(dst).top());
      const qreal railY = std::min(topStart.y(), topEnd.y()) - kAnchorOffset;
      return {topStart, QPointF(topStart.x(), railY),
              QPointF(topEnd.x(), railY), topEnd};
    }
  }
  if (std::abs(start.x() - end.x()) < kEps ||
      std::abs(start.y() - end.y()) < kEps)
    return {start, end};

  const bool sourceVertical =
      std::abs(start.y() - nodeRect(src).top()) < kEps ||
      std::abs(start.y() - nodeRect(src).bottom()) < kEps;
  const bool targetVertical =
      std::abs(end.y() - nodeRect(dst).top()) < kEps ||
      std::abs(end.y() - nodeRect(dst).bottom()) < kEps;
  if (sourceVertical && targetVertical) {
    constexpr qreal kAnchorOffset = 20.0;
    const qreal direction = end.y() >= start.y() ? 1.0 : -1.0;
    const qreal railY = end.y() - direction * kAnchorOffset;
    return {start, QPointF(start.x(), railY),
            QPointF(end.x(), railY), end};
  }
  const bool sourceHorizontal =
      std::abs(start.x() - nodeRect(src).left()) < kEps ||
      std::abs(start.x() - nodeRect(src).right()) < kEps;
  const bool targetHorizontal =
      std::abs(end.x() - nodeRect(dst).left()) < kEps ||
      std::abs(end.x() - nodeRect(dst).right()) < kEps;
  if (sourceHorizontal && targetHorizontal) {
    constexpr qreal kAnchorOffset = 20.0;
    const bool leavesLeft = start.x() < src.center.x();
    const qreal railX = leavesLeft
        ? std::min(start.x(), end.x()) - kAnchorOffset
        : std::max(start.x(), end.x()) + kAnchorOffset;
    return {start, QPointF(railX, start.y()),
            QPointF(railX, end.y()), end};
  }

  QVector<QRectF> obstacles;
  for (const QString& id : graph.order) {
    const Node& node = graph.nodes.value(id);
    if (node.group || node.dummy || id == srcId || id == dstId) continue;
    obstacles.push_back(nodeRect(node).adjusted(-kRouterPadding, -kRouterPadding,
                                                kRouterPadding, kRouterPadding));
  }
  const qreal middleY = (start.y() + end.y()) / 2.0;
  QVector<QPointF> verticalFirst{start, QPointF(start.x(), middleY),
                                 QPointF(end.x(), middleY), end};
  bool blocked = false;
  for (const QRectF& obstacle : obstacles)
    for (int i = 1; i < verticalFirst.size(); ++i)
      blocked |= segmentHits(verticalFirst.at(i - 1), verticalFirst.at(i), obstacle);
  if (!blocked) return verticalFirst;
  const qreal middleX = (start.x() + end.x()) / 2.0;
  return {start, QPointF(middleX, start.y()), QPointF(middleX, end.y()), end};
}

QVector<QPointF> routeEdgeThroughLabel(const Graph& graph,
                                       const QString& srcId,
                                       const QString& dstId,
                                       const QString& labelId) {
  const Node& src = graph.nodes.value(srcId);
  const Node& dst = graph.nodes.value(dstId);
  const QPointF waypoint = graph.nodes.value(labelId).center;
  const QPointF start = portFor(src, waypoint, true);
  const QPointF end = portFor(dst, waypoint, false);
  QVector<QPointF> result{start};
  const auto append = [&](const QPointF& point) {
    if (result.isEmpty() || result.last() != point) result.push_back(point);
  };

  // The label node is a zero-border waypoint: the original edge passes
  // through its center rather than intersecting its visual label rectangle.
  append(waypoint);
  const bool targetVertical =
      std::abs(end.y() - nodeRect(dst).top()) < kEps ||
      std::abs(end.y() - nodeRect(dst).bottom()) < kEps;
  append(targetVertical ? QPointF(end.x(), waypoint.y())
                        : QPointF(waypoint.x(), end.y()));
  append(end);

  // The upstream orthogonalizer removes duplicate and collinear waypoints.
  for (qsizetype i = 1; i + 1 < result.size();) {
    const QPointF& a = result.at(i - 1);
    const QPointF& b = result.at(i);
    const QPointF& c = result.at(i + 1);
    const bool horizontal = std::abs(a.y() - b.y()) < kEps &&
                            std::abs(b.y() - c.y()) < kEps;
    const bool vertical = std::abs(a.x() - b.x()) < kEps &&
                          std::abs(b.x() - c.x()) < kEps;
    if (horizontal || vertical) result.removeAt(i); else ++i;
  }
  return result;
}

QPointF anchorEdgeLabel(const QVector<QPointF>& points, const QSizeF& size,
                        const QPointF& fallback) {
  if (points.size() < 2) return fallback;
  const bool preferHorizontal = size.width() >= size.height();
  int best = -1;
  int bestClass = -1;
  qreal bestLength = -1.0;
  for (int i = 0; i + 1 < points.size(); ++i) {
    const qreal dx = std::abs(points.at(i + 1).x() - points.at(i).x());
    const qreal dy = std::abs(points.at(i + 1).y() - points.at(i).y());
    if (dx > kEps && dy > kEps) continue;
    const bool horizontal = dx >= dy;
    const qreal length = dx + dy;
    const qreal extent = horizontal ? size.width() : size.height();
    const int candidateClass =
        (horizontal == preferHorizontal ? 2 : 0) + (length >= extent + 2.0 ? 1 : 0);
    if (candidateClass > bestClass ||
        (candidateClass == bestClass && length > bestLength)) {
      best = i;
      bestClass = candidateClass;
      bestLength = length;
    }
  }
  if (best < 0) return fallback;
  return (points.at(best) + points.at(best + 1)) / 2.0;
}

void resolveSimpleCrossings(const Graph& graph, const FlowchartData& data,
                            QVector<QVector<QPointF>>& routed,
                            bool gravityLayering) {
  QSet<int> handled;
  for (int i = 0; i < data.edges.size(); ++i) {
    if (handled.contains(i)) continue;
    const FlowEdge& first = data.edges.at(i);
    if (!graph.nodes.contains(first.start) || !graph.nodes.contains(first.end) ||
        !first.text.isEmpty())
      continue;
    const Node& firstSource = graph.nodes.value(first.start);
    const Node& firstTarget = graph.nodes.value(first.end);
    for (int j = i + 1; j < data.edges.size(); ++j) {
      if (handled.contains(j)) continue;
      const FlowEdge& second = data.edges.at(j);
      if (!graph.nodes.contains(second.start) || !graph.nodes.contains(second.end) ||
          !second.text.isEmpty())
        continue;
      const Node& secondSource = graph.nodes.value(second.start);
      const Node& secondTarget = graph.nodes.value(second.end);
      if (firstSource.rank != secondSource.rank ||
          firstTarget.rank != secondTarget.rank ||
          firstSource.rank >= firstTarget.rank)
        continue;
      const qreal sourceOrder = firstSource.center.x() - secondSource.center.x();
      const qreal targetOrder = firstTarget.center.x() - secondTarget.center.x();
      if (sourceOrder * targetOrder >= -kEps) continue;

      const int externalIndex = sourceOrder < 0.0 ? i : j;
      const int internalIndex = externalIndex == i ? j : i;
      const FlowEdge& external = data.edges.at(externalIndex);
      const FlowEdge& internal = data.edges.at(internalIndex);
      const Node& externalSource = graph.nodes.value(external.start);
      const Node& externalTarget = graph.nodes.value(external.end);
      const Node& internalSource = graph.nodes.value(internal.start);
      const Node& internalTarget = graph.nodes.value(internal.end);

      qreal top = std::numeric_limits<qreal>::infinity();
      for (const QString& id : graph.order) {
        const Node& node = graph.nodes.value(id);
        if (!node.group && !node.dummy) top = std::min(top, nodeRect(node).top());
      }
      const QPointF externalStart(externalSource.center.x(),
                                  nodeRect(externalSource).top());
      const QPointF externalEnd(nodeRect(externalTarget).right(),
                                externalTarget.center.y());
      const qreal outerY = top - 20.0;
      const qreal outerX = externalEnd.x() + 20.0;
      routed[externalIndex] = {
          externalStart, QPointF(externalStart.x(), outerY),
          QPointF(outerX, outerY), QPointF(outerX, externalEnd.y()),
          externalEnd};

      const QPointF internalStart(nodeRect(internalSource).left(),
                                  internalSource.center.y());
      const QPointF internalEnd(internalTarget.center.x() + 10.0,
                                nodeRect(internalTarget).top());
      const qreal railX = internalStart.x() - 20.0;
      // In the gravity pipeline the two top-level group nodes occupy the
      // null-lane in layer zero. Mermaid's orthogonal router therefore rejects
      // the first interior channel and selects the next 20px track.
      const qreal railY = nodeRect(internalSource).bottom() +
                          (gravityLayering ? 30.0 : 10.0);
      routed[internalIndex] = {
          internalStart, QPointF(railX, internalStart.y()),
          QPointF(railX, railY), QPointF(internalEnd.x(), railY),
          internalEnd};
      handled.insert(externalIndex);
      handled.insert(internalIndex);
      break;
    }
  }
}

QString portSide(const Node& node, const QPointF& port) {
  const QRectF rect = nodeRect(node);
  if (std::abs(port.y() - rect.top()) < kEps) return QStringLiteral("top");
  if (std::abs(port.y() - rect.bottom()) < kEps) return QStringLiteral("bottom");
  if (std::abs(port.x() - rect.left()) < kEps) return QStringLiteral("left");
  return QStringLiteral("right");
}

QHash<int, QPointF> distributedTargetPorts(
    const Graph& graph, const FlowchartData& data,
    QHash<int, QPointF>* distributedSources) {
  QHash<QString, QVector<int>> groups;
  QHash<int, QPointF> natural;
  for (int i = 0; i < data.edges.size(); ++i) {
    const FlowEdge& edge = data.edges.at(i);
    if (!graph.nodes.contains(edge.start) || !graph.nodes.contains(edge.end))
      continue;
    const Node& source = graph.nodes.value(edge.start);
    const Node& target = graph.nodes.value(edge.end);
    const QPointF port = portFor(target, source.center, false);
    natural.insert(i, port);
    groups[edge.end + QLatin1Char(':') + portSide(target, port)].push_back(i);
  }

  QHash<int, QPointF> result;
  QSet<int> sideReassigned;
  constexpr qreal kTrackSpacing = 10.0;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    if (it.value().size() < 2) continue;
    for (const int index : it.value()) {
      const FlowEdge& edge = data.edges.at(index);
      const Node& source = graph.nodes.value(edge.start);
      const Node& target = graph.nodes.value(edge.end);
      if (target.rank - source.rank <= 1 ||
          std::abs(source.center.x() - target.center.x()) >= kEps)
        continue;
      bool blocked = false;
      for (const QString& id : graph.order) {
        const Node& obstacle = graph.nodes.value(id);
        if (obstacle.group || obstacle.dummy || id == edge.start || id == edge.end)
          continue;
        if (obstacle.rank > source.rank && obstacle.rank < target.rank &&
            std::abs(obstacle.center.x() - source.center.x()) < kEps) {
          blocked = true;
          break;
        }
      }
      if (!blocked) continue;
      distributedSources->insert(
          index, QPointF(nodeRect(source).left(), source.center.y()));
      result.insert(index,
                    QPointF(nodeRect(target).left(), target.center.y()));
      sideReassigned.insert(index);
    }
    for (const int index : it.value()) {
      if (sideReassigned.contains(index)) continue;
      const FlowEdge& edge = data.edges.at(index);
      const Node& source = graph.nodes.value(edge.start);
      const Node& target = graph.nodes.value(edge.end);
      QPointF port = natural.value(index);
      const QString side = portSide(target, port);
      if (side == QLatin1String("top") || side == QLatin1String("bottom")) {
        const qreal delta = source.center.x() - target.center.x();
        if (std::abs(delta) > kEps)
          port.rx() += delta < 0.0 ? -kTrackSpacing : kTrackSpacing;
      } else {
        const qreal delta = source.center.y() - target.center.y();
        if (std::abs(delta) > kEps)
          port.ry() += delta < 0.0 ? -kTrackSpacing : kTrackSpacing;
      }
      if (port != natural.value(index)) result.insert(index, port);
    }
  }
  return result;
}

int groupDepth(const Graph& graph, QString id) {
  int depth = 0;
  QSet<QString> seen;
  while (graph.nodes.contains(id) && !seen.contains(id)) {
    seen.insert(id);
    const QString parent = graph.nodes.value(id).parent;
    if (parent.isEmpty()) break;
    id = parent;
    ++depth;
  }
  return depth;
}

std::optional<QRectF> directChildBounds(const Graph& graph,
                                        const QString& groupId,
                                        bool includeGroups = true) {
  QRectF bounds;
  bool first = true;
  for (const QString& childId : graph.order) {
    const Node& child = graph.nodes.value(childId);
    if (child.dummy || child.parent != groupId ||
        (!includeGroups && child.group))
      continue;
    const QRectF childRect = nodeRect(child);
    bounds = first ? childRect : bounds.united(childRect);
    first = false;
  }
  if (first) return std::nullopt;
  return bounds;
}

QVector<QString> orderedGroupsByDepth(const Graph& graph) {
  QVector<QString> groups;
  for (const QString& id : graph.order)
    if (graph.nodes.value(id).group) groups.push_back(id);
  std::sort(groups.begin(), groups.end(), [&](const QString& a, const QString& b) {
    return groupDepth(graph, a) > groupDepth(graph, b);
  });
  return groups;
}

void computeTbGroups(Graph& graph) {
  const QVector<QString> groups = orderedGroupsByDepth(graph);
  QHash<QString, QRectF> content;
  for (const QString& id : groups) {
    const std::optional<QRectF> measured = directChildBounds(graph, id, false);
    Node& group = graph.nodes[id];
    if (!measured) continue;
    const QRectF bounds = *measured;
    content.insert(id, bounds);
    const qreal horizontalPadding = group.parent.isEmpty()
        ? 2.0 * std::max(group.padding, kMinLanePadding)
        : group.padding;
    group.size = QSizeF(bounds.width() + horizontalPadding,
                        bounds.height() + group.padding);
    group.center = bounds.center();
  }

  QVector<QString> lanes;
  qreal minY = std::numeric_limits<qreal>::infinity();
  qreal maxY = -std::numeric_limits<qreal>::infinity();
  qreal maxPadding = 0.0;
  for (const QString& id : groups) {
    const Node& group = graph.nodes.value(id);
    if (!group.parent.isEmpty() || !content.contains(id)) continue;
    lanes.push_back(id);
    minY = std::min(minY, content.value(id).top());
    maxY = std::max(maxY, content.value(id).bottom());
    maxPadding = std::max(maxPadding, group.padding);
  }
  if (lanes.isEmpty()) return;
  const qreal margin = std::max<qreal>(maxPadding, 36.0);
  const qreal height = maxY - minY + 2.0 * margin;
  for (const QString& id : lanes) {
    Node& lane = graph.nodes[id];
    lane.center.setY((minY + maxY) / 2.0);
    lane.size.setHeight(height);
    lane.contentTop = minY;
  }
  std::sort(lanes.begin(), lanes.end(), [&](const QString& a, const QString& b) {
    return graph.nodes.value(a).center.x() < graph.nodes.value(b).center.x();
  });
  if (lanes.size() < 2) return;
  QVector<qreal> centers, baseWidths, offsets(lanes.size());
  for (const QString& id : lanes) {
    const QRectF b = content.value(id);
    centers.push_back(b.center().x());
    baseWidths.push_back(b.width() + 2.0 * std::max(graph.nodes.value(id).padding,
                                                   kMinLanePadding));
  }
  offsets[0] = 0.0;
  for (int i = 0; i + 1 < lanes.size(); ++i)
    offsets[i + 1] = 2.0 * (centers.at(i + 1) - centers.at(i)) - offsets.at(i);
  qreal lower = 0.0;
  qreal upper = std::numeric_limits<qreal>::infinity();
  for (int i = 0; i < lanes.size(); ++i) {
    if (i % 2 == 0) lower = std::max(lower, baseWidths.at(i) - offsets.at(i));
    else upper = std::min(upper, offsets.at(i) - baseWidths.at(i));
  }
  const qreal x = lower <= upper ? (lower + upper) / 2.0 : lower;
  for (int i = 0; i < lanes.size(); ++i)
    graph.nodes[lanes.at(i)].size.setWidth(
        std::max(baseWidths.at(i), offsets.at(i) + (i % 2 == 0 ? x : -x)));
}

void computeLrGroups(Graph& graph) {
  const QVector<QString> groups = orderedGroupsByDepth(graph);

  // lrTransform.ts recomputes nested groups after rotating the content. Top
  // level lanes are normalized separately below.
  for (const QString& id : groups) {
    Node& group = graph.nodes[id];
    if (group.parent.isEmpty()) continue;
    const std::optional<QRectF> measured = directChildBounds(graph, id);
    if (!measured) continue;
    group.center = measured->center();
    group.size = QSizeF(measured->width() + group.padding,
                        measured->height() + group.padding);
  }

  QVector<QString> lanes;
  QHash<QString, QRectF> content;
  qreal globalMinX = std::numeric_limits<qreal>::infinity();
  qreal globalMaxX = -std::numeric_limits<qreal>::infinity();
  qreal maxPadding = 0.0;
  for (const QString& id : groups) {
    const Node& group = graph.nodes.value(id);
    if (!group.parent.isEmpty()) continue;
    lanes.push_back(id);
    maxPadding = std::max(maxPadding, group.padding);
  }
  for (const QString& laneId : lanes) {
    QRectF bounds;
    bool first = true;
    for (const QString& id : graph.order) {
      const Node& node = graph.nodes.value(id);
      if (node.group || node.dummy || topLane(graph, id) != laneId) continue;
      const QRectF rect = nodeRect(node);
      bounds = first ? rect : bounds.united(rect);
      first = false;
    }
    if (first) continue;
    content.insert(laneId, bounds);
    globalMinX = std::min(globalMinX, bounds.left());
    globalMaxX = std::max(globalMaxX, bounds.right());
  }
  if (content.isEmpty()) return;

  constexpr qreal kLrLayoutTitleBand = 36.0;
  const qreal horizontalMargin = std::max<qreal>(maxPadding, 10.0);
  const qreal bodyWidth = std::max<qreal>(0.0, globalMaxX - globalMinX) +
                          2.0 * horizontalMargin;
  const qreal laneWidth = kLrLayoutTitleBand + bodyWidth;
  const qreal bodyCenter = (globalMinX + globalMaxX) / 2.0;
  const qreal laneLeft = bodyCenter - bodyWidth / 2.0 - kLrLayoutTitleBand;
  const qreal centerX = laneLeft + laneWidth / 2.0;
  const qreal verticalMargin = std::max<qreal>(maxPadding, kLrLayoutTitleBand);

  QVector<QString> visible;
  for (const QString& id : lanes)
    if (content.contains(id)) visible.push_back(id);
  std::sort(visible.begin(), visible.end(), [&](const QString& a,
                                                const QString& b) {
    return content.value(a).center().y() < content.value(b).center().y();
  });
  for (qsizetype i = 0; i < visible.size(); ++i) {
    const QRectF current = content.value(visible.at(i));
    const qreal top = i == 0
        ? current.top() - verticalMargin
        : (content.value(visible.at(i - 1)).bottom() + current.top()) / 2.0;
    const qreal bottom = i + 1 == visible.size()
        ? current.bottom() + verticalMargin
        : (current.bottom() + content.value(visible.at(i + 1)).top()) / 2.0;
    Node& lane = graph.nodes[visible.at(i)];
    lane.center = QPointF(centerX, (top + bottom) / 2.0);
    lane.size = QSizeF(laneWidth, std::max<qreal>(0.0, bottom - top));
    lane.contentTop = current.top();
  }
}

void liftTopLaneTitlesAboveRails(Graph& graph,
                                 const QVector<QVector<QPointF>>& edges,
                                 const QString& direction) {
  if (direction == QLatin1String("LR") || direction == QLatin1String("RL") ||
      direction == QLatin1String("BT"))
    return;
  constexpr qreal kClearance = 4.0;
  qreal delta = 0.0;
  for (const QVector<QPointF>& points : edges) {
    for (int i = 0; i + 1 < points.size(); ++i) {
      const QPointF& a = points.at(i);
      const QPointF& b = points.at(i + 1);
      if (std::abs(a.y() - b.y()) >= kEps) continue;
      for (const QString& id : graph.order) {
        const Node& lane = graph.nodes.value(id);
        if (!lane.group || !lane.parent.isEmpty() || lane.size.isEmpty()) continue;
        const QRectF bounds = nodeRect(lane);
        const qreal titleTop = bounds.top();
        const qreal titleBottom = titleTop + kLaneTitleHeight;
        if (a.y() <= titleTop + kEps || a.y() >= titleBottom - kEps) continue;
        const qreal overlap = std::min(std::max(a.x(), b.x()), bounds.right()) -
                              std::max(std::min(a.x(), b.x()), bounds.left());
        if (overlap < 2.0) continue;
        delta = std::max(delta, titleBottom - a.y() + kClearance);
      }
    }
  }
  if (delta <= kEps) return;
  for (const QString& id : graph.order) {
    Node& lane = graph.nodes[id];
    if (!lane.group || !lane.parent.isEmpty() || lane.size.isEmpty()) continue;
    lane.center.ry() -= delta / 2.0;
    lane.size.rheight() += delta;
  }
}

void applyDirection(Graph& graph, QVector<QVector<QPointF>>& edgePoints,
                    const QString& direction) {
  if (direction != QLatin1String("LR") && direction != QLatin1String("RL") &&
      direction != QLatin1String("BT")) return;
  QVector<QString> realNodes;
  for (const QString& id : graph.order)
    if (!graph.nodes.value(id).group && !graph.nodes.value(id).dummy) realNodes.push_back(id);
  if (realNodes.isEmpty()) return;
  if (direction == QLatin1String("BT")) {
    qreal min = std::numeric_limits<qreal>::infinity();
    qreal max = -std::numeric_limits<qreal>::infinity();
    for (const QString& id : realNodes) {
      min = std::min(min, graph.nodes.value(id).center.y());
      max = std::max(max, graph.nodes.value(id).center.y());
    }
    for (auto it = graph.nodes.begin(); it != graph.nodes.end(); ++it)
      it->center.setY(min + max - it->center.y());
    for (auto& points : edgePoints)
      for (QPointF& point : points) point.setY(min + max - point.y());
    return;
  }
  qreal minX = std::numeric_limits<qreal>::infinity();
  qreal minY = std::numeric_limits<qreal>::infinity();
  qreal totalWidth = 0.0, totalHeight = 0.0;
  for (const QString& id : realNodes) {
    const Node& node = graph.nodes.value(id);
    minX = std::min(minX, node.center.x());
    minY = std::min(minY, node.center.y());
    totalWidth += node.size.width();
    totalHeight += node.size.height();
  }
  const qreal scale = totalHeight > 0.0 ? std::max<qreal>(1.0, totalWidth / totalHeight) : 1.0;
  auto rotate = [&](const QPointF& point) {
    return QPointF((point.y() - minY) * scale + 36.0, point.x() - minX);
  };
  for (auto it = graph.nodes.begin(); it != graph.nodes.end(); ++it) {
    it->center = rotate(it->center);
  }
  for (auto& points : edgePoints)
    for (QPointF& point : points) point = rotate(point);
}

void mirrorHorizontal(Graph& graph, QVector<QVector<QPointF>>& edgePoints) {
  qreal min = std::numeric_limits<qreal>::infinity();
  qreal max = -std::numeric_limits<qreal>::infinity();
  for (const QString& id : graph.order) {
    const Node& node = graph.nodes.value(id);
    if (node.group || node.dummy) continue;
    min = std::min(min, node.center.x());
    max = std::max(max, node.center.x());
  }
  if (!std::isfinite(min) || !std::isfinite(max)) return;
  for (auto it = graph.nodes.begin(); it != graph.nodes.end(); ++it)
    it->center.setX(min + max - it->center.x());
  for (auto& points : edgePoints)
    for (QPointF& point : points) point.setX(min + max - point.x());
}

Graph buildGraph(const FlowchartData& data,
                 const QMap<QString, QSizeF>& measuredNodes,
                 const QMap<QString, FlowEdgeLabelLayout>& edgeLabels,
                 const QMap<QString, QSizeF>& renderedNodeSizes) {
  Graph graph;
  QSet<QString> subgraphIds;
  QHash<QString, QString> parent;
  for (const FlowSubgraph& group : data.subgraphs) subgraphIds.insert(group.id);
  for (const FlowSubgraph& group : data.subgraphs)
    for (const QString& child : group.nodes)
      if (subgraphIds.contains(child)) parent.insert(child, group.id);

  // Mermaid's layout data contains clusters in reverse declaration order.
  for (auto it = data.subgraphs.crbegin(); it != data.subgraphs.crend(); ++it) {
    Node node;
    node.id = it->id;
    node.group = true;
    node.padding = 8.0;
    node.parent = parent.value(it->id);
    graph.nodes.insert(node.id, node);
    graph.order.push_back(node.id);
  }
  for (const FlowVertex& vertex : data.vertices) {
    Node node;
    node.id = vertex.id;
    node.size = measuredNodes.value(vertex.id);
    node.renderSize = renderedNodeSizes.value(vertex.id, node.size);
    int bestDepth = -1;
    for (const FlowSubgraph& group : data.subgraphs) {
      if (!group.nodes.contains(vertex.id)) continue;
      int depth = 0;
      QString id = group.id;
      QSet<QString> seen;
      while (parent.contains(id) && !seen.contains(id)) {
        seen.insert(id);
        id = parent.value(id);
        ++depth;
      }
      if (depth > bestDepth) { node.parent = group.id; bestDepth = depth; }
    }
    graph.nodes.insert(node.id, node);
    graph.order.push_back(node.id);
  }
  QVector<QString> loose;
  for (const QString& id : graph.order) {
    const Node& node = graph.nodes.value(id);
    if (!node.group && node.parent.isEmpty()) loose.push_back(id);
  }
  if (!loose.isEmpty()) {
    Node lane;
    lane.id = QStringLiteral("__swimlane_default__");
    lane.group = true;
    graph.nodes.insert(lane.id, lane);
    graph.order.push_back(lane.id);
    for (const QString& id : loose) graph.nodes[id].parent = lane.id;
  }

  QVector<EdgeRef> layoutOnlyEdges;
  for (int i = 0; i < data.edges.size(); ++i) {
    const FlowEdge& edge = data.edges.at(i);
    if (!graph.nodes.contains(edge.start) || !graph.nodes.contains(edge.end)) continue;
    graph.edges.push_back({i, edge.id, edge.start, edge.end, false});
    if (edge.text.isEmpty()) continue;

    // Mermaid moves each edge label into a labelRect waypoint before running
    // the swimlane pipeline. The two virtual edges affect layering and order,
    // but are never routed or painted. Cross-lane labels belong to the target
    // lane; same-lane labels stay with the source.
    const QString labelId = QStringLiteral("edge-label-%1-%2-%3")
                                .arg(edge.start, edge.end, edge.id);
    Node label;
    label.id = labelId;
    label.size = edgeLabels.value(edge.id).size;
    label.parent = graph.nodes.value(edge.start).parent ==
                           graph.nodes.value(edge.end).parent
                       ? graph.nodes.value(edge.start).parent
                       : graph.nodes.value(edge.end).parent;
    label.dummy = true;
    graph.nodes.insert(labelId, label);
    graph.order.push_back(labelId);

    layoutOnlyEdges.push_back(
        {-1, edge.id + QStringLiteral("-to-label"), edge.start, labelId, false});
    layoutOnlyEdges.push_back(
        {-1, edge.id + QStringLiteral("-from-label"), labelId, edge.end, false});
  }
  graph.edges += layoutOnlyEdges;
  return graph;
}

}  // namespace

FlowLayoutResult layoutSwimlaneNodes(
    const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
    SwimlaneLayoutOptions options) {
  Graph graph = buildGraph(data, measuredNodes, options.preparedEdgeLabels,
                           options.renderedNodeSizes);
  removeCycles(graph);
  QString direction = data.direction.toUpper();
  if (direction.isEmpty()) direction = QStringLiteral("TB");
  if (options.ignoreCrossLaneEdges)
    assignRanks(graph, direction, true);
  else
    assignRanksGravity(graph, options.optimizeRanksByCrossings);
  QVector<QStringList> layers = buildLayers(graph, !options.ignoreCrossLaneEdges);
  insertDummies(graph, layers);
  const QStringList laneOrder = options.automaticLaneOrdering
      ? optimizeLaneOrder(graph) : sourceLaneOrder(graph);
  orderLayers(graph, layers, laneOrder);
  assignCoordinates(graph, layers, options.nodeSpacing, options.rankSpacing,
                    direction, laneOrder);

  QVector<QVector<QPointF>> routed(data.edges.size());
  QHash<int, QPointF> sourcePorts;
  const QHash<int, QPointF> targetPorts =
      distributedTargetPorts(graph, data, &sourcePorts);
  QSet<int> reversedEdges;
  for (const EdgeRef& edge : graph.edges)
    if (edge.sourceIndex >= 0 && edge.reversed)
      reversedEdges.insert(edge.sourceIndex);
  for (int i = 0; i < data.edges.size(); ++i) {
    std::optional<QPointF> sourcePort = sourcePorts.contains(i)
        ? std::optional<QPointF>(sourcePorts.value(i)) : std::nullopt;
    std::optional<QPointF> targetPort = targetPorts.contains(i)
        ? std::optional<QPointF>(targetPorts.value(i)) : std::nullopt;
    const FlowEdge& edge = data.edges.at(i);
    if (reversedEdges.contains(i)) {
      // DFS feedback arcs use the outermost left track in the untransformed
      // TB coordinate system. LR/RL/BT subsequently rotate or mirror this
      // materialized geometry just like Mermaid's post-processing pass.
      const Node& source = graph.nodes.value(edge.start);
      const Node& target = graph.nodes.value(edge.end);
      sourcePort = QPointF(nodeRect(source).left(), source.center.y());
      targetPort = QPointF(nodeRect(target).left(), target.center.y());
    }
    const QString labelId = QStringLiteral("edge-label-%1-%2-%3")
                                .arg(edge.start, edge.end, edge.id);
    routed[i] = !edge.text.isEmpty() && graph.nodes.contains(labelId)
        ? routeEdgeThroughLabel(graph, edge.start, edge.end, labelId)
        : routeEdge(graph, edge.start, edge.end, sourcePort, targetPort);
  }
  resolveSimpleCrossings(graph, data, routed,
                         !options.ignoreCrossLaneEdges);
  applyDirection(graph, routed, direction);

  QHash<QString, const FlowVertex*> vertices;
  for (const FlowVertex& vertex : data.vertices)
    vertices.insert(vertex.id, &vertex);
  if (direction == QLatin1String("LR") || direction == QLatin1String("RL")) {
    for (int i = 0; i < data.edges.size(); ++i) {
      QVector<QPointF>& points = routed[i];
      if (points.size() < 2) continue;
      const FlowEdge& edge = data.edges.at(i);
      const FlowVertex* sourceVertex = vertices.value(edge.start);
      const FlowVertex* targetVertex = vertices.value(edge.end);
      if (sourceVertex && graph.nodes.contains(edge.start)) {
        points.first() = intersectFlowShape(
            *sourceVertex, nodeRect(graph.nodes.value(edge.start)),
            points.at(1), options.look);
      }
      if (targetVertex && graph.nodes.contains(edge.end)) {
        points.last() = intersectFlowShape(
            *targetVertex, nodeRect(graph.nodes.value(edge.end)),
            points.at(points.size() - 2), options.look);
      }
    }
  }
  if (direction == QLatin1String("LR") || direction == QLatin1String("RL"))
    computeLrGroups(graph);
  else
    computeTbGroups(graph);
  liftTopLaneTitlesAboveRails(graph, routed, direction);
  if (direction == QLatin1String("RL"))
    mirrorHorizontal(graph, routed);

  FlowLayoutResult result;
  for (const FlowVertex& vertex : data.vertices) {
    const Node& source = graph.nodes.value(vertex.id);
    FlowLayoutNode node;
    node.id = vertex.id;
    node.x = source.center.x();
    node.y = source.center.y();
    node.width = source.size.width();
    node.height = source.size.height();
    node.renderWidth = source.renderSize.width();
    node.renderHeight = source.renderSize.height();
    node.rank = source.rank;
    result.nodes.push_back(node);
  }
  for (int i = 0; i < data.edges.size(); ++i) {
    const FlowEdge& edge = data.edges.at(i);
    FlowLayoutEdge out;
    out.id = edge.id;
    out.points = routed.value(i);
    QVector<QPointF> rendered = out.points;
    clipFlowEdgeForMarkers(rendered, edge.type);
    QString curve = edge.interpolate.isEmpty() ? options.curve : edge.interpolate;
    if (curve.isEmpty() || curve == QLatin1String("basis"))
      curve = QStringLiteral("rounded");
    out.path = d3curve::pathForCurve(rendered, curve);
    if (!edge.text.isEmpty()) {
      const FlowEdgeLabelLayout prepared = options.preparedEdgeLabels.value(edge.id);
      out.labelSize = prepared.size;
      out.labelDocument = prepared.document;
      const QString labelId = QStringLiteral("edge-label-%1-%2-%3")
                                  .arg(edge.start, edge.end, edge.id);
      if (graph.nodes.contains(labelId)) {
        out.hasLabelPosition = true;
        const QPointF anchor = anchorEdgeLabel(
            out.points, prepared.size, graph.nodes.value(labelId).center);
        out.labelX = anchor.x();
        out.labelY = anchor.y();
      } else if (!out.points.isEmpty()) {
        const int middle = (out.points.size() - 1) / 2;
        out.hasLabelPosition = true;
        out.labelX = (out.points.at(middle).x() + out.points.at(middle + 1).x()) / 2.0;
        out.labelY = (out.points.at(middle).y() + out.points.at(middle + 1).y()) / 2.0;
      }
    }
    result.edges.push_back(out);
  }
  applyLineJumps(result, data, options);
  for (auto it = data.subgraphs.crbegin(); it != data.subgraphs.crend(); ++it) {
    const Node& source = graph.nodes.value(it->id);
    FlowLayoutCluster cluster;
    cluster.id = it->id;
    cluster.x = source.center.x();
    cluster.y = source.center.y();
    cluster.width = source.size.width();
    cluster.height = source.size.height();
    cluster.swimlane = source.parent.isEmpty();
    cluster.titleOnLeft = cluster.swimlane && direction == QLatin1String("LR");
    cluster.titleBandSize = cluster.titleOnLeft
        ? options.labelLineHeight + 8.0 : options.labelLineHeight;
    result.clusters.push_back(cluster);
  }
  if (graph.nodes.contains(QStringLiteral("__swimlane_default__"))) {
    const Node& source = graph.nodes.value(QStringLiteral("__swimlane_default__"));
    result.clusters.push_back({QStringLiteral("__swimlane_default__"),
                               source.center.x(), source.center.y(),
                               source.size.width(), source.size.height(), true,
                               direction == QLatin1String("LR"), 0.0});
  }
  return result;
}

QMap<QString, QSizeF> measureSwimlaneHandDrawnNodes(
    const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
    quint32 handDrawnSeed) {
  QMap<QString, QSizeF> result = measuredNodes;
  for (const FlowVertex& vertex : data.vertices) {
    const QSizeF size = measuredNodes.value(vertex.id);
    if (!(size.width() > 0.0) || !(size.height() > 0.0)) continue;
    const FlowShapeGeometry geometry = flowShapeGeometry(vertex, size, FlowLook::HandDrawn);
    rough::Options options;
    options.seed = handDrawnSeed;
    options.roughness = 0.7;
    options.strokeWidth = 1.3;
    options.stroke = QStringLiteral("#000");
    options.fill = QStringLiteral("#000");
    options.fillStyle = QStringLiteral("hachure");
    options.fillWeight = 4.0;
    options.hachureGap = 5.2;
    rough::Drawable drawable;
    if (geometry.kind == QLatin1String("roundedRect") ||
        geometry.kind == QLatin1String("stadium")) {
      QPainterPath path;
      path.addRoundedRect(geometry.bounds, geometry.cornerRadius,
                          geometry.cornerRadius);
      drawable = rough::path(path, options);
    } else if (geometry.kind == QLatin1String("ellipse")) {
      drawable = rough::ellipse(geometry.bounds.center().x(), geometry.bounds.center().y(),
                                geometry.bounds.width(), geometry.bounds.height(), options);
    } else if (geometry.kind == QLatin1String("polygon") && !geometry.points.isEmpty()) {
      drawable = rough::polygon(geometry.points, options);
    } else {
      drawable = rough::rectangle(geometry.bounds.x(), geometry.bounds.y(),
                                  geometry.bounds.width(), geometry.bounds.height(), options);
    }
    const QRectF bounds = rough::tightBounds(drawable);
    if (bounds.isValid()) result[vertex.id] = bounds.size();
  }
  return result;
}

}  // namespace muffin::mermaid::flowchart
