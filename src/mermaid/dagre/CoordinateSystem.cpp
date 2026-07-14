#include "mermaid/dagre/CoordinateSystem.h"

#include <QList>

namespace muffin::mermaid::dagre {

namespace {

void swapWidthHeightNode(DagreNodeLabel& a) { std::swap(a.width, a.height); }
void swapWidthHeightEdge(DagreEdgeLabel& a) { std::swap(a.width, a.height); }

void swapXYNode(DagreNodeLabel& a) { std::swap(a.x, a.y); }
void swapXYPoint(QPointF& p) { QPointF t(p.y(), p.x()); p = t; }

void reverseYNode(DagreNodeLabel& a) {
  if (a.y.has_value()) a.y = -*a.y;
}
void reverseYPoint(QPointF& p) { p.setY(-p.y()); }

void swapWidthHeight(DagreGraph& g) {
  for (const QString& v : g.nodes())
    if (DagreNodeLabel* n = g.node(v)) swapWidthHeightNode(*n);
  for (const graphlib::Edge& e : g.edges())
    if (DagreEdgeLabel* l = g.edge(e)) swapWidthHeightEdge(*l);
}

void reverseY(DagreGraph& g) {
  for (const QString& v : g.nodes())
    if (DagreNodeLabel* n = g.node(v)) reverseYNode(*n);
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* l = g.edge(e);
    if (!l) continue;
    for (QPointF& p : l->points) reverseYPoint(p);
    if (l->y.has_value()) l->y = -*l->y;
  }
}

void swapXY(DagreGraph& g) {
  for (const QString& v : g.nodes())
    if (DagreNodeLabel* n = g.node(v)) swapXYNode(*n);
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* l = g.edge(e);
    if (!l) continue;
    for (QPointF& p : l->points) swapXYPoint(p);
    std::swap(l->x, l->y);
  }
}

}  // namespace

void adjustCoordinateSystem(DagreGraph& g) {
  const QString dir = g.graph()->rankdir.toLower();
  if (dir == QLatin1String("lr") || dir == QLatin1String("rl")) swapWidthHeight(g);
}

void undoCoordinateSystem(DagreGraph& g) {
  const QString dir = g.graph()->rankdir.toLower();
  if (dir == QLatin1String("bt") || dir == QLatin1String("rl")) reverseY(g);
  if (dir == QLatin1String("lr") || dir == QLatin1String("rl")) {
    swapXY(g);
    swapWidthHeight(g);
  }
}

}  // namespace muffin::mermaid::dagre
