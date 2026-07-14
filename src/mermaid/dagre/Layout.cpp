#include "mermaid/dagre/Layout.h"

#include "mermaid/dagre/Acyclic.h"
#include "mermaid/dagre/AddBorderSegments.h"
#include "mermaid/dagre/CoordinateSystem.h"
#include "mermaid/dagre/DagreUtil.h"
#include "mermaid/dagre/NestingGraph.h"
#include "mermaid/dagre/Normalize.h"
#include "mermaid/dagre/Order.h"
#include "mermaid/dagre/ParentDummyChains.h"
#include "mermaid/dagre/Position.h"
#include "mermaid/dagre/Rank.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <cstdio>

#include <algorithm>
#include <limits>

namespace muffin::mermaid::dagre {

namespace {

void makeSpaceForEdgeLabels(DagreGraph& g) {
  DagreGraphLabel* graph = g.graph();
  graph->ranksep /= 2.0;
  const QString dir = graph->rankdir;
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* edge = g.edge(e);
    edge->minlen *= 2;
    if (edge->labelpos.toLower() != QLatin1String("c")) {
      if (dir == QLatin1String("TB") || dir == QLatin1String("BT"))
        edge->width += edge->labeloffset;
      else
        edge->height += edge->labeloffset;
    }
  }
}

void removeSelfEdges(DagreGraph& g) {
  const QList<graphlib::Edge> all = g.edges();
  for (const graphlib::Edge& e : all) {
    if (e.v != e.w) continue;
    DagreNodeLabel* node = g.node(e.v);
    DagreSelfEdge se;
    se.e = e;
    if (const DagreEdgeLabel* l = g.edge(e)) se.label = *l;
    node->selfEdges.append(se);
    g.removeEdge(e);
  }
}

void insertSelfEdges(DagreGraph& g) {
  const QVector<QList<QString>> layers = buildLayerMatrix(g);
  for (const QList<QString>& layer : layers) {
    int orderShift = 0;
    for (qsizetype i = 0; i < layer.size(); ++i) {
      const QString v = layer[i];
      DagreNodeLabel* node = g.node(v);
      node->order = static_cast<int>(i) + orderShift;
      for (const DagreSelfEdge& se : node->selfEdges) {
        ++orderShift;
        DagreNodeLabel attrs;
        attrs.width = se.label.width;
        attrs.height = se.label.height;
        attrs.rank = node->rank;
        attrs.order = static_cast<int>(i) + orderShift;
        attrs.edgeObj = se.e;
        attrs.edgeLabel = se.label;
        addDummyNode(g, QStringLiteral("selfedge"), attrs, QStringLiteral("_se"));
      }
      node->selfEdges.clear();
    }
  }
}

void positionSelfEdges(DagreGraph& g) {
  const QList<QString> nodes = g.nodes();
  for (const QString& v : nodes) {
    DagreNodeLabel* node = g.node(v);
    if (node->dummy != QLatin1String("selfedge")) continue;
    if (!node->edgeObj.has_value() || !node->edgeLabel.has_value()) {
      g.removeNode(v);
      continue;
    }
    const graphlib::Edge e = *node->edgeObj;
    DagreEdgeLabel label = *node->edgeLabel;
    const DagreNodeLabel* selfNode = g.node(e.v);
    const qreal x = selfNode->x.value_or(0.0) + selfNode->width / 2.0;
    const qreal y = selfNode->y.value_or(0.0);
    const qreal dx = node->x.value_or(0.0) - x;
    const qreal dy = selfNode->height / 2.0;
    g.setEdge(e, label);
    DagreEdgeLabel* live = g.edge(e);
    if (!live) {
      g.removeNode(v);
      continue;
    }
    live->points = {QPointF(x + (2 * dx) / 3, y - dy), QPointF(x + (5 * dx) / 6, y - dy),
                    QPointF(x + dx, y), QPointF(x + (5 * dx) / 6, y + dy),
                    QPointF(x + (2 * dx) / 3, y + dy)};
    live->x = node->x;
    live->y = node->y;
    g.removeNode(v);
  }
}

void injectEdgeLabelProxies(DagreGraph& g) {
  const QList<graphlib::Edge> all = g.edges();
  for (const graphlib::Edge& e : all) {
    const DagreEdgeLabel* edge = g.edge(e);
    if (edge->width && edge->height) {
      const DagreNodeLabel* v = g.node(e.v);
      const DagreNodeLabel* w = g.node(e.w);
      DagreNodeLabel label;
      label.rank = (w->rank.value_or(0) - v->rank.value_or(0)) / 2 + v->rank.value_or(0);
      label.edgeObj = e;
      addDummyNode(g, QStringLiteral("edge-proxy"), label, QStringLiteral("_ep"));
    }
  }
}

void removeEdgeLabelProxies(DagreGraph& g) {
  const QList<QString> nodes = g.nodes();
  for (const QString& v : nodes) {
    const DagreNodeLabel* node = g.node(v);
    if (node->dummy == QLatin1String("edge-proxy") && node->edgeObj.has_value()) {
      if (DagreEdgeLabel* edge = g.edge(*node->edgeObj)) edge->labelRank = node->rank;
      g.removeNode(v);
    }
  }
}

void assignRankMinMax(DagreGraph& g) {
  int maxRank = 0;
  for (const QString& v : g.nodes()) {
    DagreNodeLabel* node = g.node(v);
    if (!node->borderTop.isEmpty()) {
      node->minRank = g.node(node->borderTop)->rank;
      node->maxRank = g.node(node->borderBottom)->rank;
      maxRank = std::max(maxRank, node->maxRank.value_or(0));
    }
  }
  g.graph()->maxRank = maxRank;
}

void removeBorderNodes(DagreGraph& g) {
  // First pass: derive cluster dimensions from border dummies.
  const QList<QString> nodes = g.nodes();
  for (const QString& v : nodes) {
    if (g.children(v).value_or(QList<QString>{}).isEmpty()) continue;
    DagreNodeLabel* node = g.node(v);
    if (node->borderTop.isEmpty() || node->borderBottom.isEmpty()) continue;
    const DagreNodeLabel* t = g.node(node->borderTop);
    const DagreNodeLabel* b = g.node(node->borderBottom);
    const QString lId = node->borderLeft.isEmpty() ? QString() : node->borderLeft.last();
    const QString rId = node->borderRight.isEmpty() ? QString() : node->borderRight.last();
    const DagreNodeLabel* l = lId.isEmpty() ? nullptr : g.node(lId);
    const DagreNodeLabel* r = rId.isEmpty() ? nullptr : g.node(rId);
    const qreal lx = l ? l->x.value_or(0.0) : 0.0;
    const qreal rx = r ? r->x.value_or(0.0) : 0.0;
    const qreal ty = t ? t->y.value_or(0.0) : 0.0;
    const qreal by = b ? b->y.value_or(0.0) : 0.0;
    node->width = std::abs(rx - lx);
    node->height = std::abs(by - ty);
    node->x = lx + node->width / 2.0;
    node->y = ty + node->height / 2.0;
  }
  // Second pass: remove border dummies.
  const QList<QString> nodes2 = g.nodes();
  for (const QString& v : nodes2) {
    if (g.node(v)->dummy == QLatin1String("border")) g.removeNode(v);
  }
}

void fixupEdgeLabelCoords(DagreGraph& g) {
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* edge = g.edge(e);
    if (!edge->x.has_value()) continue;
    if (edge->labelpos == QLatin1String("l") || edge->labelpos == QLatin1String("r"))
      edge->width -= edge->labeloffset;
    if (edge->labelpos == QLatin1String("l")) edge->x = *edge->x - edge->width / 2.0 - edge->labeloffset;
    else if (edge->labelpos == QLatin1String("r")) edge->x = *edge->x + edge->width / 2.0 + edge->labeloffset;
  }
}

void translateGraph(DagreGraph& g) {
  qreal minX = std::numeric_limits<qreal>::infinity();
  qreal maxX = 0.0;
  qreal minY = std::numeric_limits<qreal>::infinity();
  qreal maxY = 0.0;
  auto extremes = [&](const DagreNodeLabel* n) {
    const qreal x = n->x.value_or(0.0);
    const qreal y = n->y.value_or(0.0);
    minX = std::min(minX, x - n->width / 2.0);
    maxX = std::max(maxX, x + n->width / 2.0);
    minY = std::min(minY, y - n->height / 2.0);
    maxY = std::max(maxY, y + n->height / 2.0);
  };
  for (const QString& v : g.nodes())
    if (g.node(v)) extremes(g.node(v));
  for (const graphlib::Edge& e : g.edges()) {
    const DagreEdgeLabel* edge = g.edge(e);
    if (!edge || !edge->x.has_value()) continue;
    DagreNodeLabel proxy;
    proxy.x = edge->x;
    proxy.y = edge->y;
    proxy.width = edge->width;
    proxy.height = edge->height;
    extremes(&proxy);
  }
  const qreal marginX = g.graph()->marginx;
  const qreal marginY = g.graph()->marginy;
  minX -= marginX;
  minY -= marginY;
  for (const QString& v : g.nodes()) {
    DagreNodeLabel* node = g.node(v);
    if (node->x.has_value()) node->x = *node->x - minX;
    if (node->y.has_value()) node->y = *node->y - minY;
  }
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* edge = g.edge(e);
    for (QPointF& p : edge->points) { p.setX(p.x() - minX); p.setY(p.y() - minY); }
    if (edge->x.has_value()) edge->x = *edge->x - minX;
    if (edge->y.has_value()) edge->y = *edge->y - minY;
  }
  g.graph()->width = maxX - minX + marginX;
  g.graph()->height = maxY - minY + marginY;
}

void assignNodeIntersects(DagreGraph& g) {
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* edge = g.edge(e);
    const DagreNodeLabel* nodeV = g.node(e.v);
    const DagreNodeLabel* nodeW = g.node(e.w);
    QPointF p1, p2;
    if (edge->points.isEmpty()) {
      // p1 = nodeW, p2 = nodeV (intersectRect reads x/y/width/height from node)
      DagreNodeLabel wProxy = *nodeW;
      DagreNodeLabel vProxy = *nodeV;
      p1 = intersectRect(wProxy, QPointF(nodeV->x.value_or(0.0), nodeV->y.value_or(0.0)));
      p2 = intersectRect(vProxy, QPointF(nodeW->x.value_or(0.0), nodeW->y.value_or(0.0)));
    } else {
      p1 = edge->points.first();
      p2 = edge->points.last();
    }
    edge->points.prepend(intersectRect(*nodeV, p1));
    edge->points.append(intersectRect(*nodeW, p2));
  }
}

void reversePointsForReversedEdges(DagreGraph& g) {
  for (const graphlib::Edge& e : g.edges()) {
    DagreEdgeLabel* edge = g.edge(e);
    if (edge->reversed) std::reverse(edge->points.begin(), edge->points.end());
  }
}

void positionY(DagreGraph& g) {
  const QVector<QList<QString>> layering = buildLayerMatrix(g);
  const qreal rankSep = g.graph()->ranksep;
  qreal prevY = 0.0;
  for (const QList<QString>& layer : layering) {
    qreal maxHeight = 0.0;
    for (const QString& v : layer) maxHeight = std::max(maxHeight, g.node(v)->height);
    for (const QString& v : layer) g.node(v)->y = prevY + maxHeight / 2.0;
    prevY += maxHeight + rankSep;
  }
}

void position(DagreGraph& g) {
  positionY(g);
  const QHash<QString, qreal> xs = positionX(g);
  for (auto it = xs.cbegin(); it != xs.cend(); ++it)
    if (DagreNodeLabel* node = g.node(it.key())) node->x = it.value();
}

// Snapshot serializer for the G2 intermediate-state golden. Emits an
// implementation-independent census so native (per-graph nextDummyId counter)
// and upstream (module-global _.uniqueId) can be compared without matching
// divergent dummy ids: real nodes carry full rank/order/parent/minRank/maxRank;
// dummy nodes are counted by type; edge endpoints that are dummies are
// abstracted to "dummy:<type>". Everything is sorted for deterministic output.
QJsonObject serializeGraph(const DagreGraph& g) {
  QStringList realIds;
  QHash<QString, int> dummyCounts;
  for (const QString& v : g.nodes()) {
    const DagreNodeLabel* n = g.node(v);
    if (!n) continue;
    if (n->dummy.isEmpty())
      realIds.append(v);
    else
      ++dummyCounts[n->dummy];
  }
  realIds.sort();

  auto nullOrInt = [](const std::optional<int>& v) -> QJsonValue {
    return v.has_value() ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
  };

  QJsonArray realNodes;
  for (const QString& id : realIds) {
    const DagreNodeLabel* n = g.node(id);
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("rank"), nullOrInt(n->rank));
    o.insert(QStringLiteral("order"), nullOrInt(n->order));
    const QString parent = g.parentOf(id);
    o.insert(QStringLiteral("parent"), parent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(parent));
    o.insert(QStringLiteral("minRank"), nullOrInt(n->minRank));
    o.insert(QStringLiteral("maxRank"), nullOrInt(n->maxRank));
    realNodes.append(o);
  }

  QJsonObject dummyJson;
  QStringList dummyTypes = dummyCounts.keys();
  dummyTypes.sort();
  for (const QString& type : dummyTypes) dummyJson.insert(type, dummyCounts.value(type));

  // Endpoint label: real id verbatim, dummy → "dummy:<type>".
  auto endpoint = [&g](const QString& v) -> QString {
    const DagreNodeLabel* n = g.node(v);
    if (n && !n->dummy.isEmpty()) return QStringLiteral("dummy:") + n->dummy;
    return v;
  };

  QList<QJsonObject> edgeObjs;
  for (const graphlib::Edge& e : g.edges()) {
    const DagreEdgeLabel* label = g.edge(e);
    QJsonObject o;
    o.insert(QStringLiteral("v"), endpoint(e.v));
    o.insert(QStringLiteral("w"), endpoint(e.w));
    o.insert(QStringLiteral("minlen"), label ? label->minlen : 0);
    o.insert(QStringLiteral("weight"), label ? label->weight : 0);
    edgeObjs.append(o);
  }
  std::sort(edgeObjs.begin(), edgeObjs.end(), [](const QJsonObject& a, const QJsonObject& b) {
    const QString va = a.value(QStringLiteral("v")).toString(), wa = a.value(QStringLiteral("w")).toString();
    const QString vb = b.value(QStringLiteral("v")).toString(), wb = b.value(QStringLiteral("w")).toString();
    return va == vb ? wa < wb : va < vb;
  });
  QJsonArray edgesJson;
  for (const QJsonObject& o : edgeObjs) edgesJson.append(o);

  QJsonObject state;
  state.insert(QStringLiteral("realNodes"), realNodes);
  state.insert(QStringLiteral("dummyCounts"), dummyJson);
  state.insert(QStringLiteral("edges"), edgesJson);
  return state;
}

}  // namespace

void runDagreLayout(DagreGraph& g, std::vector<DagreSnapshot>* snapshots) {
  makeSpaceForEdgeLabels(g);
  removeSelfEdges(g);
  runAcyclic(g);
  runNestingGraph(g);
  rankGraph(g);
  if (snapshots) snapshots->push_back({QStringLiteral("rank"), serializeGraph(g)});
  injectEdgeLabelProxies(g);
  removeEmptyRanks(g);
  cleanupNestingGraph(g);
  normalizeRanks(g);
  assignRankMinMax(g);
  removeEdgeLabelProxies(g);
  runNormalize(g);
  parentDummyChains(g);
  if (snapshots) snapshots->push_back({QStringLiteral("parentDummyChains"), serializeGraph(g)});
  addBorderSegments(g);
  if (snapshots) snapshots->push_back({QStringLiteral("addBorderSegments"), serializeGraph(g)});
  order(g);
  if (snapshots) snapshots->push_back({QStringLiteral("order"), serializeGraph(g)});
  insertSelfEdges(g);
  adjustCoordinateSystem(g);
  position(g);
  positionSelfEdges(g);
  removeBorderNodes(g);
  undoNormalize(g);
  fixupEdgeLabelCoords(g);
  undoCoordinateSystem(g);
  translateGraph(g);
  assignNodeIntersects(g);
  reversePointsForReversedEdges(g);
  undoAcyclic(g);
}

}  // namespace muffin::mermaid::dagre
