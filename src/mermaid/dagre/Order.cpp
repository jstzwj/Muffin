#include "mermaid/dagre/Order.h"

#include "mermaid/dagre/DagreUtil.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>

namespace muffin::mermaid::dagre {

namespace {

// A layer graph: compound overlay for one rank, with aggregated edge weights.
// Node labels carry `order` (copied from g, written back after the sweep) and,
// for subgraph nodes, the rank-specific borderLeft/borderRight. The virtual
// root collects top-level movable nodes.
struct LayerGraph {
  DagreGraph g;
  QString root;
};

// ---- barycenter ----
struct Bary {
  QString v;
  std::optional<qreal> barycenter;
  int weight = 0;
};

QList<Bary> barycenter(const DagreGraph& g, const QList<QString>& movable) {
  QList<Bary> result;
  for (const QString& v : movable) {
    const QList<graphlib::Edge> inV = g.inEdges(v);
    if (inV.isEmpty()) {
      result.append(Bary{v, std::nullopt, 0});
      continue;
    }
    qreal sum = 0.0;
    int weight = 0;
    for (const graphlib::Edge& e : inV) {
      const DagreEdgeLabel* edge = g.edge(e);
      const DagreNodeLabel* nodeU = g.node(e.v);
      const int ew = edge ? edge->weight : 0;
      sum += ew * nodeU->order.value_or(0);
      weight += ew;
    }
    Bary b;
    b.v = v;
    b.barycenter = weight ? sum / weight : 0.0;  // mirrors JS (always set when inV non-empty)
    b.weight = weight;
    result.append(b);
  }
  return result;
}

// ---- resolveConflicts ----
struct MappedEntry {
  int indegree = 0;
  QStringList inIds;
  QStringList outIds;
  QStringList vs;
  int i = 0;
  std::optional<qreal> barycenter;
  int weight = 0;
  bool merged = false;
};

struct SortEntry {
  QStringList vs;
  int i = 0;
  std::optional<qreal> barycenter;
  int weight = 0;
};

void mergeEntries(QHash<QString, MappedEntry>& m, const QString& targetId, const QString& sourceId) {
  MappedEntry& target = m[targetId];
  MappedEntry& source = m[sourceId];
  qreal sum = 0.0;
  int weight = 0;
  if (target.weight) {
    sum += target.barycenter.value_or(0.0) * target.weight;
    weight += target.weight;
  }
  if (source.weight) {
    sum += source.barycenter.value_or(0.0) * source.weight;
    weight += source.weight;
  }
  QStringList combined = source.vs;
  combined.append(target.vs);
  target.vs = combined;
  target.barycenter = sum / weight;  // JS: unconditional; degenerate 0/0 → NaN
  target.weight = weight;
  target.i = std::min(source.i, target.i);
  source.merged = true;
}

QList<SortEntry> doResolveConflicts(QHash<QString, MappedEntry>& mapped, QStringList& sourceSet) {
  QStringList ordered;
  auto handleIn = [&](const QString& vId, const QString& uId) {
    MappedEntry& u = mapped[uId];
    if (u.merged) return;
    MappedEntry& v = mapped[vId];
    if (!u.barycenter.has_value() || !v.barycenter.has_value() ||
        *u.barycenter >= *v.barycenter) {
      mergeEntries(mapped, vId, uId);
    }
  };
  auto handleOut = [&](const QString& vId, const QString& wId) {
    MappedEntry& w = mapped[wId];
    w.inIds.append(vId);
    if (--w.indegree == 0) sourceSet.append(wId);
  };
  while (!sourceSet.isEmpty()) {
    const QString entryId = sourceSet.takeLast();
    ordered.append(entryId);
    MappedEntry& entry = mapped[entryId];
    QStringList inRev = entry.inIds;
    std::reverse(inRev.begin(), inRev.end());
    for (const QString& u : inRev) handleIn(entryId, u);
    for (const QString& w : entry.outIds) handleOut(entryId, w);
  }
  QList<SortEntry> result;
  for (const QString& id : ordered)
    if (!mapped[id].merged)
      result.append(SortEntry{mapped[id].vs, mapped[id].i, mapped[id].barycenter, mapped[id].weight});
  return result;
}

QList<SortEntry> resolveConflicts(const QList<Bary>& entries, const DagreGraph& cg) {
  QHash<QString, MappedEntry> mapped;
  for (qsizetype i = 0; i < entries.size(); ++i) {
    MappedEntry me;
    me.vs = QStringList{entries[i].v};
    me.i = static_cast<int>(i);
    me.barycenter = entries[i].barycenter;
    me.weight = entries[i].weight;
    mapped.insert(entries[i].v, me);
  }
  for (const graphlib::Edge& e : cg.edges()) {
    if (mapped.contains(e.v) && mapped.contains(e.w)) {
      mapped[e.w].indegree++;
      mapped[e.v].outIds.append(e.w);
    }
  }
  QStringList sourceSet;
  for (auto it = mapped.cbegin(); it != mapped.cend(); ++it)
    if (it.value().indegree == 0) sourceSet.append(it.key());
  // Preserve stable order: iterate mapped in insertion order for the source set.
  QStringList stableSource;
  for (const Bary& b : entries)
    if (mapped.contains(b.v) && mapped[b.v].indegree == 0) stableSource.append(b.v);
  return doResolveConflicts(mapped, stableSource);
}

// ---- sort ----
int consumeUnsortable(QStringList& vs, QList<SortEntry>& unsortable, int index) {
  while (!unsortable.isEmpty() && unsortable.last().i <= index) {
    vs.append(unsortable.takeLast().vs);
    ++index;
  }
  return index;
}

SortEntry sortEntries(QList<SortEntry> entries, bool biasRight) {
  // partition by has barycenter
  QList<SortEntry> sortable;
  QList<SortEntry> rhs;
  for (const SortEntry& e : entries) (e.barycenter.has_value() ? sortable : rhs).append(e);
  std::sort(sortable.begin(), sortable.end(), [biasRight](const SortEntry& a, const SortEntry& b) {
    const qreal ba = *a.barycenter;
    const qreal bb = *b.barycenter;
    if (ba < bb) return true;
    if (ba > bb) return false;
    return biasRight ? a.i > b.i : a.i < b.i;
  });
  std::sort(rhs.begin(), rhs.end(), [](const SortEntry& a, const SortEntry& b) { return a.i > b.i; });

  QStringList vs;
  qreal sum = 0.0;
  int weight = 0;
  int vsIndex = 0;
  vsIndex = consumeUnsortable(vs, rhs, vsIndex);
  for (const SortEntry& entry : sortable) {
    vsIndex += entry.vs.size();
    vs.append(entry.vs);
    if (entry.barycenter.has_value()) {
      sum += *entry.barycenter * entry.weight;
      weight += entry.weight;
    }
    vsIndex = consumeUnsortable(vs, rhs, vsIndex);
  }
  SortEntry result;
  result.vs = vs;
  if (weight) {
    result.barycenter = sum / weight;
    result.weight = weight;
  }
  return result;
}

// ---- sortSubgraph (recursive) ----
SortEntry sortSubgraph(const DagreGraph& g, const QString& v, const DagreGraph& cg, bool biasRight) {
  QList<QString> movable = g.children(v).value_or(QList<QString>{});
  const DagreNodeLabel* node = g.node(v);
  const QString bl = node ? node->layerBorderLeft : QString();
  const QString br = node ? node->layerBorderRight : QString();
  if (!bl.isEmpty()) {
    QList<QString> filtered;
    for (const QString& w : movable)
      if (w != bl && w != br) filtered.append(w);
    movable = filtered;
  }

  QList<Bary> barycenters = barycenter(g, movable);
  QHash<QString, SortEntry> subgraphs;
  for (const Bary& entry : barycenters) {
    if (g.children(entry.v).has_value() && !g.children(entry.v)->isEmpty()) {
      SortEntry sub = sortSubgraph(g, entry.v, cg, biasRight);
      subgraphs.insert(entry.v, sub);
      // mergeBarycenters
      Bary& target = const_cast<Bary&>(entry);
      if (target.barycenter.has_value()) {
        if (sub.barycenter.has_value()) {
          const qreal merged = (target.barycenter.value() * target.weight +
                                sub.barycenter.value() * sub.weight) /
                               (target.weight + sub.weight);
          target.barycenter = merged;
          target.weight += sub.weight;
        }
      } else {
        target.barycenter = sub.barycenter;
        target.weight = sub.weight;
      }
    }
  }

  QList<SortEntry> entries = resolveConflicts(barycenters, cg);

  // expandSubgraphs
  for (SortEntry& entry : entries) {
    QStringList expanded;
    for (const QString& node : entry.vs) {
      auto it = subgraphs.constFind(node);
      if (it != subgraphs.cend()) expanded.append(it.value().vs);
      else expanded.append(node);
    }
    entry.vs = expanded;
  }

  SortEntry result = sortEntries(entries, biasRight);

  if (!bl.isEmpty()) {
    QStringList withBorders;
    withBorders.append(bl);
    withBorders.append(result.vs);
    withBorders.append(br);
    result.vs = withBorders;
    QList<QString> blPreds = g.predecessors(bl);
    if (!blPreds.isEmpty()) {
      const DagreNodeLabel* blPred = g.node(blPreds.first());
      QList<QString> brPreds = g.predecessors(br);
      const DagreNodeLabel* brPred = brPreds.isEmpty() ? nullptr : g.node(brPreds.first());
      if (!result.barycenter.has_value()) {
        result.barycenter = 0.0;
        result.weight = 0;
      }
      const qreal bc = (result.barycenter.value() * result.weight +
                        blPred->order.value_or(0) + (brPred ? brPred->order.value_or(0) : 0)) /
                       (result.weight + 2);
      result.barycenter = bc;
      result.weight += 2;
    }
  }
  return result;
}

// ---- crossCount (accumulator tree) ----
qint64 twoLayerCrossCount(const DagreGraph& g, const QList<QString>& northLayer,
                          const QList<QString>& southLayer) {
  QHash<QString, int> southPos;
  for (qsizetype i = 0; i < southLayer.size(); ++i) southPos.insert(southLayer[i], static_cast<int>(i));
  QList<QPair<int, int>> southEntries;  // (pos, weight)
  for (const QString& v : northLayer) {
    QList<QPair<int, int>> entries;
    for (const graphlib::Edge& e : g.outEdges(v)) {
      const int w = g.edge(e) ? g.edge(e)->weight : 0;
      entries.append(qMakePair(southPos.value(e.w, 0), w));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& p : entries) southEntries.append(p);
  }
  int firstIndex = 1;
  while (firstIndex < southLayer.size()) firstIndex <<= 1;
  const int treeSize = 2 * firstIndex - 1;
  firstIndex -= 1;
  QVector<int> tree(treeSize, 0);
  qint64 cc = 0;
  for (const auto& entry : southEntries) {
    int index = entry.first + firstIndex;
    tree[index] += entry.second;
    int weightSum = 0;
    while (index > 0) {
      if (index % 2) weightSum += tree[index + 1];
      index = (index - 1) >> 1;
      tree[index] += entry.second;
    }
    cc += static_cast<qint64>(entry.second) * weightSum;
  }
  return cc;
}

qint64 crossCount(const DagreGraph& g, const QVector<QList<QString>>& layering) {
  qint64 cc = 0;
  for (qsizetype i = 1; i < layering.size(); ++i)
    cc += twoLayerCrossCount(g, layering[i - 1], layering[i]);
  return cc;
}

// ---- initOrder ----
QVector<QList<QString>> initOrder(const DagreGraph& g) {
  QSet<QString> visited;
  QList<QString> simpleNodes;
  for (const QString& v : g.nodes())
    if (g.children(v).value_or(QList<QString>{}).isEmpty()) simpleNodes.append(v);
  int maxRank = 0;
  for (const QString& v : simpleNodes) maxRank = std::max(maxRank, g.node(v)->rank.value_or(0));
  QVector<QList<QString>> layers(maxRank + 1);
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    if (visited.contains(v)) return;
    visited.insert(v);
    const DagreNodeLabel* node = g.node(v);
    if (node->rank.has_value() && *node->rank >= 0 && *node->rank < layers.size())
      layers[*node->rank].append(v);
    for (const QString& w : g.successors(v)) dfs(w);
  };
  QList<QString> orderedVs = simpleNodes;
  std::stable_sort(orderedVs.begin(), orderedVs.end(),
                   [&](const QString& a, const QString& b) {
                     return g.node(a)->rank.value_or(0) < g.node(b)->rank.value_or(0);
                   });
  for (const QString& v : orderedVs) dfs(v);
  return layers;
}

void assignOrder(DagreGraph& g, const QVector<QList<QString>>& layering) {
  for (const QList<QString>& layer : layering)
    for (qsizetype i = 0; i < layer.size(); ++i)
      g.node(layer[i])->order = static_cast<int>(i);
}

// ---- buildLayerGraph ----
LayerGraph buildLayerGraph(DagreGraph& g, int rank, const QString& relationship) {
  LayerGraph lg;
  lg.g = DagreGraph({.directed = true, .multigraph = false, .compound = true});
  // createRootNode
  do {
    lg.root = QStringLiteral("_root") + QString::number(++g.graph()->nextDummyId);
  } while (g.hasNode(lg.root));
  lg.g.setGraph(DagreGraphLabel{});
  // Mirror dagre's setDefaultNodeLabel(() => g.node(v)): incident non-movable
  // nodes (e.g. rank-0 predecessors) created implicitly via setEdge must carry
  // their real `order` from g so barycenter() reads live positions.
  lg.g.setDefaultNodeLabel([&g](const QString& id) -> DagreNodeLabel {
    return g.node(id) ? *g.node(id) : DagreNodeLabel{};
  });
  lg.g.setNode(lg.root, DagreNodeLabel{});

  for (const QString& v : g.nodes()) {
    const DagreNodeLabel* node = g.node(v);
    const bool inRank = (node->rank.has_value() && *node->rank == rank) ||
                        (node->minRank.has_value() && node->maxRank.has_value() &&
                         *node->minRank <= rank && rank <= *node->maxRank);
    if (!inRank) continue;
    const QString parent = g.parentOf(v);
    // setNode with a copy of g's label (default-node-label aliasing in JS)
    lg.g.setNode(v, *node);
    lg.g.setParent(v, parent.isEmpty() ? lg.root : parent);

    const QList<graphlib::Edge> rel = relationship == QLatin1String("inEdges") ? g.inEdges(v) : g.outEdges(v);
    for (const graphlib::Edge& e : rel) {
      const QString u = e.v == v ? e.w : e.v;
      const int prev = lg.g.edge(u, v) ? lg.g.edge(u, v)->weight : 0;
      const int w = g.edge(e) ? g.edge(e)->weight : 0;
      DagreEdgeLabel agg;
      agg.weight = w + prev;
      lg.g.setEdge(u, v, agg);
    }

    if (node->minRank.has_value()) {
      DagreNodeLabel borderLabel;
      const int idx = rank;
      borderLabel.layerBorderLeft = (idx >= 0 && idx < node->borderLeft.size()) ? node->borderLeft[idx] : QString();
      borderLabel.layerBorderRight = (idx >= 0 && idx < node->borderRight.size()) ? node->borderRight[idx] : QString();
      lg.g.setNode(v, borderLabel);
    }
  }
  return lg;
}

// ---- addSubgraphConstraints ----
void addSubgraphConstraints(const DagreGraph& g, DagreGraph& cg, const QStringList& vs) {
  QHash<QString, QString> prev;
  QString rootPrev;
  for (const QString& v : vs) {
    QString child = g.parentOf(v);
    while (!child.isNull()) {
      const QString parent = g.parentOf(child);
      QString prevChild;
      if (!parent.isNull()) {
        prevChild = prev.value(parent);
        prev.insert(parent, child);
      } else {
        prevChild = rootPrev;
        rootPrev = child;
      }
      if (!prevChild.isNull() && prevChild != child) {
        cg.setEdge(prevChild, child, DagreEdgeLabel{});
        break;
      }
      child = parent;
    }
  }
}

}  // namespace

void order(DagreGraph& g) {
  const std::optional<int> mr = maxRank(g);
  if (!mr.has_value()) return;
  const int maxRankVal = *mr;

  // initOrder first: dagre aliases layer-graph node labels to g.node(v), so
  // barycenter reads live orders. Value semantics can't alias, so we (1) build
  // layer graphs AFTER initOrder so copies carry initial orders, and (2) sync
  // g's current orders into each layer graph before every sortSubgraph (so a
  // down-sweep sees the orders the previous rank just wrote).
  QVector<QList<QString>> layering = initOrder(g);
  assignOrder(g, layering);

  QList<LayerGraph> downLayerGraphs;
  for (int rank = 1; rank <= maxRankVal; ++rank)
    downLayerGraphs.append(buildLayerGraph(g, rank, QStringLiteral("inEdges")));
  QList<LayerGraph> upLayerGraphs;
  for (int rank = maxRankVal - 1; rank >= 0; --rank)
    upLayerGraphs.append(buildLayerGraph(g, rank, QStringLiteral("outEdges")));

  auto syncOrders = [&g](LayerGraph& lg) {
    for (const QString& v : lg.g.nodes()) {
      if (!g.hasNode(v)) continue;
      if (g.node(v)->order.has_value()) lg.g.node(v)->order = g.node(v)->order;
      else lg.g.node(v)->order.reset();
    }
  };

  qint64 bestCC = std::numeric_limits<qint64>::max();
  QVector<QList<QString>> best;
  for (int i = 0, lastBest = 0; lastBest < 4; ++i, ++lastBest) {
    QList<LayerGraph>& lgs = (i % 2) ? downLayerGraphs : upLayerGraphs;
    const bool biasRight = i % 4 >= 2;
    DagreGraph cg({.directed = true, .multigraph = false, .compound = false});
    for (LayerGraph& lg : lgs) {
      syncOrders(lg);
      const SortEntry sorted = sortSubgraph(lg.g, lg.root, cg, biasRight);
      for (qsizetype idx = 0; idx < sorted.vs.size(); ++idx) {
        if (DagreNodeLabel* ln = lg.g.node(sorted.vs[idx])) ln->order = static_cast<int>(idx);
      }
      addSubgraphConstraints(lg.g, cg, sorted.vs);
      // Write orders back to the real graph (value semantics: lg holds copies).
      for (const QString& v : lg.g.nodes())
        if (g.hasNode(v) && lg.g.node(v)->order.has_value()) g.node(v)->order = lg.g.node(v)->order;
    }
    layering = buildLayerMatrix(g);
    const qint64 cc = crossCount(g, layering);
    if (cc < bestCC) {
      lastBest = 0;
      best = layering;
      bestCC = cc;
    }
  }
  assignOrder(g, best);
}

}  // namespace muffin::mermaid::dagre
