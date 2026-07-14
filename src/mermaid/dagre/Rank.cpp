#include "mermaid/dagre/Rank.h"

#include "mermaid/dagre/DagreUtil.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <algorithm>
#include <functional>
#include <limits>

namespace muffin::mermaid::dagre {

namespace {

// Undirected tree used by feasible-tree / network-simplex.
struct TreeNodeLabel {
  int low = 0;
  int lim = 0;
  QString parent;
};
struct TreeEdgeLabel {
  int cutvalue = 0;
};
using Tree = graphlib::Graph<TreeNodeLabel, TreeEdgeLabel>;

QList<QString> postorder(const Tree& t, const QList<QString>& nodes) {
  QList<QString> result;
  QSet<QString> visited;
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    if (visited.contains(v)) return;
    visited.insert(v);
    for (const QString& w : t.neighbors(v)) dfs(w);
    result.append(v);
  };
  for (const QString& v : nodes) dfs(v);
  return result;
}

QList<QString> preorder(const Tree& t, const QString& root) {
  QList<QString> result;
  QSet<QString> visited;
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    if (visited.contains(v)) return;
    visited.insert(v);
    result.append(v);
    for (const QString& w : t.neighbors(v)) dfs(w);
  };
  dfs(root);
  return result;
}

int edgeWeight(const DagreGraph& g, const QString& v, const QString& w) {
  const DagreEdgeLabel* l = g.edge(v, w);
  return l ? l->weight : 0;
}

int tightTree(Tree& t, const DagreGraph& g) {
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    for (const graphlib::Edge& e : g.nodeEdges(v)) {
      const QString other = e.v == v ? e.w : e.v;
      if (!t.hasNode(other) && slack(g, e) == 0) {
        t.setNode(other, TreeNodeLabel{});
        t.setEdge(v, other, TreeEdgeLabel{});
        dfs(other);
      }
    }
  };
  const QList<QString> snapshot = t.nodes();
  for (const QString& v : snapshot) dfs(v);
  return static_cast<int>(t.nodeCount());
}

std::optional<graphlib::Edge> findMinSlackEdge(const Tree& t, const DagreGraph& g) {
  std::optional<graphlib::Edge> best;
  int bestSlack = std::numeric_limits<int>::max();
  bool found = false;
  for (const graphlib::Edge& e : g.edges()) {
    if (t.hasNode(e.v) != t.hasNode(e.w)) {
      const int s = slack(g, e);
      if (!found || s < bestSlack) {
        bestSlack = s;
        best = e;
        found = true;
      }
    }
  }
  return best;
}

void shiftRanks(const Tree& t, DagreGraph& g, int delta) {
  for (const QString& v : t.nodes())
    if (DagreNodeLabel* n = g.node(v)) n->rank = n->rank.value_or(0) + delta;
}

Tree feasibleTree(DagreGraph& g) {
  Tree t({.directed = false, .compound = false});
  const QList<QString> nodes = g.nodes();
  if (nodes.isEmpty()) return t;
  const QString start = nodes.first();
  t.setNode(start, TreeNodeLabel{});
  const qsizetype size = g.nodeCount();
  while (tightTree(t, g) < size) {
    const auto edge = findMinSlackEdge(t, g);
    if (!edge) break;
    const int delta = t.hasNode(edge->v) ? slack(g, *edge) : -slack(g, *edge);
    shiftRanks(t, g, delta);
  }
  return t;
}

int dfsAssignLowLim(Tree& t, QSet<QString>& visited, int nextLim, const QString& v,
                    const QString& parent) {
  int low = nextLim;
  TreeNodeLabel* label = t.node(v);
  visited.insert(v);
  for (const QString& w : t.neighbors(v)) {
    if (!visited.contains(w)) nextLim = dfsAssignLowLim(t, visited, nextLim, w, v);
  }
  label->low = low;
  label->lim = nextLim++;
  if (!parent.isNull()) label->parent = parent;
  else label->parent.clear();
  return nextLim;
}

void initLowLimValues(Tree& t) {
  const QList<QString> nodes = t.nodes();
  if (nodes.isEmpty()) return;
  QSet<QString> visited;
  dfsAssignLowLim(t, visited, 1, nodes.first(), QString());
}

bool isTreeEdge(const Tree& t, const QString& u, const QString& v) { return t.hasEdge(u, v); }

bool isDescendant(const TreeNodeLabel& vLabel, const TreeNodeLabel& rootLabel) {
  return rootLabel.low <= vLabel.lim && vLabel.lim <= rootLabel.lim;
}

int calcCutValue(Tree& t, const DagreGraph& g, const QString& child) {
  TreeNodeLabel* childLab = t.node(child);
  const QString parent = childLab->parent;
  bool childIsTail = true;
  const DagreEdgeLabel* graphEdge = g.edge(child, parent);
  if (!graphEdge) {
    childIsTail = false;
    graphEdge = g.edge(parent, child);
  }
  if (!graphEdge) return 0;
  int cutValue = graphEdge->weight;
  for (const graphlib::Edge& e : g.nodeEdges(child)) {
    const bool isOutEdge = e.v == child;
    const QString other = isOutEdge ? e.w : e.v;
    if (other == parent) continue;
    const bool pointsToHead = isOutEdge == childIsTail;
    const int otherWeight = g.edge(e) ? g.edge(e)->weight : 0;
    cutValue += pointsToHead ? otherWeight : -otherWeight;
    if (isTreeEdge(t, child, other)) {
      const int otherCutValue = t.edge(child, other)->cutvalue;
      cutValue += pointsToHead ? -otherCutValue : otherCutValue;
    }
  }
  return cutValue;
}

void assignCutValue(Tree& t, const DagreGraph& g, const QString& child) {
  TreeNodeLabel* childLab = t.node(child);
  const QString parent = childLab->parent;
  if (TreeEdgeLabel* te = t.edge(child, parent)) te->cutvalue = calcCutValue(t, g, child);
}

void initCutValues(Tree& t, const DagreGraph& g) {
  QList<QString> vs = postorder(t, t.nodes());
  if (!vs.isEmpty()) vs.removeLast();
  for (const QString& v : vs) assignCutValue(t, g, v);
}

std::optional<graphlib::Edge> leaveEdge(const Tree& t) {
  for (const graphlib::Edge& e : t.edges())
    if (t.edge(e)->cutvalue < 0) return e;
  return std::nullopt;
}

std::optional<graphlib::Edge> enterEdge(const Tree& t, const DagreGraph& g,
                                        const graphlib::Edge& edge) {
  QString v = edge.v;
  QString w = edge.w;
  if (!g.hasEdge(v, w)) {
    v = edge.w;
    w = edge.v;
  }
  const TreeNodeLabel* vLabel = t.node(v);
  const TreeNodeLabel* wLabel = t.node(w);
  const TreeNodeLabel* tailLabel = vLabel;
  bool flip = false;
  if (vLabel->lim > wLabel->lim) {
    tailLabel = wLabel;
    flip = true;
  }
  std::optional<graphlib::Edge> best;
  int bestSlack = std::numeric_limits<int>::max();
  bool found = false;
  for (const graphlib::Edge& e : g.edges()) {
    const TreeNodeLabel* ev = t.node(e.v);
    const TreeNodeLabel* ew = t.node(e.w);
    if ((flip == isDescendant(*ev, *tailLabel)) && (flip != isDescendant(*ew, *tailLabel))) {
      const int s = slack(g, e);
      if (!found || s < bestSlack) {
        bestSlack = s;
        best = e;
        found = true;
      }
    }
  }
  return best;
}

void updateRanks(const Tree& t, DagreGraph& g) {
  // dagre: `_.find(t.nodes(), v => !g.node(v).parent)` — g's node labels have no
  // `parent`, so this is always-true ⇒ the first tree node, which is the root
  // initLowLimValues started from.
  const QList<QString> tnodes = t.nodes();
  if (tnodes.isEmpty()) return;
  const QString root = tnodes.first();
  const QList<QString> vs = preorder(t, root);
  for (qsizetype i = 1; i < vs.size(); ++i) {
    const QString& v = vs[i];
    const QString parent = t.node(v)->parent;
    const DagreEdgeLabel* edge = g.edge(v, parent);
    bool flipped = false;
    if (!edge) {
      edge = g.edge(parent, v);
      flipped = true;
    }
    if (edge) g.node(v)->rank = g.node(parent)->rank.value_or(0) + (flipped ? edge->minlen : -edge->minlen);
  }
}

void exchangeEdges(Tree& t, DagreGraph& g, const graphlib::Edge& e, const graphlib::Edge& f) {
  t.removeEdge(e.v, e.w);
  t.setEdge(f.v, f.w, TreeEdgeLabel{});
  initLowLimValues(t);
  initCutValues(t, g);
  updateRanks(t, g);
}

}  // namespace

void longestPath(DagreGraph& g) {
  QSet<QString> visited;
  std::function<int(const QString&)> dfs = [&](const QString& v) -> int {
    DagreNodeLabel* label = g.node(v);
    if (visited.contains(v)) return label->rank.value_or(0);
    visited.insert(v);
    std::optional<int> rank;
    for (const graphlib::Edge& e : g.outEdges(v)) {
      const int candidate = dfs(e.w) - g.edge(e)->minlen;
      if (!rank.has_value() || candidate < *rank) rank = candidate;
    }
    label->rank = rank.value_or(0);
    return *label->rank;
  };
  for (const QString& v : g.sources()) dfs(v);
}

int slack(const DagreGraph& g, const graphlib::Edge& e) {
  return g.node(e.w)->rank.value_or(0) - g.node(e.v)->rank.value_or(0) - g.edge(e)->minlen;
}

void networkSimplex(DagreGraph& g) {
  longestPath(g);
  Tree t = feasibleTree(g);
  initLowLimValues(t);
  initCutValues(t, g);
  while (true) {
    const auto e = leaveEdge(t);
    if (!e) break;
    const auto f = enterEdge(t, g, *e);
    if (!f) break;
    exchangeEdges(t, g, *e, *f);
  }
}

void rankGraph(DagreGraph& g) {
  // Simplified leaf-only view (non-multigraph): aggregate multi-edge weights
  // and take the max minlen, mirroring util.simplify on asNonCompoundGraph(g).
  DagreGraph s({.directed = true, .multigraph = false, .compound = false});
  if (g.graph()) s.setGraph(*g.graph());
  for (const QString& v : g.nodes())
    if (g.children(v).value_or(QList<QString>{}).isEmpty()) s.setNode(v, *g.node(v));
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel agg;
    if (const DagreEdgeLabel* ex = s.edge(e.v, e.w)) {
      agg = *ex;
    } else {
      agg.weight = 0;
      agg.minlen = 1;
    }
    const DagreEdgeLabel& src = *g.edge(e);
    agg.weight += src.weight;
    agg.minlen = std::max(agg.minlen, src.minlen);
    s.setEdge(e.v, e.w, agg);
  }

  const QString ranker = g.graph()->ranker;  // optional; empty → network-simplex
  if (ranker == QLatin1String("tight-tree")) {
    longestPath(s);
    // feasibleTree-only refinement (no cut-value iteration) — rarely used.
    Tree t = feasibleTree(s);
    Q_UNUSED(t);
  } else if (ranker == QLatin1String("longest-path")) {
    longestPath(s);
  } else {
    networkSimplex(s);
  }

  // Write ranks back onto the compound graph's leaf nodes.
  for (const QString& v : s.nodes())
    if (DagreNodeLabel* n = g.node(v)) n->rank = s.node(v)->rank;
}

}  // namespace muffin::mermaid::dagre
