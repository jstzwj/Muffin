#include "mermaid/dagre/Normalize.h"

#include "mermaid/dagre/DagreUtil.h"

namespace muffin::mermaid::dagre {

namespace {

void normalizeEdge(DagreGraph& g, const graphlib::Edge& e) {
  const QString v = e.v;
  int vRank = g.node(v)->rank.value_or(0);
  const QString w = e.w;
  const int wRank = g.node(w)->rank.value_or(0);
  const QString name = e.name;
  const DagreEdgeLabel edgeLabel = *g.edge(e);
  const std::optional<int> labelRank = edgeLabel.labelRank;

  if (wRank == vRank + 1) return;
  g.removeEdge(e);

  DagreEdgeLabel chainLabel = edgeLabel;  // value-captured; faithful for output
  QString cur = v;
  for (int i = 0, r = vRank + 1; r < wRank; ++i, ++r) {
    chainLabel.points.clear();
    DagreNodeLabel attrs;
    attrs.width = 0.0;
    attrs.height = 0.0;
    attrs.edgeLabel = chainLabel;
    attrs.edgeObj = e;
    attrs.rank = r;
    const QString dummy = addDummyNode(g, QStringLiteral("edge"), attrs, QStringLiteral("_d"));
    if (r == labelRank) {
      DagreNodeLabel* dn = g.node(dummy);
      dn->width = edgeLabel.width;
      dn->height = edgeLabel.height;
      dn->dummy = QStringLiteral("edge-label");
      dn->labelpos = edgeLabel.labelpos;
    }
    DagreEdgeLabel seg;
    seg.weight = edgeLabel.weight;
    if (e.hasName)
      g.setEdge(cur, dummy, seg, name);
    else
      g.setEdge(cur, dummy, seg);
    if (i == 0) g.graph()->dummyChains.append(dummy);
    cur = dummy;
  }
  DagreEdgeLabel seg;
  seg.weight = edgeLabel.weight;
  if (e.hasName)
    g.setEdge(cur, w, seg, name);
  else
    g.setEdge(cur, w, seg);
}

}  // namespace

void runNormalize(DagreGraph& g) {
  g.graph()->dummyChains.clear();
  const QList<graphlib::Edge> all = g.edges();
  for (const graphlib::Edge& e : all) normalizeEdge(g, e);
}

void undoNormalize(DagreGraph& g) {
  const QVector<QString> chains = g.graph()->dummyChains;
  for (const QString& v0 : chains) {
    QString v = v0;
    DagreNodeLabel* node = g.node(v);
    if (!node || !node->edgeObj.has_value() || !node->edgeLabel.has_value()) continue;
    const graphlib::Edge edgeObj = *node->edgeObj;
    DagreEdgeLabel base = *node->edgeLabel;
    base.points.clear();
    base.x.reset();
    base.y.reset();
    g.setEdge(edgeObj, base);
    DagreEdgeLabel* live = g.edge(edgeObj);
    if (!live) continue;
    live->points.clear();
    while (node && !node->dummy.isEmpty()) {
      const QList<QString> sucs = g.successors(v);
      const QString w = sucs.isEmpty() ? QString() : sucs.first();
      live->points.append(QPointF(node->x.value_or(0.0), node->y.value_or(0.0)));
      if (node->dummy == QStringLiteral("edge-label")) {
        live->x = node->x;
        live->y = node->y;
        live->width = node->width;
        live->height = node->height;
      }
      g.removeNode(v);
      v = w;
      node = v.isEmpty() ? nullptr : g.node(v);
    }
  }
}

}  // namespace muffin::mermaid::dagre
