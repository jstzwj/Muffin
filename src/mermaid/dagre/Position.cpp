#include "mermaid/dagre/Position.h"

#include "mermaid/dagre/DagreUtil.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::dagre {

namespace {

using ConflictSet = QHash<QString, QSet<QString>>;

// Block graph for horizontal compaction: nodes are block roots, edge labels are
// the qreal separation distance (NOT DagreEdgeLabel, whose weight is int).
struct BlockNode {};
using BlockGraph = graphlib::Graph<BlockNode, double>;

void addConflict(ConflictSet& conflicts, QString v, QString w) {
  if (v > w) std::swap(v, w);
  conflicts[v].insert(w);
}

bool hasConflict(const ConflictSet& conflicts, const QString& v, const QString& w) {
  QString a = v, b = w;
  if (a > b) std::swap(a, b);
  auto it = conflicts.constFind(a);
  return it != conflicts.cend() && it.value().contains(b);
}

QString findOtherInnerSegmentNode(const DagreGraph& g, const QString& v) {
  if (g.node(v)->dummy.isEmpty()) return QString();
  for (const QString& u : g.predecessors(v))
    if (!g.node(u)->dummy.isEmpty()) return u;
  return QString();
}

ConflictSet findType1Conflicts(const DagreGraph& g, const QVector<QList<QString>>& layering) {
  ConflictSet conflicts;
  for (qsizetype li = 1; li < layering.size(); ++li) {
    const QList<QString>& prevLayer = layering[li - 1];
    const QList<QString>& layer = layering[li];
    int k0 = 0;
    int scanPos = 0;
    const qsizetype prevLayerLength = prevLayer.size();
    const QString lastNode = layer.isEmpty() ? QString() : layer.last();
    for (qsizetype i = 0; i < layer.size(); ++i) {
      const QString& v = layer[i];
      const QString w = findOtherInnerSegmentNode(g, v);
      const int k1 = !w.isNull() ? g.node(w)->order.value_or(0) : static_cast<int>(prevLayerLength);
      if (!w.isNull() || v == lastNode) {
        for (int scan = scanPos; scan <= static_cast<int>(i); ++scan) {
          const QString& scanNode = layer[scan];
          for (const QString& u : g.predecessors(scanNode)) {
            const int uPos = g.node(u)->order.value_or(0);
            const bool uDummy = !g.node(u)->dummy.isEmpty();
            const bool scanDummy = !g.node(scanNode)->dummy.isEmpty();
            if ((uPos < k0 || k1 < uPos) && !(uDummy && scanDummy))
              addConflict(conflicts, u, scanNode);
          }
        }
        scanPos = static_cast<int>(i) + 1;
        k0 = k1;
      }
    }
  }
  return conflicts;
}

ConflictSet findType2Conflicts(const DagreGraph& g, const QVector<QList<QString>>& layering) {
  ConflictSet conflicts;
  auto scan = [&](const QList<QString>& south, int southPos, int southEnd, int prevNorthBorder,
                  int nextNorthBorder) {
    for (int i = southPos; i < southEnd; ++i) {
      const QString& v = south[i];
      if (g.node(v)->dummy == QLatin1String("border")) {
        for (const QString& u : g.predecessors(v)) {
          const DagreNodeLabel* uNode = g.node(u);
          if (!uNode->dummy.isEmpty() &&
              (uNode->order.value_or(0) < prevNorthBorder || uNode->order.value_or(0) > nextNorthBorder))
            addConflict(conflicts, u, v);
        }
      }
    }
  };
  for (qsizetype li = 1; li < layering.size(); ++li) {
    const QList<QString>& north = layering[li - 1];
    const QList<QString>& south = layering[li];
    int prevNorthPos = -1;
    int nextNorthPos = 0;
    int southPos = 0;
    for (qsizetype southLookahead = 0; southLookahead < south.size(); ++southLookahead) {
      const QString& v = south[southLookahead];
      if (g.node(v)->dummy == QLatin1String("border")) {
        const QList<QString> preds = g.predecessors(v);
        if (!preds.isEmpty()) {
          nextNorthPos = g.node(preds.first())->order.value_or(0);
          scan(south, southPos, static_cast<int>(southLookahead), prevNorthPos, nextNorthPos);
          southPos = static_cast<int>(southLookahead);
          prevNorthPos = nextNorthPos;
        }
      }
      scan(south, southPos, south.size(), nextNorthPos, static_cast<int>(north.size()));
    }
  }
  return conflicts;
}

struct Alignment {
  QHash<QString, QString> root;
  QHash<QString, QString> align;
};

Alignment verticalAlignment(const DagreGraph& g, const QVector<QList<QString>>& layering,
                            const ConflictSet& conflicts,
                            const std::function<QList<QString>(const QString&)>& neighborFn) {
  Alignment result;
  QHash<QString, int> pos;
  for (const QList<QString>& layer : layering)
    for (qsizetype i = 0; i < layer.size(); ++i) {
      const QString& v = layer[i];
      result.root[v] = v;
      result.align[v] = v;
      pos[v] = static_cast<int>(i);
    }
  for (const QList<QString>& layer : layering) {
    int prevIdx = -1;
    for (const QString& v : layer) {
      QList<QString> ws = neighborFn(v);
      if (ws.isEmpty()) continue;
      std::sort(ws.begin(), ws.end(), [&](const QString& a, const QString& b) { return pos[a] < pos[b]; });
      const qreal mp = (ws.size() - 1) / 2.0;
      for (int i = static_cast<int>(std::floor(mp)); i <= static_cast<int>(std::ceil(mp)); ++i) {
        const QString& w = ws[i];
        if (result.align[v] == v && prevIdx < pos[w] && !hasConflict(conflicts, v, w)) {
          result.align[w] = v;
          result.root[v] = result.root[w];
          result.align[v] = result.root[w];
          prevIdx = pos[w];
        }
      }
    }
  }
  return result;
}

qreal sepNodeWidth(const DagreGraph& g, const QString& v) { return g.node(v)->width; }  // NOLINT

// sep(nodeSep, edgeSep, reverseSep) closure -> distance(v, w)
std::function<qreal(const DagreGraph&, const QString&, const QString&)> makeSep(qreal nodeSep,
                                                                                 qreal edgeSep,
                                                                                 bool reverseSep) {
  return [nodeSep, edgeSep, reverseSep](const DagreGraph& g, const QString& v, const QString& w) {
    const DagreNodeLabel* vL = g.node(v);
    const DagreNodeLabel* wL = g.node(w);
    qreal sum = 0.0;
    qreal delta = 0.0;
    sum += vL->width / 2.0;
    if (!vL->labelpos.isEmpty()) {
      const QString lp = vL->labelpos.toLower();
      if (lp == QLatin1String("l")) delta = -vL->width / 2.0;
      else if (lp == QLatin1String("r")) delta = vL->width / 2.0;
    }
    if (delta != 0.0) sum += reverseSep ? delta : -delta;
    delta = 0.0;
    sum += (vL->dummy.isEmpty() ? nodeSep : edgeSep) / 2.0;
    sum += (wL->dummy.isEmpty() ? nodeSep : edgeSep) / 2.0;
    sum += wL->width / 2.0;
    if (!wL->labelpos.isEmpty()) {
      const QString lp = wL->labelpos.toLower();
      if (lp == QLatin1String("l")) delta = wL->width / 2.0;
      else if (lp == QLatin1String("r")) delta = -wL->width / 2.0;
    }
    if (delta != 0.0) sum += reverseSep ? delta : -delta;
    return sum;
  };
}

BlockGraph buildBlockGraph(const DagreGraph& g, const QVector<QList<QString>>& layering,
                           const QHash<QString, QString>& root, bool reverseSep) {
  BlockGraph blockG({.directed = true, .multigraph = false, .compound = false});
  const qreal nodeSep = g.graph()->nodesep;
  const qreal edgeSep = g.graph()->edgesep;
  auto sepFn = makeSep(nodeSep, edgeSep, reverseSep);
  for (const QList<QString>& layer : layering) {
    QString u;
    for (const QString& v : layer) {
      const QString vRoot = root.value(v);
      blockG.setNode(vRoot);
      if (!u.isNull()) {
        const QString uRoot = root.value(u);
        const double* prevEdge = blockG.edge(uRoot, vRoot);
        const double prev = prevEdge ? *prevEdge : 0.0;
        blockG.setEdge(uRoot, vRoot, std::max(sepFn(g, v, u), prev));
      }
      u = v;
    }
  }
  return blockG;
}

QHash<QString, qreal> horizontalCompaction(const DagreGraph& g,
                                           const QVector<QList<QString>>& layering,
                                           const QHash<QString, QString>& root,
                                           const QHash<QString, QString>& align, bool reverseSep) {
  QHash<QString, qreal> xs;
  BlockGraph blockG = buildBlockGraph(g, layering, root, reverseSep);
  const QString borderType = reverseSep ? QStringLiteral("borderLeft") : QStringLiteral("borderRight");

  auto iterate = [&](const std::function<void(const QString&)>& setXs,
                     const std::function<QList<QString>(const QString&)>& nextNodes) {
    QStringList stack = blockG.nodes();
    QSet<QString> visited;
    while (!stack.isEmpty()) {
      const QString elem = stack.takeLast();
      if (visited.contains(elem)) {
        setXs(elem);
      } else {
        visited.insert(elem);
        stack.append(elem);
        stack.append(nextNodes(elem));
      }
    }
  };

  // pass1: xs[elem] = max over inEdges of (xs[e.v] + blockG.edge(e))
  auto pass1 = [&](const QString& elem) {
    qreal acc = 0.0;
    for (const graphlib::Edge& e : blockG.inEdges(elem)) {
      const double* d = blockG.edge(e);
      const qreal w = d ? *d : 0.0;
      acc = std::max(acc, xs.value(e.v, 0.0) + w);
    }
    xs[elem] = acc;
  };
  // pass2: min over outEdges of (xs[e.w] - blockG.edge(e)); respect borderType
  auto pass2 = [&](const QString& elem) {
    qreal mn = std::numeric_limits<qreal>::infinity();
    for (const graphlib::Edge& e : blockG.outEdges(elem)) {
      const double* d = blockG.edge(e);
      const qreal w = d ? *d : 0.0;
      mn = std::min(mn, xs.value(e.w, 0.0) - w);
    }
    const DagreNodeLabel* node = g.node(elem);
    if (mn != std::numeric_limits<qreal>::infinity() && (!node || node->borderType != borderType))
      xs[elem] = std::max(xs.value(elem, 0.0), mn);
  };
  iterate(pass1, [&](const QString& e) { return blockG.predecessors(e); });
  iterate(pass2, [&](const QString& e) { return blockG.successors(e); });

  // Assign x to all nodes via their root.
  QHash<QString, qreal> result;
  for (auto it = align.cbegin(); it != align.cend(); ++it)
    result[it.key()] = xs.value(root.value(it.key()), 0.0);
  return result;
}

const QString& widthOf(const DagreGraph& g, const QString& v) { return v; }  // placeholder, unused

QHash<QString, qreal> findSmallestWidthAlignment(const DagreGraph& g,
                                                  const QHash<QString, QHash<QString, qreal>>& xss) {
  QString bestKey;
  qreal bestWidth = std::numeric_limits<qreal>::max();
  for (auto it = xss.cbegin(); it != xss.cend(); ++it) {
    qreal mx = -std::numeric_limits<qreal>::infinity();
    qreal mn = std::numeric_limits<qreal>::infinity();
    for (auto xit = it.value().cbegin(); xit != it.value().cend(); ++xit) {
      const qreal half = g.node(xit.key())->width / 2.0;
      mx = std::max(mx, xit.value() + half);
      mn = std::min(mn, xit.value() - half);
    }
    const qreal w = mx - mn;
    if (w < bestWidth) {
      bestWidth = w;
      bestKey = it.key();
    }
  }
  return bestKey.isEmpty() ? QHash<QString, qreal>() : xss.value(bestKey);
}

void alignCoordinates(QHash<QString, QHash<QString, qreal>>& xss, const QHash<QString, qreal>& alignTo) {
  QList<qreal> alignToVals = alignTo.values();
  const qreal alignToMin = *std::min_element(alignToVals.begin(), alignToVals.end());
  const qreal alignToMax = *std::max_element(alignToVals.begin(), alignToVals.end());
  for (const QString& vert : {QStringLiteral("u"), QStringLiteral("d")}) {
    for (const QString& horiz : {QStringLiteral("l"), QStringLiteral("r")}) {
      const QString alignment = vert + horiz;
      QHash<QString, qreal> xs = xss.value(alignment);
      if (xs == alignTo) continue;
      const QList<qreal> vals = xs.values();
      const qreal xsMin = *std::min_element(vals.begin(), vals.end());
      const qreal xsMax = *std::max_element(vals.begin(), vals.end());
      const qreal delta = horiz == QLatin1String("l") ? alignToMin - xsMin : alignToMax - xsMax;
      if (delta != 0.0) {
        QHash<QString, qreal> shifted;
        for (auto it = xs.cbegin(); it != xs.cend(); ++it) shifted[it.key()] = it.value() + delta;
        xss[alignment] = shifted;
      }
    }
  }
}

QHash<QString, qreal> balance(const QHash<QString, QHash<QString, qreal>>& xss, const QString& align) {
  const QHash<QString, qreal>& ul = xss.value(QStringLiteral("ul"));
  QHash<QString, qreal> result;
  for (auto it = ul.cbegin(); it != ul.cend(); ++it) {
    const QString& v = it.key();
    if (!align.isEmpty()) {
      result[v] = xss.value(align.toLower()).value(v);
    } else {
      QList<qreal> vals;
      for (auto xit = xss.cbegin(); xit != xss.cend(); ++xit) vals.append(xit.value().value(v));
      std::sort(vals.begin(), vals.end());
      result[v] = (vals.value(1) + vals.value(2)) / 2.0;
    }
  }
  return result;
}

}  // namespace

QHash<QString, qreal> positionX(DagreGraph& g) {
  const QVector<QList<QString>> layering = buildLayerMatrix(g);
  ConflictSet conflicts = findType1Conflicts(g, layering);
  ConflictSet t2 = findType2Conflicts(g, layering);
  for (auto it = t2.cbegin(); it != t2.cend(); ++it)
    for (const QString& w : it.value()) conflicts[it.key()].insert(w);

  QHash<QString, QHash<QString, qreal>> xss;
  for (const QString& vert : {QStringLiteral("u"), QStringLiteral("d")}) {
    QVector<QList<QString>> adjusted = layering;
    if (vert == QLatin1String("d"))
      std::reverse(adjusted.begin(), adjusted.end());
    for (const QString& horiz : {QStringLiteral("l"), QStringLiteral("r")}) {
      QVector<QList<QString>> inner = adjusted;
      if (horiz == QLatin1String("r"))
        for (QList<QString>& layer : inner) std::reverse(layer.begin(), layer.end());
      auto neighborFn = [vert, &g](const QString& v) {
        return vert == QLatin1String("u") ? g.predecessors(v) : g.successors(v);
      };
      const Alignment al = verticalAlignment(g, inner, conflicts, neighborFn);
      QHash<QString, qreal> xs = horizontalCompaction(g, inner, al.root, al.align, horiz == QLatin1String("r"));
      if (horiz == QLatin1String("r"))
        for (auto it = xs.begin(); it != xs.end(); ++it) it.value() = -it.value();
      xss[vert + horiz] = xs;
    }
  }

  const QHash<QString, qreal> smallest = findSmallestWidthAlignment(g, xss);
  alignCoordinates(xss, smallest);
  const QString align = g.graph()->align;
  return balance(xss, align);
}

}  // namespace muffin::mermaid::dagre
