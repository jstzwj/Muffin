#include "mermaid/dagre/DagreUtil.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace muffin::mermaid::dagre {

namespace {

// Return every node id that has a rank, in stable insertion order.
QVector<QString> rankedNodes(const DagreGraph& g) {
  QVector<QString> result;
  for (const QString& v : g.nodes())
    if (g.node(v)->rank.has_value()) result.append(v);
  return result;
}

}  // namespace

QString addDummyNode(DagreGraph& g, const QString& type, DagreNodeLabel attrs,
                     const QString& name) {
  DagreGraphLabel* label = g.graph();
  QString v;
  do {
    v = name + QString::number(++label->nextDummyId);
  } while (g.hasNode(v));
  attrs.dummy = type;
  g.setNode(v, attrs);
  return v;
}

QString addBorderNode(DagreGraph& g, const QString& prefix) {
  DagreNodeLabel node;
  node.width = 0.0;
  node.height = 0.0;
  return addDummyNode(g, QStringLiteral("border"), node, prefix);
}

QString addBorderNode(DagreGraph& g, const QString& prefix, int rank, int order) {
  DagreNodeLabel node;
  node.width = 0.0;
  node.height = 0.0;
  node.rank = rank;
  node.order = order;
  return addDummyNode(g, QStringLiteral("border"), node, prefix);
}

std::optional<int> maxRank(const DagreGraph& g) {
  std::optional<int> result;
  for (const QString& v : g.nodes()) {
    const std::optional<int> rank = g.node(v)->rank;
    if (rank.has_value()) {
      if (!result.has_value() || *rank > *result) result = rank;
    }
  }
  return result;
}

QVector<QVector<QString>> buildLayerMatrix(const DagreGraph& g) {
  const std::optional<int> top = maxRank(g);
  if (!top.has_value()) return {};
  QVector<QVector<QString>> layering(*top + 1);
  for (const QString& v : g.nodes()) {
    const DagreNodeLabel* node = g.node(v);
    if (!node->rank.has_value() || !node->order.has_value()) continue;
    const int rank = *node->rank;
    const int order = *node->order;
    auto& row = layering[rank];
    if (row.size() <= order) row.resize(order + 1);
    row[order] = v;
  }
  return layering;
}

QPointF intersectRect(const DagreNodeLabel& rect, const QPointF& point) {
  const qreal x = rect.x.value_or(0.0);
  const qreal y = rect.y.value_or(0.0);
  const qreal dx = point.x() - x;
  const qreal dy = point.y() - y;
  qreal w = rect.width / 2.0;
  qreal h = rect.height / 2.0;
  if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy))
    throw std::runtime_error("Not possible to find intersection inside of the rectangle");
  qreal sx;
  qreal sy;
  if (std::abs(dy) * w > std::abs(dx) * h) {
    if (dy < 0.0) h = -h;
    sx = qFuzzyIsNull(dy) ? 0.0 : (h * dx) / dy;
    sy = h;
  } else {
    if (dx < 0.0) w = -w;
    sx = w;
    sy = qFuzzyIsNull(dx) ? 0.0 : (w * dy) / dx;
  }
  return QPointF(x + sx, y + sy);
}

DagreGraph asNonCompoundGraph(const DagreGraph& g) {
  DagreGraph simplified(/*opts*/ {.directed = true, .multigraph = g.isMultigraph(), .compound = false});
  if (const DagreGraphLabel* gl = g.graph()) simplified.setGraph(*gl);
  for (const QString& v : g.nodes()) {
    if (g.children(v).value_or(QList<QString>{}).isEmpty()) {
      if (const DagreNodeLabel* node = g.node(v)) simplified.setNode(v, *node);
    }
  }
  for (const graphlib::Edge& e : g.edges()) {
    if (const DagreEdgeLabel* label = g.edge(e)) simplified.setEdge(e, *label);
  }
  return simplified;
}

void normalizeRanks(DagreGraph& g) {
  std::optional<int> min;
  for (const QString& v : g.nodes()) {
    const std::optional<int> rank = g.node(v)->rank;
    if (rank.has_value()) {
      if (!min.has_value() || *rank < *min) min = rank;
    }
  }
  if (!min.has_value()) return;
  for (const QString& v : g.nodes()) {
    DagreNodeLabel* node = g.node(v);
    if (node->rank.has_value()) node->rank = *node->rank - *min;
  }
}

void removeEmptyRanks(DagreGraph& g) {
  const QVector<QString> ranked = rankedNodes(g);
  if (ranked.isEmpty()) return;
  int offset = std::numeric_limits<int>::max();
  for (const QString& v : ranked) offset = std::min(offset, *g.node(v)->rank);
  // Build layers (sparse), mirroring the JS array-with-holes behaviour.
  QHash<int, QVector<QString>> layersByOffset;
  int maxOffset = 0;
  for (const QString& v : ranked) {
    const int o = *g.node(v)->rank - offset;
    layersByOffset[o].append(v);
    maxOffset = std::max(maxOffset, o);
  }
  const int nodeRankFactor = g.graph()->nodeRankFactor > 0 ? g.graph()->nodeRankFactor : 1;
  int delta = 0;
  for (int i = 0; i <= maxOffset; ++i) {
    const auto it = layersByOffset.constFind(i);
    const bool empty = it == layersByOffset.cend();
    // dagre: `if (undefined && i%nodeRankFactor) --delta; else if (delta) forEach(vs)`.
    // The else covers (empty && i%==0) too, where vs is undefined and forEach is a
    // no-op — so we must NOT touch the cend iterator there.
    if (empty) {
      if (i % nodeRankFactor != 0) --delta;
    } else if (delta != 0) {
      for (const QString& v : it.value()) g.node(v)->rank = *g.node(v)->rank + delta;
    }
  }
}

}  // namespace muffin::mermaid::dagre
