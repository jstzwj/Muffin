#include "mermaid/dagre/Acyclic.h"

#include <QHash>
#include <QList>
#include <QSet>

namespace muffin::mermaid::dagre {

namespace {

// dfsFAS: depth-first feedback arc set. Mirrors acyclic.js dfsFAS.
QList<graphlib::Edge> dfsFas(const DagreGraph& g) {
  QList<graphlib::Edge> fas;
  QSet<QString> stack;
  QSet<QString> visited;
  std::function<void(const QString&)> dfs = [&](const QString& v) {
    if (visited.contains(v)) return;
    visited.insert(v);
    stack.insert(v);
    for (const graphlib::Edge& e : g.outEdges(v)) {
      if (stack.contains(e.w)) fas.append(e);
      else dfs(e.w);
    }
    stack.remove(v);
  };
  for (const QString& v : g.nodes()) dfs(v);
  return fas;
}

}  // namespace

void runAcyclic(DagreGraph& g) {
  const QList<graphlib::Edge> fas = dfsFas(g);
  for (const graphlib::Edge& e : fas) {
    DagreEdgeLabel label = *g.edge(e);
    g.removeEdge(e);
    label.forwardName = e.hasName ? std::optional<QString>(e.name) : std::nullopt;
    label.reversed = true;
    // _.uniqueId('rev') shares dagre's global counter; we use the per-graph
    // dummy counter (same one addDummyNode uses) to keep numbering faithful.
    const QString revName = QStringLiteral("rev") + QString::number(++g.graph()->nextDummyId);
    g.setEdge(e.w, e.v, label, revName);
  }
}

void undoAcyclic(DagreGraph& g) {
  const QList<graphlib::Edge> all = g.edges();
  for (const graphlib::Edge& e : all) {
    DagreEdgeLabel* label = g.edge(e);
    if (!label || !label->reversed) continue;
    DagreEdgeLabel copy = *label;
    g.removeEdge(e);
    copy.reversed = false;
    std::optional<QString> forwardName = copy.forwardName;
    copy.forwardName.reset();
    if (forwardName.has_value())
      g.setEdge(e.w, e.v, copy, *forwardName);
    else
      g.setEdge(e.w, e.v, copy);
  }
}

}  // namespace muffin::mermaid::dagre
