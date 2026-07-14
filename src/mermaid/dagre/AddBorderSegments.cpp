#include "mermaid/dagre/AddBorderSegments.h"

#include "mermaid/dagre/DagreUtil.h"

#include <QList>
#include <QString>
#include <QVector>

#include <functional>

namespace muffin::mermaid::dagre {

namespace {

QList<QString> kids(const DagreGraph& g, const QString& v) {
  return g.children(v).value_or(QList<QString>{});
}

// Local helper mirroring add-border-segments.js `addBorderNode`
// (g, prop, prefix, sg, sgNode, rank). Distinct from util.addBorderNode.
void addBorderNode(DagreGraph& g, const QString& prop, const QString& prefix,
                   const QString& sgId, int rank) {
  DagreNodeLabel* sgNode = g.node(sgId);
  QVector<QString>* arr = prop == QLatin1String("borderLeft") ? &sgNode->borderLeft
                                                               : &sgNode->borderRight;
  const QString prev = (rank - 1 >= 0 && rank - 1 < arr->size()) ? arr->at(rank - 1) : QString();

  DagreNodeLabel label;
  label.width = 0.0;
  label.height = 0.0;
  label.rank = rank;
  label.borderType = prop;
  const QString curr = addDummyNode(g, QStringLiteral("border"), label, prefix);

  // Re-fetch defensively (insertions above do not relocate QHash values, but
  // this keeps the alias unambiguous).
  DagreNodeLabel* sgNode2 = g.node(sgId);
  QVector<QString>* arr2 = prop == QLatin1String("borderLeft") ? &sgNode2->borderLeft
                                                               : &sgNode2->borderRight;
  if (arr2->size() <= rank) arr2->resize(rank + 1);
  (*arr2)[rank] = curr;

  g.setParent(curr, sgId);
  if (!prev.isEmpty()) {
    DagreEdgeLabel e;
    e.weight = 1;
    g.setEdge(prev, curr, e);
  }
}

}  // namespace

void addBorderSegments(DagreGraph& g) {
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    const QList<QString> children = kids(g, v);
    if (!children.isEmpty())
      for (const QString& child : children) dfs(child);
    // Re-fetch the node label ONLY after the recursion: a child's addBorderNode
    // calls g.setNode/addDummyNode, which can rehash the graph's internal QHash
    // and invalidate a pointer captured before the recursion (the JS original is
    // immune because objects are GC'd, not relocated). Holding that pointer across
    // the recursion was a use-after-free → non-deterministic crash on compound
    // graphs with crossing edges.
    DagreNodeLabel* node = g.node(v);
    if (node && node->minRank.has_value()) {  // hasOwnProperty 'minRank'
      node->borderLeft.clear();
      node->borderRight.clear();
      const int maxRank = *node->maxRank + 1;
      for (int rank = *node->minRank; rank < maxRank; ++rank) {
        addBorderNode(g, QStringLiteral("borderLeft"), QStringLiteral("_bl"), v, rank);
        addBorderNode(g, QStringLiteral("borderRight"), QStringLiteral("_br"), v, rank);
      }
    }
  };
  for (const QString& v : g.children().value_or(QList<QString>{})) dfs(v);
}

}  // namespace muffin::mermaid::dagre
