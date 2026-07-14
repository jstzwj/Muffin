#include "mermaid/dagre/NestingGraph.h"

#include "mermaid/dagre/DagreUtil.h"

#include <QList>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <limits>

namespace muffin::mermaid::dagre {

namespace {

QList<QString> kids(const DagreGraph& g, const QString& v) {
  return g.children(v).value_or(QList<QString>{});
}

int sumWeights(const DagreGraph& g) {
  int total = 0;
  for (const graphlib::Edge& e : g.edges())
    if (const DagreEdgeLabel* label = g.edge(e)) total += label->weight;
  return total;
}

void nestingDfs(DagreGraph& g, const QString& root, int nodeSep, int weight, int height,
                const QHash<QString, int>& depths, const QString& v) {
  const QList<QString> children = kids(g, v);
  if (children.isEmpty()) {
    if (v != root) {
      DagreEdgeLabel e;
      e.weight = 0;
      e.minlen = nodeSep;
      g.setEdge(root, v, e);
    }
    return;
  }

  const QString top = addBorderNode(g, QStringLiteral("_bt"));
  const QString bottom = addBorderNode(g, QStringLiteral("_bb"));
  DagreNodeLabel* label = g.node(v);

  g.setParent(top, v);
  label->borderTop = top;
  g.setParent(bottom, v);
  label->borderBottom = bottom;

  for (const QString& child : children) {
    nestingDfs(g, root, nodeSep, weight, height, depths, child);

    DagreNodeLabel* childNode = g.node(child);
    const QString childTop = !childNode->borderTop.isEmpty() ? childNode->borderTop : child;
    const QString childBottom = !childNode->borderBottom.isEmpty() ? childNode->borderBottom : child;
    const int thisWeight = !childNode->borderTop.isEmpty() ? weight : 2 * weight;
    const int minlen = childTop != childBottom ? 1 : height - depths.value(v) + 1;

    DagreEdgeLabel down;
    down.weight = thisWeight;
    down.minlen = minlen;
    down.nestingEdge = true;
    g.setEdge(top, childTop, down);

    DagreEdgeLabel up;
    up.weight = thisWeight;
    up.minlen = minlen;
    up.nestingEdge = true;
    g.setEdge(childBottom, bottom, up);
  }

  if (g.parentOf(v).isNull()) {
    DagreEdgeLabel e;
    e.weight = 0;
    e.minlen = height + depths.value(v);
    g.setEdge(root, top, e);
  }
}

}  // namespace

QHash<QString, int> treeDepths(const DagreGraph& g) {
  QHash<QString, int> depths;
  std::function<void(const QString&, int)> dfs = [&](const QString& v, int depth) {
    for (const QString& child : kids(g, v)) dfs(child, depth + 1);
    depths[v] = depth;
  };
  for (const QString& v : g.children().value_or(QList<QString>{})) dfs(v, 1);
  return depths;
}

void runNestingGraph(DagreGraph& g) {
  const QString root = addDummyNode(g, QStringLiteral("root"), DagreNodeLabel{},
                                    QStringLiteral("_root"));
  const QHash<QString, int> depths = treeDepths(g);
  int maxDepth = std::numeric_limits<int>::min();
  bool any = false;
  for (auto it = depths.cbegin(); it != depths.cend(); ++it) {
    maxDepth = std::max(maxDepth, it.value());
    any = true;
  }
  // upstream: _.max(_.values(depths)) - 1; undefined - 1 when empty → treat as -1
  const int height = any ? maxDepth - 1 : -1;
  const int nodeSep = 2 * height + 1;

  g.graph()->nestingRoot = root;

  // Multiply minlen by nodeSep to align nodes on non-border ranks.
  for (const graphlib::Edge& e : g.edges()) {
    if (DagreEdgeLabel* label = g.edge(e)) label->minlen *= nodeSep;
  }

  const int weight = sumWeights(g) + 1;

  for (const QString& child : g.children().value_or(QList<QString>{}))
    nestingDfs(g, root, nodeSep, weight, height, depths, child);

  g.graph()->nodeRankFactor = nodeSep;
}

void cleanupNestingGraph(DagreGraph& g) {
  DagreGraphLabel* gl = g.graph();
  const QString root = gl->nestingRoot;
  if (!root.isEmpty()) g.removeNode(root);
  gl->nestingRoot.clear();
  // Snapshot edges before mutating.
  const QList<graphlib::Edge> all = g.edges();
  for (const graphlib::Edge& e : all) {
    if (const DagreEdgeLabel* label = g.edge(e); label && label->nestingEdge) g.removeEdge(e);
  }
}

}  // namespace muffin::mermaid::dagre
