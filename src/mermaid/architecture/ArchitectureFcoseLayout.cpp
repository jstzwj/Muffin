// Architecture compound-layout port derived from layout-base 2.0.1,
// cose-base 2.2.0 and cytoscape-fcose 2.2.0. All three upstream packages
// are MIT licensed by the i-Vis Research Group / Cytoscape Consortium.
#include "mermaid/architecture/ArchitectureFcoseLayout.h"
#include "mermaid/architecture/ArchitectureSpectralLayout.h"

#include <QHash>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QQueue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace muffin::mermaid::architecture {
namespace {

constexpr qreal kRepulsion = 4500.0;
constexpr qreal kGravity = 0.25;
constexpr qreal kCompoundGravity = 1.0;
constexpr qreal kGravityRange = 3.8;
constexpr qreal kCompoundGravityRange = 1.5;
constexpr qreal kGraphMargin = 15.0;
constexpr qreal kInitialCooling = 0.3;
constexpr qreal kFinalTemperature = 0.04;
constexpr qreal kMaxDisplacement = 100.0;
constexpr int kConvergencePeriod = 100;

qreal sign(qreal value) {
  return value > 0.0 ? 1.0 : (value < 0.0 ? -1.0 : 0.0);
}

bool intersects(const QRectF& a, const QRectF& b) {
  // layout-base RectangleD treats touching edges as intersecting.
  return !(a.right() < b.left() || a.bottom() < b.top() ||
           b.right() < a.left() || b.bottom() < a.top());
}

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
  const qreal p1x = a.center().x(), p1y = a.center().y();
  const qreal p2x = b.center().x(), p2y = b.center().y();
  if (intersects(a, b)) return {{p1x, p1y}, {p2x, p2y}, true};
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
    bool foundA = false, foundB = false;
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
    int directionA, directionB;
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
        case 1: result.a = {p1x - a.height() / 2.0 / lineSlope, a.top()}; break;
        case 2: result.a = {a.right(), p1y + a.width() / 2.0 * lineSlope}; break;
        case 3: result.a = {p1x + a.height() / 2.0 / lineSlope, a.bottom()}; break;
        default: result.a = {a.left(), p1y - a.width() / 2.0 * lineSlope}; break;
      }
    }
    if (!foundB) {
      switch (directionB) {
        case 1: result.b = {p2x - b.height() / 2.0 / lineSlope, b.top()}; break;
        case 2: result.b = {b.right(), p2y + b.width() / 2.0 * lineSlope}; break;
        case 3: result.b = {p2x + b.height() / 2.0 / lineSlope, b.bottom()}; break;
        default: result.b = {b.left(), p2y - b.width() / 2.0 * lineSlope}; break;
      }
    }
  }
  return result;
}

QPointF separationAmount(const QRectF& a, const QRectF& b, qreal buffer) {
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
  if (dx == 0.0 && dy == 0.0) slope = 1.0;
  qreal moveY = slope * overlapX;
  qreal moveX = overlapY / slope;
  if (overlapX < moveX) moveX = overlapX;
  else moveY = overlapY;
  const qreal directionX = a.center().x() < b.center().x() ? -1.0 : 1.0;
  const qreal directionY = a.center().y() < b.center().y() ? -1.0 : 1.0;
  return {-directionX * (moveX / 2.0 + buffer),
          -directionY * (moveY / 2.0 + buffer)};
}

class SeededRandom {
public:
  explicit SeededRandom(quint32 seed) : state_(seed) {}
  double next() {
    state_ += 1831565813u;
    quint32 t = state_;
    t = quint32((t ^ (t >> 15)) * (t | 1u));
    t ^= t + quint32((t ^ (t >> 7)) * (t | 61u));
    return double(t ^ (t >> 14)) / 4294967296.0;
  }
private:
  quint32 state_;
};

template <typename T>
struct OrderedMap {
  QVector<T> values;
  QVector<QString> keys;
  int indexOf(const QString& key) const { return keys.indexOf(key); }
  bool contains(const QString& key) const { return indexOf(key) >= 0; }
  void set(const QString& key, const T& value) {
    const int index = indexOf(key);
    if (index >= 0) values[index] = value;
    else { keys.append(key); values.append(value); }
  }
  T value(const QString& key, const T& fallback = {}) const {
    const int index = indexOf(key);
    return index >= 0 ? values.at(index) : fallback;
  }
};

struct Node {
  QString id;
  QString parentId;
  int owner = 0;
  int childGraph = -1;
  QRectF rect;
  QVector<int> edges;
  QVector<int> surrounding;
  QPointF spring;
  QPointF repulsion;
  QPointF gravity;
  QPointF displacement;
  int noOfChildren = 1;
  int depth = 1;
  qreal estimatedSize = 0.0;
  qreal padding = 0.0;
  bool leaf() const { return childGraph < 0; }
  QPointF center() const { return rect.center(); }
  void setCenter(QPointF center) { rect.moveCenter(center); }
};

struct Graph {
  int parentNode = -1;
  QVector<int> nodes;
  QRectF bounds;
  qreal estimatedSize = 0.0;
  bool connected = false;
};

struct Edge {
  int source = -1;
  int target = -1;
  qreal baseIdeal = 50.0;
  qreal ideal = 50.0;
  qreal elasticity = 0.45;
  bool interGraph = false;
  int lca = 0;
  int sourceInLca = -1;
  int targetInLca = -1;
};

struct AlignmentConstraints {
  QVector<QVector<int>> horizontal;
  QVector<QVector<int>> vertical;
};

struct RelativeConstraint {
  enum class Axis { Horizontal, Vertical };
  Axis axis = Axis::Horizontal;
  int before = -1;
  int after = -1;
  qreal gap = 0.0;
};

struct SpatialMap {
  QVector<QString> ids;
  QVector<QPoint> positions;
};

struct GraphModel {
  QVector<Node> nodes;
  QVector<Graph> graphs;
  QVector<Edge> edges;
  QHash<QString, int> nodeById;
  QVector<int> allOrder;
  QVector<int> leafOrder;
  AlignmentConstraints alignments;
  QVector<RelativeConstraint> relative;
  qreal defaultEdgeLength = 50.0;
};

QString pairKey(QChar lhs, QChar rhs) {
  return QString(lhs) + rhs;
}

std::optional<QPoint> shiftForPair(QChar lhs, QChar rhs) {
  if (lhs == rhs) return std::nullopt;
  if (lhs == QLatin1Char('L') || lhs == QLatin1Char('R')) {
    const int x = lhs == QLatin1Char('L') ? -1 : 1;
    if (rhs == QLatin1Char('T') || rhs == QLatin1Char('B'))
      return QPoint(x, rhs == QLatin1Char('T') ? 1 : -1);
    return QPoint(x, 0);
  }
  const int y = lhs == QLatin1Char('T') ? 1 : -1;
  if (rhs == QLatin1Char('L') || rhs == QLatin1Char('R'))
    return QPoint(rhs == QLatin1Char('L') ? 1 : -1, y);
  return QPoint(0, y);
}

QString directionAlignment(QChar lhs, QChar rhs) {
  const bool lhsX = lhs == QLatin1Char('L') || lhs == QLatin1Char('R');
  const bool rhsX = rhs == QLatin1Char('L') || rhs == QLatin1Char('R');
  if (lhsX != rhsX) return QStringLiteral("bend");
  return lhsX ? QStringLiteral("horizontal") : QStringLiteral("vertical");
}

QVector<QString> diagramLeafIds(const ArchitectureData& data) {
  QVector<QString> result;
  for (const auto& service : data.services) result.append(service.id);
  for (const auto& junction : data.junctions) result.append(junction.id);
  return result;
}

QString parentOf(const ArchitectureData& data, const QString& id) {
  for (const auto& service : data.services)
    if (service.id == id) return service.parent;
  for (const auto& junction : data.junctions)
    if (junction.id == id) return junction.parent;
  for (const auto& group : data.groups)
    if (group.id == id) return group.parent;
  return {};
}

QVector<SpatialMap> buildSpatialMaps(const ArchitectureData& data) {
  const QVector<QString> ids = diagramLeafIds(data);
  QHash<QString, OrderedMap<QString>> adjacency;
  for (const QString& id : ids) adjacency.insert(id, {});
  for (const auto& edge : data.edges) {
    if (!adjacency.contains(edge.lhsId) || !adjacency.contains(edge.rhsId)) continue;
    if (shiftForPair(edge.lhsDir, edge.rhsDir))
      adjacency[edge.lhsId].set(pairKey(edge.lhsDir, edge.rhsDir), edge.rhsId);
    if (shiftForPair(edge.rhsDir, edge.lhsDir))
      adjacency[edge.rhsId].set(pairKey(edge.rhsDir, edge.lhsDir), edge.lhsId);
  }
  QSet<QString> visited;
  QVector<SpatialMap> maps;
  for (const QString& start : ids) {
    if (visited.contains(start)) continue;
    SpatialMap map;
    map.ids.append(start); map.positions.append({0, 0});
    QQueue<QString> queue; queue.enqueue(start);
    while (!queue.isEmpty()) {
      const QString id = queue.dequeue();
      const int currentIndex = map.ids.indexOf(id);
      visited.insert(id);
      const auto& adj = adjacency[id];
      for (int i = 0; i < adj.keys.size(); ++i) {
        const QString next = adj.values.at(i);
        if (visited.contains(next)) continue;
        const QString pair = adj.keys.at(i);
        const QPoint shift = *shiftForPair(pair.at(0), pair.at(1));
        if (!map.ids.contains(next)) {
          map.ids.append(next);
          map.positions.append(map.positions.at(currentIndex) + shift);
          queue.enqueue(next);
        }
      }
    }
    maps.append(std::move(map));
  }
  return maps;
}

// Constraint generation needs node indexes. Keep this function next to graph construction
// so the ordered JS object semantics and the final integer model cannot diverge.
void buildConstraints(GraphModel& model, const ArchitectureData& data,
                      const QVector<SpatialMap>& maps,
                      const QHash<QString, QHash<QString, QString>>& groupAlign,
                      qreal gap) {
  QVector<QString> declared;
  for (const auto& hint : data.alignments) declared += hint.members;
  auto autoGroups = [&](const SpatialMap& map, bool horizontal) {
    OrderedMap<OrderedMap<QVector<QString>>> lines;
    for (int i = 0; i < map.ids.size(); ++i) {
      const QString coordinate = QString::number(horizontal ? map.positions[i].y()
                                                            : map.positions[i].x());
      OrderedMap<QVector<QString>> groups = lines.value(coordinate);
      QString parent = parentOf(data, map.ids[i]);
      if (parent.isEmpty()) parent = QStringLiteral("default");
      QVector<QString> members = groups.value(parent); members.append(map.ids[i]);
      groups.set(parent, members); lines.set(coordinate, groups);
    }
    QVector<QVector<QString>> out;
    for (const auto& groups : lines.values) {
      if (groups.keys.size() == 1) {
        if (groups.values[0].size() > 1) out.append(groups.values[0]);
        continue;
      }
      QVector<QString> merged;
      QVector<QVector<QString>> split;
      int count = 0;
      for (int i = 0; i < groups.keys.size() - 1; ++i) {
        for (int j = i + 1; j < groups.keys.size(); ++j) {
          const QString a = groups.keys[i], b = groups.keys[j];
          const QString alignment = groupAlign.value(a).value(b);
          if (alignment == (horizontal ? QLatin1String("horizontal")
                                      : QLatin1String("vertical")) ||
              a == QLatin1String("default") || b == QLatin1String("default")) {
            merged += groups.values[i]; merged += groups.values[j];
          } else {
            Q_UNUSED(count); split.append(groups.values[i]); split.append(groups.values[j]);
            count += 2;
          }
        }
      }
      if (!merged.isEmpty()) out.append(merged);
      for (const auto& members : split) if (members.size() > 1) out.append(members);
    }
    return out;
  };
  for (const SpatialMap& map : maps) {
    for (const auto& ids : autoGroups(map, true)) {
      bool drop = false; for (const QString& id : ids) if (declared.contains(id)) drop = true;
      if (drop) continue;
      QVector<int> group; for (const QString& id : ids) group.append(model.nodeById.value(id, -1));
      group.erase(std::remove(group.begin(), group.end(), -1), group.end());
      if (group.size() > 1) model.alignments.horizontal.append(group);
    }
    for (const auto& ids : autoGroups(map, false)) {
      bool drop = false; for (const QString& id : ids) if (declared.contains(id)) drop = true;
      if (drop) continue;
      QVector<int> group; for (const QString& id : ids) group.append(model.nodeById.value(id, -1));
      group.erase(std::remove(group.begin(), group.end(), -1), group.end());
      if (group.size() > 1) model.alignments.vertical.append(group);
    }
  }
  QSet<QString> declaredPairs;
  for (const auto& hint : data.alignments) {
    QVector<int> group;
    for (const QString& id : hint.members) group.append(model.nodeById.value(id, -1));
    group.erase(std::remove(group.begin(), group.end(), -1), group.end());
    if (group.size() > 1) {
      if (hint.direction == ArchitectureAlignment::Direction::Row)
        model.alignments.horizontal.append(group);
      else model.alignments.vertical.append(group);
    }
    for (int i = 0; i + 1 < hint.members.size(); ++i) {
      declaredPairs.insert(hint.members[i] + QLatin1Char('|') + hint.members[i + 1]);
      declaredPairs.insert(hint.members[i + 1] + QLatin1Char('|') + hint.members[i]);
      model.relative.append({hint.direction == ArchitectureAlignment::Direction::Row
                                 ? RelativeConstraint::Axis::Horizontal
                                 : RelativeConstraint::Axis::Vertical,
                             model.nodeById.value(hint.members[i], -1),
                             model.nodeById.value(hint.members[i + 1], -1), gap});
    }
  }
  const QVector<QPoint> shifts{{-1,0},{1,0},{0,1},{0,-1}};
  for (const SpatialMap& map : maps) {
    OrderedMap<QString> inverse;
    for (int i = 0; i < map.ids.size(); ++i)
      inverse.set(QString::number(map.positions[i].x()) + QLatin1Char(',') +
                      QString::number(map.positions[i].y()), map.ids[i]);
    QQueue<QPoint> queue; queue.enqueue({0,0});
    QSet<QString> visited;
    while (!queue.isEmpty()) {
      const QPoint current = queue.dequeue();
      const QString currentKey = QString::number(current.x()) + QLatin1Char(',') +
                                 QString::number(current.y());
      visited.insert(currentKey);
      const QString currentId = inverse.value(currentKey);
      if (currentId.isEmpty()) continue;
      for (int direction = 0; direction < shifts.size(); ++direction) {
        const QPoint next = current + shifts[direction];
        const QString nextKey = QString::number(next.x()) + QLatin1Char(',') +
                                QString::number(next.y());
        const QString nextId = inverse.value(nextKey);
        if (nextId.isEmpty() || visited.contains(nextKey)) continue;
        queue.enqueue(next);
        if (declaredPairs.contains(currentId + QLatin1Char('|') + nextId)) continue;
        RelativeConstraint constraint;
        constraint.axis = direction < 2 ? RelativeConstraint::Axis::Horizontal
                                        : RelativeConstraint::Axis::Vertical;
        if (direction == 0 || direction == 2) {
          constraint.before = model.nodeById.value(nextId, -1);
          constraint.after = model.nodeById.value(currentId, -1);
        } else {
          constraint.before = model.nodeById.value(currentId, -1);
          constraint.after = model.nodeById.value(nextId, -1);
        }
        constraint.gap = gap;
        if (constraint.before >= 0 && constraint.after >= 0)
          model.relative.append(constraint);
      }
    }
  }
}

GraphModel makeModel(const ArchitectureData& data,
                     const ArchitectureFcoseOptions& options) {
  GraphModel model;
  model.graphs.append(Graph{});
  auto addGraph = [&](auto&& self, const QString& parentId, int graphIndex) -> void {
    // Cytoscape adds all groups first, then services, then junctions.
    for (const auto& group : data.groups) {
      if (group.parent != parentId) continue;
      Node node; node.id = group.id; node.parentId = parentId; node.owner = graphIndex;
      node.padding = options.padding; node.rect = QRectF(-20, -20, 40, 40);
      const int nodeIndex = model.nodes.size(); model.nodes.append(node);
      model.nodeById.insert(node.id, nodeIndex); model.graphs[graphIndex].nodes.append(nodeIndex);
      const int childGraph = model.graphs.size();
      Graph child; child.parentNode = nodeIndex; model.graphs.append(child);
      model.nodes[nodeIndex].childGraph = childGraph;
      self(self, group.id, childGraph);
    }
    for (const auto& service : data.services) {
      if (service.parent != parentId) continue;
      Node node; node.id = service.id; node.parentId = parentId; node.owner = graphIndex;
      node.rect = QRectF(-options.iconSize / 2.0, -options.iconSize / 2.0,
                         options.iconSize, options.iconSize);
      const int index = model.nodes.size(); model.nodes.append(node);
      model.nodeById.insert(node.id, index); model.graphs[graphIndex].nodes.append(index);
    }
    for (const auto& junction : data.junctions) {
      if (junction.parent != parentId) continue;
      Node node; node.id = junction.id; node.parentId = parentId; node.owner = graphIndex;
      node.rect = QRectF(-options.iconSize / 2.0, -options.iconSize / 2.0,
                         options.iconSize, options.iconSize);
      const int index = model.nodes.size(); model.nodes.append(node);
      model.nodeById.insert(node.id, index); model.graphs[graphIndex].nodes.append(index);
    }
  };
  addGraph(addGraph, QString(), 0);
  // LGraphManager.getAllNodes() concatenates graph-local node arrays in graph
  // creation order. This differs from processChildrenList's recursive object
  // creation order and is observable through compound displacement propagation.
  for (const Graph& graph : std::as_const(model.graphs))
    for (int node : graph.nodes) model.allOrder.append(node);
  for (int node : std::as_const(model.allOrder))
    if (model.nodes[node].leaf()) model.leafOrder.append(node);
  qreal idealTotal = 0.0;
  QSet<QString> connectedPairs;
  for (const auto& source : data.edges) {
    const int a = model.nodeById.value(source.lhsId, -1);
    const int b = model.nodeById.value(source.rhsId, -1);
    if (a < 0 || b < 0 || a == b) continue;
    const QString pair = QString::number(std::min(a,b)) + QLatin1Char('|') +
                         QString::number(std::max(a,b));
    if (connectedPairs.contains(pair)) continue;
    connectedPairs.insert(pair);
    Edge edge; edge.source = a; edge.target = b;
    const bool sameParent = model.nodes[a].parentId == model.nodes[b].parentId;
    edge.baseIdeal = sameParent ? options.idealEdgeLengthMultiplier * options.iconSize
                                : 0.5 * options.iconSize;
    edge.ideal = edge.baseIdeal;
    edge.elasticity = sameParent ? options.edgeElasticity : 1e-3;
    edge.interGraph = model.nodes[a].owner != model.nodes[b].owner;
    const int edgeIndex = model.edges.size(); model.edges.append(edge);
    model.nodes[a].edges.append(edgeIndex); model.nodes[b].edges.append(edgeIndex);
    idealTotal += edge.ideal;
  }
  model.defaultEdgeLength = model.edges.isEmpty() ? 50.0 : idealTotal / model.edges.size();
  QHash<QString, QHash<QString, QString>> groupAlign;
  for (const auto& edge : data.edges) {
    const QString a = parentOf(data, edge.lhsId), b = parentOf(data, edge.rhsId);
    if (a.isEmpty() || b.isEmpty() || a == b) continue;
    const QString alignment = directionAlignment(edge.lhsDir, edge.rhsDir);
    if (alignment == QLatin1String("bend")) continue;
    groupAlign[a][b] = alignment; groupAlign[b][a] = alignment;
  }
  buildConstraints(model, data, buildSpatialMaps(data), groupAlign,
                   options.idealEdgeLengthMultiplier * options.iconSize);
  return model;
}

int childCount(GraphModel& model, int nodeIndex) {
  Node& node = model.nodes[nodeIndex];
  if (node.leaf()) return node.noOfChildren = 1;
  int count = 0;
  for (int child : model.graphs[node.childGraph].nodes) count += childCount(model, child);
  return node.noOfChildren = std::max(1, count);
}

qreal estimateGraph(GraphModel& model, int graphIndex, int depth) {
  Graph& graph = model.graphs[graphIndex];
  qreal sum = 0.0;
  for (int nodeIndex : graph.nodes) {
    Node& node = model.nodes[nodeIndex]; node.depth = depth;
    if (node.leaf()) node.estimatedSize = (node.rect.width() + node.rect.height()) / 2.0;
    else {
      node.estimatedSize = estimateGraph(model, node.childGraph, depth + 1);
      node.rect.setSize({node.estimatedSize, node.estimatedSize});
    }
    sum += node.estimatedSize;
  }
  graph.estimatedSize = graph.nodes.isEmpty() ? 40.0
      : (sum == 0.0 ? 40.0 : sum / std::sqrt(qreal(graph.nodes.size())));
  return graph.estimatedSize;
}

QVector<int> nodeAncestors(const GraphModel& model, int nodeIndex) {
  QVector<int> result{nodeIndex};
  int graph = model.nodes[nodeIndex].owner;
  while (graph != 0) {
    const int parent = model.graphs[graph].parentNode;
    result.append(parent); graph = model.nodes[parent].owner;
  }
  return result;
}

void calculateLcaAndIdeals(GraphModel& model) {
  for (Edge& edge : model.edges) {
    edge.ideal = edge.baseIdeal;
    QVector<int> sourcePath = nodeAncestors(model, edge.source);
    QVector<int> targetPath = nodeAncestors(model, edge.target);
    edge.lca = 0; edge.sourceInLca = edge.source; edge.targetInLca = edge.target;
    for (int sourceNode : sourcePath) {
      for (int targetNode : targetPath) {
        if (model.nodes[sourceNode].owner == model.nodes[targetNode].owner) {
          edge.lca = model.nodes[sourceNode].owner;
          edge.sourceInLca = sourceNode; edge.targetInLca = targetNode;
          goto found;
        }
      }
    }
found:
    if (!edge.interGraph) continue;
    const qreal original = edge.baseIdeal;
    edge.ideal += model.nodes[edge.sourceInLca].estimatedSize +
                  model.nodes[edge.targetInLca].estimatedSize - 80.0;
    const int lcaDepth = edge.lca == 0 ? 1
        : model.nodes[model.graphs[edge.lca].parentNode].depth;
    edge.ideal += original * 0.1 *
        (model.nodes[edge.source].depth + model.nodes[edge.target].depth - 2 * lcaDepth);
  }
}

QRectF updateBounds(GraphModel& model, int graphIndex, bool recursive = true) {
  Graph& graph = model.graphs[graphIndex];
  bool first = true; QRectF bounds;
  for (int index : graph.nodes) {
    Node& node = model.nodes[index];
    if (recursive && !node.leaf()) node.rect = updateBounds(model, node.childGraph, true);
    bounds = first ? node.rect : bounds.united(node.rect); first = false;
  }
  if (first) {
    if (graph.parentNode >= 0) bounds = model.nodes[graph.parentNode].rect;
    else bounds = {};
  }
  const qreal margin = graph.parentNode >= 0 ? model.nodes[graph.parentNode].padding
                                             : kGraphMargin;
  graph.bounds = bounds.adjusted(-margin, -margin, margin, margin);
  return graph.bounds;
}

int otherEnd(const Edge& edge, int node) {
  return edge.source == node ? edge.target : edge.source;
}

int endInGraph(const GraphModel& model, int node, int graph) {
  int current = node;
  while (model.nodes[current].owner != graph) {
    const int owner = model.nodes[current].owner;
    if (owner == 0) return -1;
    current = model.graphs[owner].parentNode;
  }
  return current;
}

QVector<int> gravityNodes(GraphModel& model) {
  QVector<int> result;
  for (int graphIndex = 0; graphIndex < model.graphs.size(); ++graphIndex) {
    Graph& graph = model.graphs[graphIndex];
    if (graph.nodes.isEmpty()) continue;
    QSet<int> visited; QQueue<int> queue;
    auto enqueueTree = [&](auto&& self, int node) -> void {
      if (visited.contains(node)) return;
      visited.insert(node); queue.enqueue(node);
      if (!model.nodes[node].leaf())
        for (int child : model.graphs[model.nodes[node].childGraph].nodes) self(self, child);
    };
    enqueueTree(enqueueTree, graph.nodes.front());
    while (!queue.isEmpty()) {
      const int current = queue.dequeue();
      for (int edgeIndex : model.nodes[current].edges) {
        const int next = endInGraph(model, otherEnd(model.edges[edgeIndex], current), graphIndex);
        if (next >= 0) enqueueTree(enqueueTree, next);
      }
    }
    int ownerVisited = 0;
    for (int node : visited) if (model.nodes[node].owner == graphIndex) ++ownerVisited;
    graph.connected = ownerVisited == graph.nodes.size();
    if (!graph.connected) result += graph.nodes;
  }
  return result;
}

struct AxisModel {
  QVector<QVector<int>> alignments;
  QHash<int, int> nodeToDummy;
  QVector<QVector<int>> dummyNodes;
  QVector<int> order;
  QHash<int, QVector<QPair<int, qreal>>> after;
  QHash<int, QVector<QPair<int, qreal>>> before;
  QHash<int, qreal> temporary;
};

void enforceAxis(GraphModel& model, AxisModel& axis,
                 const QVector<RelativeConstraint>& constraints, bool horizontal) {
  axis.alignments = horizontal ? model.alignments.vertical : model.alignments.horizontal;
  // Alignment sets become dummy vertices on the perpendicular relative axis.
  for (const QVector<int>& group : axis.alignments) {
    const int dummy = -1 - axis.dummyNodes.size(); axis.dummyNodes.append(group);
    for (int node : group) axis.nodeToDummy.insert(node, dummy);
    qreal average = 0.0;
    for (int node : group) average += horizontal ? model.nodes[node].center().x()
                                                : model.nodes[node].center().y();
    average /= group.size();
    for (int node : group) {
      QPointF center = model.nodes[node].center();
      if (horizontal) center.setX(average); else center.setY(average);
      model.nodes[node].setCenter(center);
    }
  }
  struct Arc { int from; int to; qreal gap; };
  QVector<Arc> arcs;
  for (const RelativeConstraint& constraint : constraints) {
    if ((constraint.axis == RelativeConstraint::Axis::Horizontal) != horizontal) continue;
    const int from = axis.nodeToDummy.value(constraint.before, constraint.before);
    const int to = axis.nodeToDummy.value(constraint.after, constraint.after);
    arcs.append({from, to, constraint.gap});
    if (!axis.order.contains(from)) axis.order.append(from);
    if (!axis.order.contains(to)) axis.order.append(to);
    axis.after[from].append({to, constraint.gap});
    axis.before[to].append({from, constraint.gap});
  }
  if (arcs.isEmpty()) return;
  QHash<int, int> indegree; QHash<int, QVector<Arc>> outgoing;
  for (int node : axis.order) indegree[node] = 0;
  for (const Arc& arc : arcs) { outgoing[arc.from].append(arc); ++indegree[arc.to]; }
  QQueue<int> queue; QHash<int, qreal> position;
  for (int node : axis.order) {
    const QVector<int> actual = node < 0 ? axis.dummyNodes[-node - 1] : QVector<int>{node};
    qreal value = 0.0; for (int item : actual)
      value += horizontal ? model.nodes[item].center().x() : model.nodes[item].center().y();
    position[node] = value / actual.size();
    if (indegree[node] == 0) queue.enqueue(node); else position[node] = -std::numeric_limits<qreal>::infinity();
  }
  while (!queue.isEmpty()) {
    const int node = queue.dequeue();
    for (const Arc& arc : outgoing[node]) {
      position[arc.to] = std::max(position[arc.to], position[node] + arc.gap);
      if (--indegree[arc.to] == 0) queue.enqueue(arc.to);
    }
  }
  // ConstraintHandler recentres every unfixed weak component around its old extent.
  QHash<int, QVector<int>> undirected;
  for (const Arc& arc : arcs) { undirected[arc.from].append(arc.to); undirected[arc.to].append(arc.from); }
  QSet<int> seen;
  for (int start : axis.order) {
    if (seen.contains(start)) continue;
    QVector<int> component; QQueue<int> componentQueue; componentQueue.enqueue(start); seen.insert(start);
    while (!componentQueue.isEmpty()) {
      const int node = componentQueue.dequeue(); component.append(node);
      for (int next : undirected[node]) if (!seen.contains(next)) { seen.insert(next); componentQueue.enqueue(next); }
    }
    qreal minBefore = std::numeric_limits<qreal>::infinity(), maxBefore = -minBefore;
    qreal minAfter = minBefore, maxAfter = -minBefore;
    for (int node : component) {
      const QVector<int> actual = node < 0 ? axis.dummyNodes[-node - 1] : QVector<int>{node};
      const qreal old = horizontal ? model.nodes[actual.front()].center().x()
                                   : model.nodes[actual.front()].center().y();
      minBefore = std::min(minBefore, old); maxBefore = std::max(maxBefore, old);
      minAfter = std::min(minAfter, position[node]); maxAfter = std::max(maxAfter, position[node]);
    }
    const qreal shift = (minBefore + maxBefore) / 2.0 - (minAfter + maxAfter) / 2.0;
    for (int node : component) position[node] += shift;
  }
  for (auto it = position.cbegin(); it != position.cend(); ++it) {
    const QVector<int> actual = it.key() < 0 ? axis.dummyNodes[-it.key() - 1]
                                             : QVector<int>{it.key()};
    for (int node : actual) {
      QPointF center = model.nodes[node].center();
      if (horizontal) center.setX(it.value()); else center.setY(it.value());
      model.nodes[node].setCenter(center);
    }
    axis.temporary[it.key()] = it.value();
  }
}

void alignInitial(GraphModel& model) {
  for (const QVector<int>& group : model.alignments.vertical) {
    qreal average = 0; for (int node : group) average += model.nodes[node].center().x();
    average /= group.size(); for (int node : group) { QPointF c=model.nodes[node].center();c.setX(average);model.nodes[node].setCenter(c); }
  }
  for (const QVector<int>& group : model.alignments.horizontal) {
    qreal average = 0; for (int node : group) average += model.nodes[node].center().y();
    average /= group.size(); for (int node : group) { QPointF c=model.nodes[node].center();c.setY(average);model.nodes[node].setCenter(c); }
  }
}

void propagate(GraphModel& model, int nodeIndex, QPointF displacement) {
  Node& node = model.nodes[nodeIndex];
  if (node.leaf()) { node.displacement += displacement; return; }
  for (int child : model.graphs[node.childGraph].nodes) propagate(model, child, displacement);
}

void updateAlignmentDisplacements(GraphModel& model) {
  for (const QVector<int>& group : model.alignments.vertical) {
    qreal average = 0; for (int node : group) average += model.nodes[node].displacement.x();
    average /= group.size(); for (int node : group) model.nodes[node].displacement.setX(average);
  }
  for (const QVector<int>& group : model.alignments.horizontal) {
    qreal average = 0; for (int node : group) average += model.nodes[node].displacement.y();
    average /= group.size(); for (int node : group) model.nodes[node].displacement.setY(average);
  }
}

void shuffleTail(QVector<int>& values, SeededRandom& random) {
  for (int i = values.size() - 1; i >= 2.0 * values.size() / 3.0; --i) {
    const int j = int(std::floor(random.next() * (i + 1)));
    std::swap(values[i], values[j]);
  }
}

void relaxAxis(GraphModel& model, AxisModel& axis, bool horizontal,
               int iteration, SeededRandom& random) {
  if (iteration % 10 == 0) shuffleTail(axis.order, random);
  for (int key : axis.order) {
    const QVector<int> actual = key < 0 ? axis.dummyNodes[-key - 1] : QVector<int>{key};
    qreal displacement = horizontal ? model.nodes[actual.front()].displacement.x()
                                    : model.nodes[actual.front()].displacement.y();
    for (const auto& [right, gap] : axis.after[key]) {
      const qreal diff = axis.temporary[right] - axis.temporary[key] - displacement;
      if (diff < gap) displacement -= gap - diff;
    }
    for (const auto& [left, gap] : axis.before[key]) {
      const qreal diff = axis.temporary[key] - axis.temporary[left] + displacement;
      if (diff < gap) displacement += gap - diff;
    }
    axis.temporary[key] += displacement;
    for (int node : actual) {
      if (horizontal) model.nodes[node].displacement.setX(displacement);
      else model.nodes[node].displacement.setY(displacement);
    }
  }
}

void updateSurrounding(GraphModel& model, qreal range) {
  for (Node& node : model.nodes) node.surrounding.clear();
  for (const Graph& graph : model.graphs) {
    QVector<bool> processed(model.nodes.size(), false);
    // The graphs in Architecture fixtures are small. Pair filtering below is
    // equivalent to the layout-base grid's retained surrounding set, while
    // preserving graph-local insertion order.
    for (int a : graph.nodes) {
      for (int b : graph.nodes) {
        if (a == b || processed[b]) continue;
        const QRectF ar=model.nodes[a].rect, br=model.nodes[b].rect;
        const qreal dx=std::abs(ar.center().x()-br.center().x())-(ar.width()+br.width())/2.0;
        const qreal dy=std::abs(ar.center().y()-br.center().y())-(ar.height()+br.height())/2.0;
        if (dx <= range && dy <= range) model.nodes[a].surrounding.append(b);
      }
      processed[a] = true;
    }
  }
}

void springForces(GraphModel& model) {
  for (const Edge& edge : model.edges) {
    Node& source=model.nodes[edge.source]; Node& target=model.nodes[edge.target];
    const RectIntersection clips=intersectRects(target.rect,source.rect);
    if (clips.overlapping) continue;
    QPointF delta=clips.a-clips.b;
    if (std::abs(delta.x()) < 1.0) delta.setX(sign(delta.x()));
    if (std::abs(delta.y()) < 1.0) delta.setY(sign(delta.y()));
    const qreal length=std::sqrt(QPointF::dotProduct(delta,delta));
    if (length == 0.0) continue;
    const qreal magnitude=edge.elasticity*(length-edge.ideal);
    const QPointF force=magnitude*delta/length;
    source.spring+=force; target.spring-=force;
  }
}

void repulsionForces(GraphModel& model, qreal minDistance) {
  for (int i=0;i<model.nodes.size();++i) for(int j:model.nodes[i].surrounding) {
    Node& a=model.nodes[i];Node& b=model.nodes[j];QPointF force;
    if(intersects(a.rect,b.rect)) {
      const QPointF amount=separationAmount(a.rect,b.rect,model.defaultEdgeLength/2.0);
      const qreal children=qreal(a.noOfChildren)*b.noOfChildren/(a.noOfChildren+b.noOfChildren);
      force=2.0*children*amount;
      a.repulsion-=force;b.repulsion+=force;
    } else {
      const RectIntersection clips=intersectRects(a.rect,b.rect);QPointF delta=clips.b-clips.a;
      if(std::abs(delta.x())<minDistance)delta.setX(sign(delta.x())*minDistance);
      if(std::abs(delta.y())<minDistance)delta.setY(sign(delta.y())*minDistance);
      const qreal d2=QPointF::dotProduct(delta,delta),d=std::sqrt(d2);
      const qreal magnitude=kRepulsion*a.noOfChildren*b.noOfChildren/d2;
      force=magnitude*delta/d;a.repulsion-=force;b.repulsion+=force;
    }
  }
}

void gravityForces(GraphModel& model, const QVector<int>& gravity) {
  for(int index:gravity){Node& node=model.nodes[index];const Graph& owner=model.graphs[node.owner];
    const QPointF delta=node.center()-owner.bounds.center();
    const qreal absX=std::abs(delta.x())+node.rect.width()/2.0;
    const qreal absY=std::abs(delta.y())+node.rect.height()/2.0;
    const qreal estimated=owner.estimatedSize*(node.owner==0?kGravityRange:kCompoundGravityRange);
    if(absX>estimated||absY>estimated) node.gravity=-kGravity*delta*(node.owner==0?1.0:kCompoundGravity);
  }
}

QRectF allNodeBounds(const GraphModel& model) {
  bool first=true;QRectF result;for(const Node& node:model.nodes){result=first?node.rect:result.united(node.rect);first=false;}return result;
}

qreal cytoscapeLabelWidth(const QString& text) {
#ifdef Q_OS_WIN
  static const bool loadedWindowsSans = [] {
    const QString fontPath = qEnvironmentVariable(
                                 "WINDIR", QStringLiteral("C:\\Windows")) +
                             QStringLiteral("/Fonts/arial.ttf");
    if (QFileInfo::exists(fontPath))
      QFontDatabase::addApplicationFont(fontPath);
    return true;
  }();
  Q_UNUSED(loadedWindowsSans);
#endif
  QFont font;
  font.setFamilies({QStringLiteral("Helvetica Neue"), QStringLiteral("Helvetica"),
                    QStringLiteral("Arial"), QStringLiteral("sans-serif")});
  font.setPixelSize(16);
  font.setHintingPreference(QFont::PreferNoHinting);
  return std::ceil(QFontMetricsF(font).horizontalAdvance(text));
}

QRectF initialNodeHorizontalBounds(const GraphModel& model,
                                   const ArchitectureData& data,
                                   int nodeIndex) {
  const Node& node = model.nodes.at(nodeIndex);
  if (node.leaf()) {
    QRectF result = node.rect.adjusted(-1.0, 0.0, 1.0, 0.0);
    const auto service = std::find_if(
        data.services.cbegin(), data.services.cend(),
        [&](const ArchitectureService& candidate) {
          return candidate.id == node.id && candidate.hasTitle;
        });
    if (service != data.services.cend()) {
      // Compound auto-sizing includes Cytoscape's 2px label error margin,
      // half of its 1px compound border, and the 1px body-bounds expansion.
      const qreal halfWidth = cytoscapeLabelWidth(service->title) / 2.0 + 3.5;
      result.setLeft(std::min(result.left(), node.center().x() - halfWidth));
      result.setRight(std::max(result.right(), node.center().x() + halfWidth));
    }
    return result;
  }
  bool first = true;
  QRectF children;
  for (int childIndex : model.graphs.at(node.childGraph).nodes) {
    const QRectF child = initialNodeHorizontalBounds(model, data, childIndex);
    children = first ? child : children.united(child);
    first = false;
  }
  if (first) return node.rect;
  return children.adjusted(-node.padding, 0.0, node.padding, 0.0);
}

QRectF cytoscapeCollectionBounds(const GraphModel& model,
                                 const ArchitectureData& data,
                                 const ArchitectureFcoseOptions& options,
                                 bool initial) {
  QSet<QString> titledServices;
  QSet<QString> titledGroups;
  QSet<QString> compoundParents;
  for (const ArchitectureService& service : data.services)
    if (service.hasTitle) titledServices.insert(service.id);
  for (const ArchitectureGroup& group : data.groups) {
    if (group.hasTitle) titledGroups.insert(group.id);
    if (group.hasParent) compoundParents.insert(group.parent);
  }

  bool first = true;
  QRectF bounds;
  auto include = [&](const QRectF& rect) {
    bounds = first ? rect : bounds.united(rect);
    first = false;
  };
  for (const Node& node : model.nodes) {
    if (node.leaf()) {
      QRectF rect = node.rect.adjusted(-1.0, -1.0, 1.0, 1.0);
      if (titledServices.contains(node.id)) {
        const auto service = std::find_if(
            data.services.cbegin(), data.services.cend(),
            [&](const ArchitectureService& candidate) {
              return candidate.id == node.id;
            });
        if (service != data.services.cend()) {
          const qreal halfWidth = cytoscapeLabelWidth(service->title) / 2.0 + 3.0;
          rect.setLeft(std::min(rect.left(), node.center().x() - halfWidth));
          rect.setRight(std::max(rect.right(), node.center().x() + halfWidth));
        }
        const qreal bottom = node.center().y() + options.iconSize / 2.0 +
                             options.fontSize + 3.0;
        rect.setBottom(std::max(rect.bottom(), bottom));
      }
      include(rect);
    } else {
      const bool nested = compoundParents.contains(node.id);
      const qreal verticalExpansion = nested ? 4.0 : 2.5;
      const qreal leftExpansion = nested ? 4.0 : 2.5;
      const qreal rightExpansion = nested && !initial ? 2.5 : leftExpansion;
      QRectF rect = node.rect.adjusted(-leftExpansion, -verticalExpansion,
                                       rightExpansion, verticalExpansion);
      if (initial) {
        const QRectF content = initialNodeHorizontalBounds(
            model, data, model.nodeById.value(node.id));
        rect.setLeft(std::min(rect.left(), content.left()));
        rect.setRight(std::max(rect.right(), content.right()));
      }
      if (titledGroups.contains(node.id)) rect.setBottom(rect.bottom() + options.fontSize + 1.0);
      include(rect);
    }
  }

  const auto endpoint = [&](const QString& id, QChar direction) {
    const int index = model.nodeById.value(id, -1);
    if (index < 0) return QPointF{};
    const QPointF origin = model.nodes[index].center();
    const qreal half = options.iconSize / 2.0;
    if (direction == QLatin1Char('L')) return origin + QPointF(0.0, half);
    if (direction == QLatin1Char('R')) return origin + QPointF(options.iconSize, half);
    if (direction == QLatin1Char('T')) return origin + QPointF(half, 0.0);
    return origin + QPointF(half, options.iconSize);
  };
  for (const ArchitectureEdge& edge : data.edges) {
    if (!model.nodeById.contains(edge.lhsId) || !model.nodeById.contains(edge.rhsId)) continue;
    const QPointF start = endpoint(edge.lhsId, edge.lhsDir);
    const QPointF end = endpoint(edge.rhsId, edge.rhsDir);
    const bool lhsHorizontal = edge.lhsDir == QLatin1Char('L') ||
                               edge.lhsDir == QLatin1Char('R');
    const bool rhsHorizontal = edge.rhsDir == QLatin1Char('L') ||
                               edge.rhsDir == QLatin1Char('R');
    QPointF labelCenter = (start + end) / 2.0;
    QRectF rect(start, QSizeF());
    rect = rect.united(QRectF(end, QSizeF()));
    if (lhsHorizontal != rhsHorizontal) {
      QPointF bend;
      if (initial) {
        const QPointF delta = end - start;
        const qreal length = std::hypot(delta.x(), delta.y());
        bend = length > 0.0
                   ? start + QPointF(-delta.y(), delta.x()) * (0.5 / length)
                   : start;
      } else {
        bend = lhsHorizontal ? QPointF(end.x(), start.y())
                             : QPointF(start.x(), end.y());
      }
      rect = rect.united(QRectF(bend, QSizeF()));
      labelCenter = bend;
    }
    rect = rect.adjusted(-2.5, -2.5, 2.5, 2.5);
    if (!edge.title.isEmpty()) {
      const qreal halfWidth = cytoscapeLabelWidth(edge.title) / 2.0 + 3.0;
      const qreal halfHeight = 16.0 / 2.0 + 3.0;
      rect = rect.united(QRectF(labelCenter.x() - halfWidth,
                               labelCenter.y() - halfHeight,
                               2.0 * halfWidth, 2.0 * halfHeight));
    }
    include(rect);
  }
  return first ? QRectF{} : bounds;
}

void runIncremental(GraphModel& model, const ArchitectureFcoseOptions& options,
                    SeededRandom& random) {
  for(int root:model.graphs[0].nodes)childCount(model,root);
  const QVector<int> gravity=gravityNodes(model);
  estimateGraph(model,0,1);calculateLcaAndIdeals(model);
  alignInitial(model);
  AxisModel horizontal,vertical;
  enforceAxis(model,horizontal,model.relative,true);
  enforceAxis(model,vertical,model.relative,false);
  updateBounds(model,0);
  const qreal range=2.0*model.defaultEdgeLength;
  const qreal minDistance=model.defaultEdgeLength/10.0;
  const qreal threshold=3.0*model.defaultEdgeLength/100.0*model.nodes.size();
  const qreal maxCycles=qreal(std::max(options.numIter,1))/kConvergencePeriod;
  qreal cooling=kInitialCooling,total=0,old=0;int cycle=0;
  for(int iteration=1;iteration<options.numIter;++iteration){
    if(iteration%kConvergencePeriod==0){const bool oscillating=iteration>options.numIter/3&&std::abs(total-old)<2.0;const bool converged=total<threshold;old=total;if(converged||oscillating)break;++cycle;
      const qreal exponent=std::log(100.0*(kInitialCooling-kFinalTemperature))/std::log(maxCycles);
      cooling=std::max(kInitialCooling-std::pow(qreal(cycle),exponent)/100.0,kFinalTemperature);}
    updateBounds(model,0);
    if(iteration%10==1)updateSurrounding(model,range);
    for(Node& node:model.nodes){node.spring={};node.repulsion={};node.gravity={};node.displacement={};}
    springForces(model);repulsionForces(model,minDistance);gravityForces(model,gravity);
    for(int i:model.allOrder){Node& node=model.nodes[i];
      node.displacement+=cooling*(node.spring+node.repulsion+node.gravity)/node.noOfChildren;
      const qreal cap=cooling*kMaxDisplacement;
      node.displacement.setX(std::clamp(node.displacement.x(),-cap,cap));
      node.displacement.setY(std::clamp(node.displacement.y(),-cap,cap));
      if(!node.leaf())propagate(model,i,node.displacement);}
    updateAlignmentDisplacements(model);relaxAxis(model,horizontal,true,iteration,random);relaxAxis(model,vertical,false,iteration,random);
    total=0;for(int i:model.allOrder){Node& node=model.nodes[i];if(node.leaf()){node.rect.translate(node.displacement);total+=std::abs(node.displacement.x())+std::abs(node.displacement.y());}node.displacement={};}
  }
  updateBounds(model,0);
}

qreal visualCenterY(const ArchitectureData& data, qreal iconSize) {
  bool anyTitle=false,groupTitle=false;
  for(const auto& service:data.services)anyTitle|=service.hasTitle;
  for(const auto& group:data.groups)groupTitle|=group.hasTitle;
  if(groupTitle)return iconSize==80.0?17.0:7.5;
  return anyTitle?18.0:0.0;
}

void relocate(GraphModel& model, QPointF desiredCenter) {
  const QRectF bounds=allNodeBounds(model);const QPointF shift=desiredCenter-bounds.center();
  for(Node& node:model.nodes)node.rect.translate(shift);
  for(Graph& graph:model.graphs)graph.bounds.translate(shift);
}

ArchitectureFcoseResult fallbackNoEdge(const ArchitectureData& data,
                                        const ArchitectureFcoseOptions& options) {
  ArchitectureFcoseResult result;const QVector<QString> ids=diagramLeafIds(data);
  const qreal y=visualCenterY(data,options.iconSize);
  if(ids.size()==1){result.topLeft.insert(ids[0],{0,y});return result;}
  const qreal step=options.iconSize+66.22203301247856;
  for(int i=0;i<ids.size();++i){const qreal offset=(qreal(ids.size()-1)/2.0-i)*step;result.topLeft.insert(ids[i],{offset,y+offset});}
  return result;
}

}  // namespace

ArchitectureFcoseResult layoutArchitectureFcose(
    const ArchitectureData& data, const ArchitectureFcoseOptions& options,
    const QHash<QString, qreal>& renderedNodeHeights) {
  Q_UNUSED(renderedNodeHeights);
  if(data.services.isEmpty()&&data.junctions.isEmpty())return {};
  if(data.edges.isEmpty()&&data.groups.isEmpty())return fallbackNoEdge(data,options);
  GraphModel model=makeModel(data,options);
  updateBounds(model, 0);
  const QRectF initialCollection =
      cytoscapeCollectionBounds(model, data, options, true);
  QPointF componentCenter = initialCollection.center();
  for(int run=0;run<2;++run){
    // Mermaid wraps each Cytoscape layout.run() in a separate seeded scope.
    SeededRandom random(options.seed);
    if (options.randomize) {
      QHash<int, int> transformedIndex;
      for (int i = 0; i < model.leafOrder.size(); ++i)
        transformedIndex.insert(model.leafOrder.at(i), i);
      QVector<QVector<int>> adjacency(model.leafOrder.size());
      for (const Edge& edge : std::as_const(model.edges)) {
        if (!transformedIndex.contains(edge.source) ||
            !transformedIndex.contains(edge.target))
          continue;
        const int source = transformedIndex.value(edge.source);
        const int target = transformedIndex.value(edge.target);
        adjacency[source].append(target);
        adjacency[target].append(source);
      }
      QVector<QPointF> spectral = layoutArchitectureSpectral(
          adjacency, options.nodeSeparation, [&random] { return random.next(); });
      auto mapAlignments = [&](const QVector<QVector<int>>& groups) {
        QVector<QVector<int>> mapped;
        for (const QVector<int>& group : groups) {
          QVector<int> item;
          for (int node : group)
            if (transformedIndex.contains(node))
              item.append(transformedIndex.value(node));
          if (item.size() > 1) mapped.append(std::move(item));
        }
        return mapped;
      };
      QVector<ArchitectureSpectralRelativeConstraint> relative;
      for (const RelativeConstraint& constraint : std::as_const(model.relative)) {
        if (!transformedIndex.contains(constraint.before) ||
            !transformedIndex.contains(constraint.after))
          continue;
        relative.append({constraint.axis == RelativeConstraint::Axis::Horizontal,
                         transformedIndex.value(constraint.before),
                         transformedIndex.value(constraint.after)});
      }
      transformArchitectureSpectralConstraints(
          spectral, mapAlignments(model.alignments.vertical),
          mapAlignments(model.alignments.horizontal), relative);
      for (int i = 0; i < model.leafOrder.size(); ++i)
        model.nodes[model.leafOrder.at(i)].setCenter(spectral.at(i));
      updateBounds(model, 0);
    }
    runIncremental(model,options,random);
    relocate(model, componentCenter);
    updateBounds(model, 0);
    const QRectF postCollection =
        cytoscapeCollectionBounds(model, data, options, false);
    componentCenter = postCollection.center();
  }
  ArchitectureFcoseResult result;
  for(int index:model.leafOrder)result.topLeft.insert(model.nodes[index].id,model.nodes[index].center());
  updateBounds(model,0);
  for(const auto& group:data.groups){const int index=model.nodeById.value(group.id,-1);if(index>=0)result.groups.insert(group.id,model.nodes[index].rect);}
  return result;
}

}  // namespace muffin::mermaid::architecture
