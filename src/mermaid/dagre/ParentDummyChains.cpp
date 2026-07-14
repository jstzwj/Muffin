#include "mermaid/dagre/ParentDummyChains.h"

#include <QtGlobal>

#include <algorithm>
#include <climits>
#include <functional>

namespace muffin::mermaid::dagre {

namespace {

QList<QString> kids(const DagreGraph& g, const QString& v) {
  return g.children(v).value_or(QList<QString>{});
}

// JS `g.node(pathV).maxRank < node.rank` with undefined→false semantics.
int maxRankOrInf(const DagreGraph& g, const QString& v) {
  const DagreNodeLabel* node = g.node(v);
  if (!node || !node->maxRank.has_value()) return INT_MAX;  // undefined < x === false
  return *node->maxRank;
}

int minRankOrInf(const DagreGraph& g, const QString& v) {
  const DagreNodeLabel* node = g.node(v);
  if (!node || !node->minRank.has_value()) return INT_MAX;  // undefined <= x === false
  return *node->minRank;
}

}  // namespace

QHash<QString, PostorderEntry> postorder(const DagreGraph& g) {
  QHash<QString, PostorderEntry> result;
  int lim = 0;
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    const int low = lim;
    for (const QString& child : kids(g, v)) dfs(child);
    result[v] = PostorderEntry{low, lim};
    lim++;
  };
  for (const QString& v : g.children().value_or(QList<QString>{})) dfs(v);
  return result;
}

ClusterPath findPath(const DagreGraph& g, const QHash<QString, PostorderEntry>& postorderNums,
                     const QString& v, const QString& w) {
  QList<QString> vPath;
  QList<QString> wPath;
  const PostorderEntry vEntry = postorderNums.value(v);
  const PostorderEntry wEntry = postorderNums.value(w);
  const int low = std::min(vEntry.low, wEntry.low);
  const int lim = std::max(vEntry.lim, wEntry.lim);

  // Traverse up from v to find the LCA. JS do-while pushes each parent (which
  // may be undefined → null QString) and stops when parent is falsy or the
  // low/lim bracket is satisfied.
  QString parent = v;
  QString lca;
  forever {
    parent = g.parentOf(parent);
    vPath.append(parent);  // push even when null, matching JS `vPath.push(parent)`
    if (parent.isNull()) break;
    const PostorderEntry pe = postorderNums.value(parent);
    if (!(pe.low > low || lim > pe.lim)) break;  // while condition false → stop
  }
  lca = parent;

  // Traverse from w up to the LCA.
  parent = w;
  while (true) {
    parent = g.parentOf(parent);
    if (parent != lca) {
      if (parent.isNull()) break;  // safety: never found lca; stop at root
      wPath.append(parent);
    } else {
      break;
    }
  }

  std::reverse(wPath.begin(), wPath.end());
  ClusterPath result;
  result.path = vPath + wPath;
  result.lca = lca;
  return result;
}

void parentDummyChains(DagreGraph& g) {
  const QHash<QString, PostorderEntry> postorderNums = postorder(g);
  const QVector<QString> chains = g.graph()->dummyChains;
  for (const QString& v : chains) {
    DagreNodeLabel* node = g.node(v);
    if (!node || !node->edgeObj.has_value()) continue;
    const graphlib::Edge edgeObj = *node->edgeObj;
    const ClusterPath pathData = findPath(g, postorderNums, edgeObj.v, edgeObj.w);
    const QList<QString>& path = pathData.path;
    const QString& lca = pathData.lca;
    int pathIdx = 0;
    QString pathV = path.isEmpty() ? QString() : path.first();
    bool ascending = true;
    QString cur = v;

    while (cur != edgeObj.w) {
      node = g.node(cur);
      if (!node) break;

      if (ascending) {
        // while ((pathV = path[pathIdx]) !== lca && maxRank(pathV) < node.rank)
        while (true) {
          pathV = (pathIdx >= 0 && pathIdx < path.size()) ? path.at(pathIdx) : QString();
          if (pathV == lca) break;  // !== lca is false
          if (!(maxRankOrInf(g, pathV) < node->rank.value_or(INT_MAX))) break;
          ++pathIdx;
        }
        pathV = (pathIdx >= 0 && pathIdx < path.size()) ? path.at(pathIdx) : QString();
        if (pathV == lca) ascending = false;
      }

      if (!ascending) {
        // while (pathIdx < path.length - 1 && minRank(path[pathIdx+1]) <= node.rank)
        while (pathIdx < path.size() - 1) {
          const QString next = path.at(pathIdx + 1);
          if (!(minRankOrInf(g, next) <= node->rank.value_or(INT_MAX))) break;
          ++pathIdx;
        }
        pathV = (pathIdx >= 0 && pathIdx < path.size()) ? path.at(pathIdx) : QString();
      }

      // g.setParent(cur, pathV) — undefined pathV ⇒ graph root (JS default).
      if (pathV.isNull())
        g.setParent(cur);
      else
        g.setParent(cur, pathV);

      const QList<QString> sucs = g.successors(cur);
      if (sucs.isEmpty()) break;  // malformed chain; avoid infinite loop
      cur = sucs.first();
    }
  }
}

}  // namespace muffin::mermaid::dagre
