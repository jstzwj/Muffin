#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/Layout.h"

#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::flowchart {
namespace {
namespace d = muffin::mermaid::dagre;
constexpr qreal kSelfLoopLabelRectExtent = 0.1;

QPointF intersectNodeForShape(const d::DagreNodeLabel& node, const QString& type, const QPointF& toward,
                              FlowLook look) {
  FlowVertex vertex;
  vertex.type = type;
  const QPointF center(node.x.value_or(0.0), node.y.value_or(0.0));
  return intersectFlowShape(
      vertex, QRectF(center.x() - node.width / 2.0, center.y() - node.height / 2.0, node.width, node.height), toward,
      look);
}

FlowLayoutResult layoutFlowchartNodesDagreScope(const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
                                                FlowLayoutOptions options,
                                                std::vector<dagre::DagreSnapshot>* dagreSnapshots,
                                                const QString& retainedRoot);

// cutPathAtIntersect(): a path's points up to the one where it enters the
// box, the point where it crosses in its place.
QVector<QPointF> cutPathAtIntersect(const QVector<QPointF>& points, const QRectF& box) {
  const qreal halfWidth = box.width() / 2.0;
  const qreal halfHeight = box.height() / 2.0;
  // intersection(): where the line from the point outside crosses into the box.
  const auto entry = [&](const QPointF& outside, const QPointF& inside) {
    const qreal x = box.center().x();
    const qreal y = box.center().y();
    const qreal dx = std::abs(x - inside.x());
    const qreal range = std::abs(outside.y() - inside.y());
    const qreal run = std::abs(outside.x() - inside.x());
    QPointF result;
    if (std::abs(y - outside.y()) * halfWidth > std::abs(x - outside.x()) * halfHeight) {
      // The top or the bottom of the box.
      const qreal q = inside.y() < outside.y() ? outside.y() - halfHeight - y : y - halfHeight - outside.y();
      const qreal r = (run * q) / range;
      result = QPointF(inside.x() < outside.x() ? inside.x() + r : inside.x() - run + r,
                       inside.y() < outside.y() ? inside.y() + range - q : inside.y() - range + q);
      if (r == 0.0)
        result = outside;
    } else {
      // A side of the box.
      const qreal r = inside.x() < outside.x() ? outside.x() - halfWidth - x : x - halfWidth - outside.x();
      const qreal q = (range * r) / run;
      result = QPointF(inside.x() < outside.x() ? inside.x() + run - r : inside.x() - run + r,
                       inside.y() < outside.y() ? inside.y() + q : inside.y() - q);
      if (r == 0.0)
        result = outside;
    }
    if (run == 0.0)
      result.setX(outside.x());
    if (range == 0.0)
      result.setY(outside.y());
    return result;
  };
  QVector<QPointF> cut;
  QPointF lastOutside = points.value(0);
  bool inside = false;
  for (const QPointF& point : points) {
    // outsideNode(): on the box's edge is outside.
    const bool outside =
        std::abs(point.x() - box.center().x()) >= halfWidth || std::abs(point.y() - box.center().y()) >= halfHeight;
    if (!outside && !inside) {
      const QPointF crossed = entry(lastOutside, point);
      if (!cut.contains(crossed))
        cut.append(crossed);
      inside = true;
    } else {
      lastOutside = point;
      if (!inside)
        cut.append(point);
    }
  }
  return cut;
}

// One recursive scope owns one Dagre graph. Each phase has a single purpose;
// only final geometry leaves the scope, so extraction cannot leak parent state.
class DagreScopeLayout {
public:
  DagreScopeLayout(const FlowchartData& source, const QMap<QString, QSizeF>& measured, FlowLayoutOptions settings,
                   QString root)
      : data(source), measuredNodes(measured), options(std::move(settings)), retainedRoot(std::move(root)),
        g({.directed = true, .multigraph = true, .compound = true}) {}

  FlowLayoutResult run(std::vector<dagre::DagreSnapshot>* snapshots) {
    indexSubgraphs();
    resolveClusterEdges();
    extractClusters();
    configureGraph();
    insertNodes();
    assignParents();
    insertEdges();
    d::runDagreLayout(g, snapshots);
    resolveOrigin();
    FlowLayoutResult result;
    collectNodes(result);
    collectEdges(result);
    collectClusters(result);
    restoreClusters(result);
    auto ordered = orderResult(std::move(result));
    adjustOriginAndTitles(ordered);
    buildPaths(ordered);
    return ordered;
  }

private:
  struct ExtractedCluster {
    QString id;
    QSet<QString> vertices;
    QSet<QString> subgraphs;
    FlowLayoutResult layout;
    QPointF center;
    QSizeF size;
    // Dagre's width of the root cluster's box and the width it is drawn at.
    qreal dagreWidth = 0.0;
    qreal boxWidth = 0.0;
  };

  // A copy: the relationships to clusters are moved to their anchors.
  FlowchartData data;
  const QMap<QString, QSizeF>& measuredNodes;
  const FlowLayoutOptions options;
  const QString retainedRoot;
  QSet<QString> subgraphIds;
  QHash<QString, QString> subgraphParent;
  QVector<ExtractedCluster> extracted;
  QSet<QString> claimedVertices;
  QSet<QString> claimedSubgraphs;
  QHash<QString, QString> extractedEndpoint;
  // A cluster with members: the node a relationship to it is anchored on, and
  // whether a relationship leaves it.
  struct Cluster {
    QString anchor;
    bool external = false;
  };
  QHash<QString, Cluster> clusters;
  // Per relationship, the cluster it leaves or enters (empty for none).
  QVector<QString> fromCluster;
  QVector<QString> toCluster;
  d::DagreGraph g;
  QString dir;
  QPointF origin;

  QString layoutEndpoint(const QString& vertex) const { return extractedEndpoint.value(vertex, vertex); }

  bool isSubgraphDescendant(QString child, const QString& ancestor) const {
    QSet<QString> guard;
    while (subgraphParent.contains(child) && !guard.contains(child)) {
      guard.insert(child);
      child = subgraphParent.value(child);
      if (child == ancestor)
        return true;
    }
    return false;
  }

  FlowEdgeLabelLayout preparedEdgeLabel(const FlowEdge& edge) const {
    FlowEdgeLabelLayout prepared = options.preparedEdgeLabels.value(edge.id);
    if (prepared.document.text.isNull())
      prepared = layoutFlowchartEdgeLabel(edge);
    // A measured 0x0 is AUTHORITATIVE, not "absent": state's display:none on
    // the edge-label <p> collapses the fo content — the label reserves no
    // space (edges and viewBox shrink like the browser's getBBox).
    const auto measuredIt = options.measuredEdgeLabels.constFind(edge.id);
    if (measuredIt != options.measuredEdgeLabels.constEnd())
      prepared.size = measuredIt.value();
    return prepared;
  }

  d::DagreEdgeLabel edgeLabel(const FlowEdge& edge) const {
    d::DagreEdgeLabel label;
    label.minlen = 1;
    label.weight = 1;
    label.labelpos = QStringLiteral("c");
    label.labeloffset = 10;
    if (!edge.text.isEmpty()) {
      const FlowEdgeLabelLayout prepared = preparedEdgeLabel(edge);
      label.width = prepared.size.width();
      label.height = prepared.size.height();
    }
    return label;
  }

  void indexSubgraphs() {
    for (const FlowSubgraph& sg : data.subgraphs)
      subgraphIds.insert(sg.id);
    for (const FlowSubgraph& sg : data.subgraphs)
      for (const QString& child : sg.nodes)
        if (subgraphIds.contains(child))
          subgraphParent.insert(child, sg.id);
    // A vertex named like a subgraph (a relationship written to the subgraph
    // names it as a node too) is that subgraph: mermaid's getData merges the
    // two into the cluster's node.
    data.vertices.erase(std::remove_if(data.vertices.begin(), data.vertices.end(),
                                       [&](const FlowVertex& vertex) { return subgraphIds.contains(vertex.id); }),
                        data.vertices.end());
  }

  // The nodes the layout lists for this scope, in its order: the clusters,
  // then the vertices (a family may prescribe the order itself).
  QStringList upstreamOrder() const {
    if (!options.nodeInsertionOrder.isEmpty())
      return options.nodeInsertionOrder;
    QStringList order;
    for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it)
      order.append(it->id);
    for (const FlowVertex& vertex : data.vertices)
      order.append(vertex.id);
    return order;
  }

  int subgraphDepth(const QString& id) const {
    int depth = 1;
    QString current = id;
    QSet<QString> guard;
    while (subgraphParent.contains(current) && !guard.contains(current)) {
      guard.insert(current);
      current = subgraphParent.value(current);
      ++depth;
    }
    return depth;
  }

  // The cluster a node belongs to: the deepest one that lists a vertex, the
  // listing cluster of a nested one.
  QString parentOf(const QString& id) const {
    if (subgraphIds.contains(id))
      return subgraphParent.value(id);
    QString best;
    int bestDepth = 0;
    for (const FlowSubgraph& sg : data.subgraphs)
      if (sg.nodes.contains(id)) {
        const int depth = subgraphDepth(sg.id);
        if (depth > bestDepth) {
          best = sg.id;
          bestDepth = depth;
        }
      }
    return best;
  }

  QStringList childrenOf(const QString& id) const {
    QStringList children;
    for (const QString& candidate : upstreamOrder())
      if (parentOf(candidate) == id)
        children.append(candidate);
    return children;
  }

  // What lies below a cluster, a nested cluster's contents before the
  // cluster itself.
  void collectClusterOrder(const QString& id, QStringList& order) const {
    for (const QString& child : childrenOf(id)) {
      if (subgraphIds.contains(child))
        collectClusterOrder(child, order);
      order.append(child);
    }
  }

  QSet<QString> descendantsOf(const QString& id) const {
    QSet<QString> result;
    QStringList pending = childrenOf(id);
    while (!pending.isEmpty()) {
      const QString child = pending.takeLast();
      if (result.contains(child))
        continue;
      result.insert(child);
      pending += childrenOf(child);
    }
    return result;
  }

  // Whether the leaf has a relationship the cluster has too, with the same
  // third node.
  bool mirrorsEdge(const QString& clusterId, const QString& leafId) const {
    const auto touches = [](const FlowEdge& edge, const QString& id) { return edge.start == id || edge.end == id; };
    for (const FlowEdge& a : data.edges) {
      if (!touches(a, clusterId))
        continue;
      const QString from = a.start == clusterId ? leafId : a.start;
      const QString to = a.end == clusterId ? leafId : a.end;
      for (const FlowEdge& b : data.edges)
        if (touches(b, leafId) && b.start == from && b.end == to)
          return true;
    }
    return false;
  }

  // findNonClusterChild(): the first thing below a cluster that is not a
  // cluster, passed over when it mirrors a relationship of the cluster's own.
  QString firstLeaf(const QString& id) const {
    const QStringList children = childrenOf(id);
    if (children.isEmpty())
      return id;
    QString reserve;
    for (const QString& child : children) {
      const QString leaf = subgraphIds.contains(child) ? firstLeaf(child) : child;
      if (leaf.isEmpty())
        continue;
      if (mirrorsEdge(id, leaf))
        reserve = leaf;
      else
        return leaf;
    }
    return reserve;
  }

  // A node is in a cluster a relationship does not leave: one that would be
  // laid out on its own, taking the node out of this graph.
  bool inExtractableCluster(const QString& node, const QString& rootId) const {
    QString parent = parentOf(node);
    while (!parent.isEmpty() && parent != rootId) {
      const auto it = clusters.constFind(parent);
      if (it != clusters.constEnd() && !it->external)
        return true;
      parent = parentOf(parent);
    }
    return false;
  }

  // A node of the cluster that stays in this graph: one outside the clusters
  // a relationship does not leave.
  QString safeAnchor(const QString& clusterId, const QString& excluded) const {
    const QSet<QString> belowExcluded = descendantsOf(excluded);
    for (const QString& child : childrenOf(clusterId)) {
      if (child == excluded || belowExcluded.contains(child))
        continue;
      const QString candidate = firstLeaf(child);
      if (candidate.isEmpty() || inExtractableCluster(candidate, clusterId))
        continue;
      return candidate;
    }
    return {};
  }

  // getAnchorId(): a cluster a relationship leaves is met at its anchor; a
  // closed one keeps its own id, which becomes the node it is laid out as.
  QString anchorId(const QString& id) const {
    const auto it = clusters.constFind(id);
    return it != clusters.constEnd() && it->external ? it->anchor : id;
  }

  // The box a path is cut at: Dagre's box of the cluster, widened for a title
  // wider than it (a cluster laid out on its own keeps its own size).
  QRectF clusterBox(const QString& id) const {
    const d::DagreNodeLabel* node = g.node(id);
    if (!node)
      return {};
    qreal width = node->width;
    if (std::none_of(extracted.cbegin(), extracted.cend(),
                     [&](const ExtractedCluster& item) { return item.id == id; })) {
      const QSizeF titleSize = options.measuredClusterLabels.value(id);
      if (titleSize.isValid() && !titleSize.isEmpty())
        width = std::max(width, titleSize.width() + options.clusterPadding);
    }
    return QRectF(node->x.value_or(0.0) - width / 2.0, node->y.value_or(0.0) - node->height / 2.0, width, node->height);
  }

  // adjustClustersAndEdges(): a cluster is noted with the node a relationship
  // to it is anchored on and with whether a relationship leaves it. Then each
  // relationship to a cluster a relationship leaves is moved to that node,
  // and remembers the cluster so that its path is cut at the cluster's box.
  void resolveClusterEdges() {
    for (const FlowSubgraph& sg : data.subgraphs)
      if (!childrenOf(sg.id).isEmpty())
        clusters.insert(sg.id, {firstLeaf(sg.id), false});
    for (const FlowSubgraph& sg : data.subgraphs) {
      const auto it = clusters.find(sg.id);
      if (it == clusters.end())
        continue;
      const QSet<QString> below = descendantsOf(sg.id);
      for (const FlowEdge& edge : data.edges)
        if (below.contains(edge.start) != below.contains(edge.end)) {
          it->external = true;
          break;
        }
    }
    for (const FlowSubgraph& sg : data.subgraphs) {
      const auto it = clusters.find(sg.id);
      if (it == clusters.end())
        continue;
      const QString leaf = it->anchor;
      const QString parent = parentOf(leaf);
      if (parent != sg.id && clusters.contains(parent) && !clusters.value(parent).external)
        it->anchor = parent;
      const bool leavesDirectly = std::any_of(data.edges.cbegin(), data.edges.cend(),
                                              [&](const FlowEdge& edge) { return edge.start == sg.id; });
      if (!leaf.isEmpty() && it->external && leavesDirectly && inExtractableCluster(leaf, sg.id)) {
        const QString safe = safeAnchor(sg.id, parent);
        if (!safe.isEmpty())
          it->anchor = safe;
      }
    }
    fromCluster.resize(data.edges.size());
    toCluster.resize(data.edges.size());
    QVector<bool> namesCluster(data.edges.size(), false);
    for (qsizetype index = 0; index < data.edges.size(); ++index) {
      FlowEdge& edge = data.edges[index];
      const QString v = edge.start;
      const QString w = edge.end;
      if (!clusters.contains(v) && !clusters.contains(w))
        continue;
      namesCluster[index] = true;
      edge.start = anchorId(v);
      edge.end = anchorId(w);
      if (edge.start != v) {
        const QString parent = parentOf(edge.start);
        if (clusters.contains(parent))
          clusters[parent].external = true;
        fromCluster[index] = v;
      }
      if (edge.end != w) {
        const QString parent = parentOf(edge.end);
        if (clusters.contains(parent))
          clusters[parent].external = true;
        toCluster[index] = w;
      }
    }
    // A relationship that names a cluster is taken out of the graph and set
    // again, so the graph lists it after the others.
    QVector<FlowEdge> ordered;
    QVector<QString> from, to;
    ordered.reserve(data.edges.size());
    for (const bool named : {false, true})
      for (qsizetype index = 0; index < data.edges.size(); ++index)
        if (namesCluster.at(index) == named) {
          ordered.append(data.edges.at(index));
          from.append(fromCluster.at(index));
          to.append(toCluster.at(index));
        }
    data.edges = std::move(ordered);
    fromCluster = std::move(from);
    toCluster = std::move(to);
  }

  void extractClusters() {
    for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
      if (it->id == retainedRoot || claimedSubgraphs.contains(it->id) || it->nodes.isEmpty())
        continue;
      QSet<QString> members;
      // Everything below the cluster: the vertices of its own and of its
      // nested subgraphs, and those subgraphs. A relationship leaves the
      // cluster only when exactly one of its ends is down there, so one to
      // a subgraph of the cluster enters it.
      QSet<QString> descendants;
      for (const QString& id : it->nodes)
        if (!subgraphIds.contains(id))
          members.insert(id);
      for (const FlowSubgraph& nested : data.subgraphs)
        if (nested.id != it->id && isSubgraphDescendant(nested.id, it->id)) {
          descendants.insert(nested.id);
          for (const QString& id : nested.nodes)
            if (!subgraphIds.contains(id))
              members.insert(id);
        }
      if (members.isEmpty())
        continue;
      descendants.unite(members);
      bool externalConnection = false;
      for (const FlowEdge& edge : data.edges) {
        if (descendants.contains(edge.start) != descendants.contains(edge.end)) {
          externalConnection = true;
          break;
        }
      }
      // Mermaid recursively extracts explicit-direction clusters even when an
      // edge crosses their boundary. The parent graph rebinds that endpoint to
      // the extracted cluster atom; non-explicit clusters remain extractable only
      // when they are closed.
      if (externalConnection && !it->hasExplicitDir)
        continue;

      ExtractedCluster item;
      item.id = it->id;
      item.vertices = members;
      item.subgraphs.insert(it->id);
      for (const FlowSubgraph& nested : data.subgraphs)
        if (isSubgraphDescendant(nested.id, it->id))
          item.subgraphs.insert(nested.id);

      FlowchartData inner;
      const QString parentDirection =
          data.direction.toUpper().isEmpty() ? QStringLiteral("TB") : data.direction.toUpper();
      inner.direction =
          !it->dir.isEmpty()
              ? it->dir
              : (!retainedRoot.isEmpty()
                     ? QStringLiteral("TB")
                     : (parentDirection == QLatin1String("TB") ? QStringLiteral("LR") : QStringLiteral("TB")));
      inner.title = data.title;
      inner.accTitle = data.accTitle;
      inner.accDescription = data.accDescription;
      inner.classes = data.classes;
      inner.tooltips = data.tooltips;
      inner.defaultEdgeStyles = data.defaultEdgeStyles;
      inner.defaultEdgeInterpolate = data.defaultEdgeInterpolate;
      for (const FlowVertex& vertex : data.vertices)
        if (item.vertices.contains(vertex.id))
          inner.vertices.push_back(vertex);
      // copy(): a relationship goes with the first of its ends that is
      // taken, the ends in order below the cluster.
      QStringList order;
      collectClusterOrder(it->id, order);
      const auto inside = [&](const FlowEdge& edge) {
        return descendants.contains(edge.start) && descendants.contains(edge.end);
      };
      QVector<bool> taken(data.edges.size(), false);
      for (const QString& node : order)
        for (qsizetype index = 0; index < data.edges.size(); ++index) {
          const FlowEdge& edge = data.edges.at(index);
          if (!taken.at(index) && inside(edge) && (edge.start == node || edge.end == node)) {
            taken[index] = true;
            inner.edges.push_back(edge);
          }
        }
      for (qsizetype index = 0; index < data.edges.size(); ++index)
        if (!taken.at(index) && inside(data.edges.at(index)))
          inner.edges.push_back(data.edges.at(index));
      for (const FlowSubgraph& subgraph : data.subgraphs)
        if (item.subgraphs.contains(subgraph.id))
          inner.subgraphs.push_back(subgraph);

      FlowLayoutOptions innerOptions = options;
      innerOptions.rankSpacing += 25.0;
      if (options.preserveRecursiveSvgFrame) {
        innerOptions.preserveDagreCoordinates = true;
        innerOptions.diagramPadding = 8.0;
      }
      item.layout = layoutFlowchartNodesDagreScope(inner, measuredNodes, innerOptions, nullptr, item.id);
      const auto root = std::find_if(item.layout.clusters.cbegin(), item.layout.clusters.cend(),
                                     [&](const FlowLayoutCluster& cluster) { return cluster.id == item.id; });
      if (root == item.layout.clusters.cend())
        continue;
      item.center = QPointF(root->x, root->y);
      item.size = QSizeF(root->width, root->height);
      item.dagreWidth = root->dagreWidth;
      item.boxWidth = root->logicalWidth > 0.0 ? root->logicalWidth : root->width;
      claimedVertices.unite(item.vertices);
      claimedSubgraphs.unite(item.subgraphs);
      extracted.push_back(std::move(item));
    }
  }

  void configureGraph() {
    d::DagreGraphLabel gl;
    dir = data.direction.toUpper();
    if (dir.isEmpty())
      dir = QStringLiteral("TB");
    gl.rankdir = dir;
    gl.nodesep = options.nodeSpacing;
    gl.edgesep = options.edgeSpacing;
    gl.ranksep = options.rankSpacing;
    gl.nodePadding = options.nodePadding;
    // The shared dagre-wrapper render() hardcodes marginx/marginy = 8,
    // independent of diagramPadding — translateGraph anchors the node bbox at
    // (8, 8) and setupGraphViewbox's viewBox = inkBBox ± padding carries
    // origin = 8 - padding. Flowchart/Swimlane set this explicitly; the other
    // families sharing this pipeline keep their own (fixture-locked) margin
    // semantics via diagramPadding.
    gl.marginx = options.dagreWrapperMargin >= 0.0 ? options.dagreWrapperMargin : options.diagramPadding;
    gl.marginy = options.dagreWrapperMargin >= 0.0 ? options.dagreWrapperMargin : options.diagramPadding;
    g.setGraph(gl);
  }

  void insertNodes() {
    // flowDb.getData() emits cluster nodes first, in reverse declaration order,
    // followed by vertices in insertion order. Dagre uses graph insertion order
    // to break mirror-equivalent ordering ties, so this order is observable.
    QHash<QString, QSizeF> extractedSizes;
    for (const ExtractedCluster& item : extracted)
      extractedSizes.insert(item.id, item.size);
    for (const ExtractedCluster& item : extracted) {
      for (const QString& vertex : item.vertices)
        extractedEndpoint.insert(vertex, item.id);
      // The subgraphs nested in it left the graph with it: a relationship to
      // one of them is a relationship inside the cluster.
      for (const QString& subgraph : item.subgraphs)
        if (subgraph != item.id)
          extractedEndpoint.insert(subgraph, item.id);
    }
    auto addSubgraphNode = [&](const FlowSubgraph& subgraph) {
      if (claimedSubgraphs.contains(subgraph.id) && !extractedSizes.contains(subgraph.id))
        return;
      d::DagreNodeLabel label;
      if (extractedSizes.contains(subgraph.id)) {
        label.width = extractedSizes.value(subgraph.id).width();
        label.height = extractedSizes.value(subgraph.id).height();
      }
      g.setNode(subgraph.id, label);
    };
    auto addVertexNode = [&](const FlowVertex& v) {
      if (claimedVertices.contains(v.id))
        return;
      const QSizeF sz = measuredNodes.value(v.id);
      d::DagreNodeLabel nl;
      nl.width = sz.width();
      nl.height = sz.height();
      // Mermaid expands forkJoin by state.padding / 2 after measuring its bar.
      if (options.look == FlowLook::Classic && canonicalShape(v.type) == QLatin1String("fork")) {
        nl.width += 4.0;
        nl.height += 4.0;
      }
      g.setNode(v.id, nl);
      if (!retainedRoot.isEmpty()) {
        const bool hasSelfLoop = std::any_of(data.edges.cbegin(), data.edges.cend(), [&](const FlowEdge& edge) {
          return edge.start == v.id && edge.end == v.id;
        });
        if (hasSelfLoop) {
          d::DagreNodeLabel dummy;
          dummy.width = kSelfLoopLabelRectExtent;
          dummy.height = kSelfLoopLabelRectExtent;
          g.setNode(v.id + QStringLiteral("---") + v.id + QStringLiteral("---1"), dummy);
          g.setNode(v.id + QStringLiteral("---") + v.id + QStringLiteral("---2"), dummy);
        }
      }
    };
    if (options.nodeInsertionOrder.isEmpty()) {
      if (!retainedRoot.isEmpty()) {
        const auto root = std::find_if(data.subgraphs.cbegin(), data.subgraphs.cend(),
                                       [&](const FlowSubgraph& subgraph) { return subgraph.id == retainedRoot; });
        if (root != data.subgraphs.cend())
          addSubgraphNode(*root);
      }
      for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
        if (!retainedRoot.isEmpty() && it->id == retainedRoot)
          continue;
        addSubgraphNode(*it);
      }
      for (const FlowVertex& vertex : data.vertices)
        addVertexNode(vertex);
    } else {
      QSet<QString> insertedNodes;
      const auto addSubgraphOnce = [&](const FlowSubgraph& subgraph) {
        if (insertedNodes.contains(subgraph.id))
          return;
        addSubgraphNode(subgraph);
        insertedNodes.insert(subgraph.id);
      };
      const auto addVertexOnce = [&](const FlowVertex& vertex) {
        if (insertedNodes.contains(vertex.id))
          return;
        addVertexNode(vertex);
        insertedNodes.insert(vertex.id);
      };
      for (const QString& id : options.nodeInsertionOrder) {
        const auto subgraph = std::find_if(data.subgraphs.cbegin(), data.subgraphs.cend(),
                                           [&](const FlowSubgraph& candidate) { return candidate.id == id; });
        if (subgraph != data.subgraphs.cend()) {
          addSubgraphOnce(*subgraph);
          continue;
        }
        const auto vertex = std::find_if(data.vertices.cbegin(), data.vertices.cend(),
                                         [&](const FlowVertex& candidate) { return candidate.id == id; });
        if (vertex != data.vertices.cend())
          addVertexOnce(*vertex);
      }
      if (!retainedRoot.isEmpty()) {
        const auto root = std::find_if(data.subgraphs.cbegin(), data.subgraphs.cend(),
                                       [&](const FlowSubgraph& subgraph) { return subgraph.id == retainedRoot; });
        if (root != data.subgraphs.cend())
          addSubgraphOnce(*root);
      }
      for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
        if (!retainedRoot.isEmpty() && it->id == retainedRoot)
          continue;
        addSubgraphOnce(*it);
      }
      for (const FlowVertex& vertex : data.vertices)
        addVertexOnce(vertex);
    }
  }

  void assignParents() {
    // The parser lists a node in every subgraph scope that references it (including
    // edge endpoints), so a node can appear in several subgraph node lists. dagre
    // wants each node parented to its DIRECT (innermost) subgraph. Reconstruct that
    // by parenting each node to its deepest containing subgraph, and nesting
    // subgraphs under the subgraph that lists them.
    auto subgraphDepth = [&](const QString& id) {
      int depth = 1;
      QString p = subgraphParent.value(id);
      QSet<QString> guard;
      while (!p.isNull() && !guard.contains(p)) {
        guard.insert(p);
        ++depth;
        p = subgraphParent.value(p);
      }
      return depth;
    };
    for (const FlowSubgraph& sg : data.subgraphs)
      if (!claimedSubgraphs.contains(sg.id) && subgraphParent.contains(sg.id) &&
          !claimedSubgraphs.contains(subgraphParent.value(sg.id)))
        g.setParent(sg.id, subgraphParent.value(sg.id));
    for (const FlowSubgraph& sg : data.subgraphs) {
      if (claimedSubgraphs.contains(sg.id))
        continue;
      for (const QString& child : sg.nodes) {
        if (claimedVertices.contains(child) || claimedSubgraphs.contains(child))
          continue;
        if (subgraphIds.contains(child))
          continue; // nested subgraph, handled above
        QString best = sg.id;
        int bestDepth = subgraphDepth(sg.id);
        for (const FlowSubgraph& sg2 : data.subgraphs)
          if (sg2.nodes.contains(child)) {
            const int d2 = subgraphDepth(sg2.id);
            if (d2 > bestDepth) {
              best = sg2.id;
              bestDepth = d2;
            }
          }
        g.setParent(child, best);
      }
    }
    for (const ExtractedCluster& item : extracted) {
      const QString parent = subgraphParent.value(item.id);
      if (!parent.isEmpty() && g.hasNode(parent))
        g.setParent(item.id, parent);
    }
  }

  void insertEdges() {
    for (const FlowEdge& fe : data.edges) {
      const QString start = layoutEndpoint(fe.start);
      const QString end = layoutEndpoint(fe.end);
      if (start == end && (start != fe.start || end != fe.end))
        continue;
      d::DagreEdgeLabel el = edgeLabel(fe);
      if (start == end) {
        // The flowchart renderer expands a self-loop into a three-edge cycle
        // through two 10x10 layout nodes. Dagre's native self-edge mechanism has
        // different compound bounds and is not used by Mermaid flowcharts.
        const QString dummy1 = start + QStringLiteral("---") + start + QStringLiteral("---1");
        const QString dummy2 = start + QStringLiteral("---") + start + QStringLiteral("---2");
        // The placeholders are declared as 10x10 in Mermaid data, then Chrome's
        // insertNode(labelRect) resolves their actual pre-Dagre SVG bbox to 0.1px.
        d::DagreNodeLabel dummy;
        dummy.width = kSelfLoopLabelRectExtent;
        dummy.height = kSelfLoopLabelRectExtent;
        g.setNode(dummy1, dummy);
        g.setNode(dummy2, dummy);
        const QString parent = g.parentOf(start);
        if (!parent.isNull()) {
          g.setParent(dummy1, parent);
          g.setParent(dummy2, parent);
        }
        d::DagreEdgeLabel empty = el;
        empty.width = 0.0;
        empty.height = 0.0;
        g.setEdge(start, dummy1, empty, start + QStringLiteral("-cyclic-special-0"));
        g.setEdge(dummy1, dummy2, el, start + QStringLiteral("-cyclic-special-1"));
        g.setEdge(dummy2, start, empty, start + QStringLiteral("-cyclic-special-2"));
        continue;
      }
      if (fe.id.isEmpty())
        g.setEdge(start, end, el);
      else
        g.setEdge(start, end, el, fe.id);
    }
  }

  void resolveOrigin() {
    if (!options.preserveDagreCoordinates) {
      for (const FlowVertex& vertex : data.vertices)
        if (const d::DagreNodeLabel* n = g.node(vertex.id)) {
          origin = QPointF(n->x.value_or(0.0), n->y.value_or(0.0));
          break;
        }
    }
  }

  void collectNodes(FlowLayoutResult& result) const {
    for (const FlowVertex& v : data.vertices) {
      if (claimedVertices.contains(v.id))
        continue;
      const d::DagreNodeLabel* n = g.node(v.id);
      FlowLayoutNode node;
      node.id = v.id;
      node.x = (n ? n->x.value_or(0.0) : 0.0) - origin.x();
      node.y = (n ? n->y.value_or(0.0) : 0.0) - origin.y();
      node.width = n ? n->width : 0.0;
      node.height = n ? n->height : 0.0;
      if (options.look == FlowLook::Classic && canonicalShape(v.type) == QLatin1String("fork")) {
        const QSizeF rendered = measuredNodes.value(v.id);
        node.renderWidth = rendered.width();
        node.renderHeight = rendered.height();
      }
      node.rank = (n && n->rank.has_value()) ? *n->rank : 0;
      result.nodes.push_back(node);
    }
  }

  bool collectSelfLoop(const FlowEdge& fe, const QString& start, FlowLayoutEdge& out) const {
    QHash<QString, QString> vertexType;
    for (const FlowVertex& v : data.vertices)
      vertexType.insert(v.id, v.type);

    const d::DagreNodeLabel* node = g.node(start);
    const d::DagreNodeLabel* dummy1 = g.node(start + QStringLiteral("---") + start + QStringLiteral("---1"));
    const d::DagreNodeLabel* dummy2 = g.node(start + QStringLiteral("---") + start + QStringLiteral("---2"));
    if (node && dummy1 && dummy2) {
      const auto segmentPoints = [&](const QString& tailId, const QString& headId, const QString& edgeName,
                                     const QString& tailType, const QString& headType) {
        QVector<QPointF> points;
        const d::DagreEdgeLabel* segment = g.edge(tailId, headId, edgeName);
        if (!segment)
          return points;
        points = segment->points;
        const d::DagreNodeLabel* tail = g.node(tailId);
        const d::DagreNodeLabel* head = g.node(headId);
        if (points.size() >= 2) {
          points.first() =
              tail ? intersectNodeForShape(*tail, tailType, points.value(1), options.look) : points.first();
          points.last() = head ? intersectNodeForShape(*head, headType, points.value(points.size() - 2), options.look)
                               : points.last();
        }
        for (QPointF& point : points)
          point -= origin;
        return points;
      };
      const QString dummy1Id = start + QStringLiteral("---") + start + QStringLiteral("---1");
      const QString dummy2Id = start + QStringLiteral("---") + start + QStringLiteral("---2");
      out.loopMarks = {QPointF(dummy1->x.value_or(0.0), dummy1->y.value_or(0.0)) - origin,
                       QPointF(dummy2->x.value_or(0.0), dummy2->y.value_or(0.0)) - origin};
      out.segments = {
          segmentPoints(start, dummy1Id, start + QStringLiteral("-cyclic-special-0"), vertexType.value(start),
                        QStringLiteral("rect")),
          segmentPoints(dummy1Id, dummy2Id, start + QStringLiteral("-cyclic-special-1"), QStringLiteral("rect"),
                        QStringLiteral("rect")),
          segmentPoints(dummy2Id, start, start + QStringLiteral("-cyclic-special-2"), QStringLiteral("rect"),
                        vertexType.value(start)),
      };
      const QPointF center(node->x.value_or(0.0), node->y.value_or(0.0));
      const QPointF hint((dummy1->x.value_or(0.0) + dummy2->x.value_or(0.0)) / 2.0,
                         (dummy1->y.value_or(0.0) + dummy2->y.value_or(0.0)) / 2.0);
      const QPointF delta = hint - center;
      enum class LoopSide { Top, Right, Bottom, Left };
      LoopSide side;
      if (std::abs(delta.x()) > std::abs(delta.y()))
        side = delta.x() > 0.0 ? LoopSide::Right : LoopSide::Left;
      else if (std::abs(delta.y()) > 0.0)
        side = delta.y() > 0.0 ? LoopSide::Bottom : LoopSide::Top;
      else if (dir == QLatin1String("BT"))
        side = LoopSide::Bottom;
      else if (dir == QLatin1String("LR"))
        side = LoopSide::Right;
      else if (dir == QLatin1String("RL"))
        side = LoopSide::Left;
      else
        side = LoopSide::Top;

      const d::DagreEdgeLabel selfLabel = edgeLabel(fe);
      const qreal labelWidth = selfLabel.width;
      const qreal labelHeight = selfLabel.height;
      const qreal maxSpan = std::max<qreal>(36.0, std::min<qreal>(100.0, node->width * 0.8));
      const qreal span = std::clamp(std::max(labelWidth, node->width * 0.35), 36.0, maxSpan);
      const qreal depth = std::clamp(std::min(node->width, node->height) * 0.45, 24.0, 48.0);
      const qreal x = center.x(), y = center.y();
      if (side == LoopSide::Bottom) {
        const qreal edge = y + node->height / 2.0;
        out.points = {{x - span / 2.0, edge},
                      {x - span / 2.0, edge + depth},
                      {x + span / 2.0, edge + depth},
                      {x + span / 2.0, edge}};
        out.labelX = x;
        out.labelY = edge + depth + labelHeight / 2.0 + 4.0;
      } else if (side == LoopSide::Right) {
        const qreal edge = x + node->width / 2.0;
        out.points = {{edge, y - span / 2.0},
                      {edge + depth, y - span / 2.0},
                      {edge + depth, y + span / 2.0},
                      {edge, y + span / 2.0}};
        out.labelX = edge + depth + labelWidth / 2.0 + 4.0;
        out.labelY = y;
      } else if (side == LoopSide::Left) {
        const qreal edge = x - node->width / 2.0;
        out.points = {{edge, y - span / 2.0},
                      {edge - depth, y - span / 2.0},
                      {edge - depth, y + span / 2.0},
                      {edge, y + span / 2.0}};
        out.labelX = edge - depth - labelWidth / 2.0 - 4.0;
        out.labelY = y;
      } else {
        const qreal edge = y - node->height / 2.0;
        out.points = {{x - span / 2.0, edge},
                      {x - span / 2.0, edge - depth},
                      {x + span / 2.0, edge - depth},
                      {x + span / 2.0, edge}};
        out.labelX = x;
        out.labelY = edge - depth - labelHeight / 2.0 - 4.0;
      }
      out.points.first() = intersectNodeForShape(*node, vertexType.value(start), out.points.at(1), options.look);
      out.points.last() =
          intersectNodeForShape(*node, vertexType.value(start), out.points.at(out.points.size() - 2), options.look);
      out.hasLabelPosition = !fe.text.isEmpty();
      // A family that draws a loop as three paths of its own (class) gives its
      // label the place of the middle one, as Dagre laid it out.
      const d::DagreEdgeLabel* middle = g.edge(dummy1Id, dummy2Id, start + QStringLiteral("-cyclic-special-1"));
      if (out.hasLabelPosition && options.selfLoopLabelOnMiddlePath && middle && middle->x && middle->y) {
        out.labelX = *middle->x;
        out.labelY = *middle->y;
      }
      for (QPointF& point : out.points)
        point -= origin;
      if (out.hasLabelPosition) {
        out.labelX -= origin.x();
        out.labelY -= origin.y();
      }
      QVector<QPointF> rendered = out.points;
      clipFlowEdgeForMarkers(rendered, fe.type);
      const QString curve =
          !fe.interpolate.isEmpty()
              ? fe.interpolate
              : (!data.defaultEdgeInterpolate.isEmpty() ? data.defaultEdgeInterpolate : options.curve);
      out.path = d3curve::pathForCurve(rendered, curve);
      return true;
    }

    return false;
  }

  void collectEdges(FlowLayoutResult& result) const {
    // Edge endpoints are re-intersected against each node's shape (mermaid's
    // tail.intersect / head.intersect), so we need each vertex's shape type.
    QHash<QString, QString> vertexType;
    for (const FlowVertex& v : data.vertices)
      vertexType.insert(v.id, v.type);

    // Mermaid names a loop's three edges after its node alone, so each further
    // edge from a node to itself replaces the previous one in its graph: of
    // several, only the last is laid out and drawn.
    QHash<QString, qsizetype> lastSelfEdge;
    for (qsizetype index = 0; index < data.edges.size(); ++index)
      if (data.edges.at(index).start == data.edges.at(index).end)
        lastSelfEdge.insert(data.edges.at(index).start, index);

    for (qsizetype index = 0; index < data.edges.size(); ++index) {
      const FlowEdge& fe = data.edges.at(index);
      const QString start = layoutEndpoint(fe.start);
      const QString end = layoutEndpoint(fe.end);
      if (start == end && (start != fe.start || end != fe.end))
        continue;
      if (fe.start == fe.end && lastSelfEdge.value(fe.start) != index)
        continue;
      FlowLayoutEdge out;
      out.id = fe.id;
      if (!fe.text.isEmpty()) {
        const FlowEdgeLabelLayout prepared = preparedEdgeLabel(fe);
        out.labelSize = prepared.size;
        out.labelDocument = prepared.document;
      }
      if (fe.start == fe.end && collectSelfLoop(fe, start, out)) {
        result.edges.push_back(std::move(out));
        continue;
      }
      const d::DagreEdgeLabel* el = fe.id.isEmpty() ? g.edge(start, end) : g.edge(start, end, fe.id);
      if (el) {
        // mermaid's renderer drops dagre's rect-intersected endpoints and
        // re-intersects the first/last interior point against each node's shape:
        //   points = edge.points.slice(1, len-1)
        //   points.unshift(tail.intersect(points[0]))   // points[0] == edge.points[1]
        //   points.push(head.intersect(points[last]))    // points[last] == edge.points[len-2]
        // For rect/round shapes this reproduces dagre's rect intersect exactly;
        // for circle/diamond it differs on diagonal approaches.
        QVector<QPointF> absPts = el->points;
        const d::DagreNodeLabel* tailNode = g.node(start);
        const d::DagreNodeLabel* headNode = g.node(end);
        if (absPts.size() >= 3) {
          const QPointF firstInterior = absPts.at(1);
          const QPointF lastInterior = absPts.at(absPts.size() - 2);
          QVector<QPointF> reintersected;
          reintersected.reserve(absPts.size());
          reintersected.append(
              tailNode ? intersectNodeForShape(*tailNode, vertexType.value(start), firstInterior, options.look)
                       : absPts.first());
          for (qsizetype i = 1; i + 1 < absPts.size(); ++i)
            reintersected.append(absPts.at(i));
          reintersected.append(headNode
                                   ? intersectNodeForShape(*headNode, vertexType.value(end), lastInterior, options.look)
                                   : absPts.last());
          absPts = reintersected;
        } else if (absPts.size() == 2) {
          // mermaid re-intersects BOTH endpoints of a 2-point edge too:
          //   points = [tail.intersect(points[0]), head.intersect(points[1])]
          // (flowDiagram chunk). For rect/round shapes this is a no-op (the point
          // is already on the rect boundary); for polygon/circle shapes it snaps
          // the endpoint onto the shape's actual border.
          absPts = {
              tailNode ? intersectNodeForShape(*tailNode, vertexType.value(start), absPts.at(0), options.look)
                       : absPts.at(0),
              headNode ? intersectNodeForShape(*headNode, vertexType.value(end), absPts.at(1), options.look)
                       : absPts.at(1),
          };
        }
        // insertEdge(): a relationship that enters or leaves a cluster ends
        // where it crosses the cluster's box. One that enters is cut in
        // Dagre's own points, one that leaves in the points so far.
        QVector<QPointF> drawn = absPts;
        if (index < toCluster.size() && !toCluster.at(index).isEmpty()) {
          drawn = cutPathAtIntersect(el->points, clusterBox(toCluster.at(index)));
          out.cutAtCluster = true;
        }
        if (index < fromCluster.size() && !fromCluster.at(index).isEmpty()) {
          std::reverse(drawn.begin(), drawn.end());
          drawn = cutPathAtIntersect(drawn, clusterBox(fromCluster.at(index)));
          std::reverse(drawn.begin(), drawn.end());
          out.cutAtCluster = true;
        }
        for (const QPointF& p : drawn)
          out.points.push_back(p - origin);
        out.reversedForLayout = el->reversed;
        if (el->x.has_value()) {
          out.hasLabelPosition = true;
          out.labelX = *el->x - origin.x();
          out.labelY = *el->y - origin.y();
        }
      }
      QVector<QPointF> rendered = out.points;
      clipFlowEdgeForMarkers(rendered, fe.type);
      const QString curve =
          !fe.interpolate.isEmpty()
              ? fe.interpolate
              : (!data.defaultEdgeInterpolate.isEmpty() ? data.defaultEdgeInterpolate : options.curve);
      out.path = d3curve::pathForCurve(rendered, curve);
      result.edges.push_back(out);
    }

    QSet<QString> selfEdgeIds;
    for (const FlowEdge& edge : data.edges)
      if (edge.start == edge.end)
        selfEdgeIds.insert(edge.id);
    std::stable_partition(result.edges.begin(), result.edges.end(),
                          [&](const FlowLayoutEdge& edge) { return !selfEdgeIds.contains(edge.id); });
  }

  void collectClusters(FlowLayoutResult& result) const {
    // Clusters: reverse subgraph order, matching the existing extraction.
    for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
      if (claimedSubgraphs.contains(it->id))
        continue;
      const d::DagreNodeLabel* n = g.node(it->id);
      if (!n || n->width <= 0.0)
        continue;
      FlowLayoutCluster c;
      c.id = it->id;
      c.x = n->x.value_or(0.0) - origin.x();
      c.y = n->y.value_or(0.0) - origin.y();
      c.width = n->width;
      c.height = n->height;
      c.dagreWidth = c.width;
      const QSizeF titleSize = options.measuredClusterLabels.value(it->id);
      if (titleSize.isValid() && !titleSize.isEmpty())
        c.width = std::max(c.width, titleSize.width() + options.clusterPadding);
      if (options.clusterSizeTransform) {
        const QSizeF logicalSize(c.width, c.height);
        const QRectF logicalBounds(QPointF(c.x - c.width / 2.0, c.y - c.height / 2.0), logicalSize);
        const QSizeF rendered = options.clusterSizeTransform(c.id, logicalBounds);
        c.logicalWidth = logicalSize.width();
        c.logicalHeight = logicalSize.height();
        c.width = rendered.width();
        c.height = rendered.height();
      }
      result.clusters.push_back(c);
    }
  }

  void restoreClusters(FlowLayoutResult& result) const {
    for (const ExtractedCluster& item : extracted) {
      const d::DagreNodeLabel* atom = g.node(item.id);
      if (!atom)
        continue;
      const QPointF atomCenter(atom->x.value_or(0.0) - origin.x(), atom->y.value_or(0.0) - origin.y());
      // insertCluster() leaves node.diff for positionNode(): a cluster whose
      // Dagre box holds its title keeps that box and gets -padding; a narrower
      // one is widened to title + padding, centred where Dagre put its box,
      // and gets half the widening less the padding.
      const qreal padding = options.clusterPadding;
      const qreal widening = item.boxWidth - item.dagreWidth;
      const qreal diff = widening > 0.0 ? widening / 2.0 - padding : -padding;
      // positionNode() centres a rendered cluster's child <g> on the atom,
      // shifted by that diff. The child's box starts at Dagre's 8px margin,
      // which is what remains of the shift once the box is centred. A scope
      // that keeps Dagre's frame holds the box at that margin itself.
      const QPointF delta =
          options.preserveRecursiveSvgFrame
              ? QPointF(atomCenter.x() + diff - item.size.width() / 2.0,
                        atomCenter.y() - 8.0 - item.size.height() / 2.0)
              : QPointF(atomCenter.x() - item.center.x() + 8.0 - padding, atomCenter.y() - item.center.y());
      for (FlowLayoutNode node : item.layout.nodes) {
        node.x += delta.x();
        node.y += delta.y();
        result.nodes.push_back(std::move(node));
      }
      for (FlowLayoutEdge edge : item.layout.edges) {
        for (QPointF& point : edge.points)
          point += delta;
        for (QVector<QPointF>& segment : edge.segments)
          for (QPointF& point : segment)
            point += delta;
        if (edge.hasLabelPosition) {
          edge.labelX += delta.x();
          edge.labelY += delta.y();
        }
        result.edges.push_back(std::move(edge));
      }
      for (FlowLayoutCluster cluster : item.layout.clusters) {
        cluster.x += delta.x();
        cluster.y += delta.y();
        result.clusters.push_back(std::move(cluster));
      }
    }
  }

  FlowLayoutResult orderResult(FlowLayoutResult result) const {
    QSet<QString> semanticVertexIds;
    for (const FlowVertex& vertex : data.vertices)
      semanticVertexIds.insert(vertex.id);
    QStringList layoutNodeOrder;
    for (const QString& id : g.nodes()) {
      const auto extractedIt = std::find_if(extracted.cbegin(), extracted.cend(),
                                            [&](const ExtractedCluster& item) { return item.id == id; });
      if (extractedIt != extracted.cend()) {
        for (const FlowLayoutNode& node : extractedIt->layout.nodes)
          layoutNodeOrder.push_back(node.id);
      } else if (semanticVertexIds.contains(id) && !claimedVertices.contains(id)) {
        layoutNodeOrder.push_back(id);
      }
    }

    QHash<QString, FlowLayoutNode> nodesById;
    for (FlowLayoutNode& node : result.nodes)
      nodesById.insert(node.id, std::move(node));
    QHash<QString, FlowLayoutEdge> edgesById;
    QStringList layoutEdgeOrder;
    layoutEdgeOrder.reserve(result.edges.size());
    for (const FlowLayoutEdge& edge : result.edges)
      layoutEdgeOrder.push_back(edge.id);
    for (FlowLayoutEdge& edge : result.edges)
      edgesById.insert(edge.id, std::move(edge));
    QHash<QString, FlowLayoutCluster> clustersById;
    for (FlowLayoutCluster& cluster : result.clusters)
      clustersById.insert(cluster.id, std::move(cluster));

    FlowLayoutResult ordered;
    for (const QString& nodeId : layoutNodeOrder)
      if (nodesById.contains(nodeId))
        ordered.nodes.push_back(nodesById.take(nodeId));
    for (const QString& edgeId : layoutEdgeOrder)
      if (edgesById.contains(edgeId))
        ordered.edges.push_back(edgesById.take(edgeId));
    for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it)
      if (clustersById.contains(it->id))
        ordered.clusters.push_back(clustersById.take(it->id));

    return ordered;
  }

  void adjustOriginAndTitles(FlowLayoutResult& ordered) const {
    QPointF finalOrigin;
    if (!options.preserveDagreCoordinates && !ordered.nodes.isEmpty())
      finalOrigin = QPointF(ordered.nodes.first().x, ordered.nodes.first().y);
    if (!finalOrigin.isNull()) {
      for (FlowLayoutNode& node : ordered.nodes) {
        node.x -= finalOrigin.x();
        node.y -= finalOrigin.y();
      }
      for (FlowLayoutEdge& edge : ordered.edges) {
        for (QPointF& point : edge.points)
          point -= finalOrigin;
        for (QVector<QPointF>& segment : edge.segments)
          for (QPointF& point : segment)
            point -= finalOrigin;
        if (edge.hasLabelPosition) {
          edge.labelX -= finalOrigin.x();
          edge.labelY -= finalOrigin.y();
        }
      }
      for (FlowLayoutCluster& cluster : ordered.clusters) {
        cluster.x -= finalOrigin.x();
        cluster.y -= finalOrigin.y();
      }
    }
    const qreal subGraphTitleTotalMargin = options.subGraphTitleTopMargin + options.subGraphTitleBottomMargin;
    if (!qFuzzyIsNull(subGraphTitleTotalMargin)) {
      const qreal offset = subGraphTitleTotalMargin / 2.0;
      // Mermaid applies the title band after Dagre: every regular node and edge
      // moves down by half the total, while a pure cluster keeps its centre and
      // grows by the full total. This intentionally moves even nodes outside a
      // cluster; it is observable in setupGraphViewbox.
      for (FlowLayoutNode& node : ordered.nodes)
        node.y += offset;
      for (FlowLayoutEdge& edge : ordered.edges) {
        for (QPointF& point : edge.points)
          point.ry() += offset;
        for (QVector<QPointF>& segment : edge.segments)
          for (QPointF& point : segment)
            point.ry() += offset;
        if (edge.hasLabelPosition)
          edge.labelY += offset;
      }
      for (FlowLayoutCluster& cluster : ordered.clusters)
        cluster.height += subGraphTitleTotalMargin;
    }
    for (FlowLayoutCluster& cluster : ordered.clusters)
      cluster.titleTopMargin = options.subGraphTitleTopMargin;
  }

  void buildPaths(FlowLayoutResult& ordered) const {
    QHash<QString, const FlowEdge*> semanticEdges;
    for (const FlowEdge& edge : data.edges)
      semanticEdges.insert(edge.id, &edge);
    for (FlowLayoutEdge& edge : ordered.edges) {
      QVector<QPointF> rendered = edge.points;
      const FlowEdge* semantic = semanticEdges.value(edge.id);
      if (semantic)
        clipFlowEdgeForMarkers(rendered, semantic->type);
      const QString curve =
          semantic && !semantic->interpolate.isEmpty()
              ? semantic->interpolate
              : (!data.defaultEdgeInterpolate.isEmpty() ? data.defaultEdgeInterpolate : options.curve);
      edge.path = d3curve::pathForCurve(rendered, curve);
      if (edge.hasLabelPosition && options.repositionEdgeLabels) {
        const QPointF label =
            positionEdgeLabel(edge.points, edge.path, QPointF(edge.labelX, edge.labelY), edge.cutAtCluster);
        edge.labelX = label.x();
        edge.labelY = label.y();
      }
    }
  }
};

FlowLayoutResult layoutFlowchartNodesDagreScope(const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
                                                FlowLayoutOptions options, std::vector<dagre::DagreSnapshot>* snapshots,
                                                const QString& retainedRoot) {
  return DagreScopeLayout(data, measuredNodes, std::move(options), retainedRoot).run(snapshots);
}

} // namespace

FlowLayoutResult layoutFlowchartNodesDagre(const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
                                           FlowLayoutOptions options,
                                           std::vector<dagre::DagreSnapshot>* dagreSnapshots) {
  return layoutFlowchartNodesDagreScope(data, measuredNodes, std::move(options), dagreSnapshots, {});
}

QPointF positionEdgeLabel(const QVector<QPointF>& points, const QString& path, const QPointF& placed, bool updated) {
  if (points.isEmpty())
    return placed;
  // ECMAScript's Math.round(): half a unit rounds up, whichever the sign.
  const auto rounded = [](qreal value) { return QString::number(static_cast<qlonglong>(std::floor(value + 0.5))); };
  static const QRegularExpression decimal(QStringLiteral(R"((\d+\.\d+))"));
  QString data;
  qsizetype consumed = 0;
  auto matches = decimal.globalMatch(path);
  while (matches.hasNext()) {
    const auto match = matches.next();
    data += path.mid(consumed, match.capturedStart() - consumed);
    data += rounded(match.captured().toDouble());
    consumed = match.capturedEnd();
  }
  data += path.mid(consumed);
  const QPointF middle = points.at(points.size() / 2);
  if (!updated && (data.contains(rounded(middle.x())) || data.contains(rounded(middle.y()))))
    return placed;
  qreal remaining = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index)
    remaining +=
        std::hypot(points.at(index).x() - points.at(index - 1).x(), points.at(index).y() - points.at(index - 1).y());
  remaining /= 2.0;
  for (qsizetype index = 1; index < points.size(); ++index) {
    const QPointF delta = points.at(index) - points.at(index - 1);
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length == 0.0)
      return points.at(index - 1);
    if (length >= remaining)
      return points.at(index - 1) + delta * (remaining / length);
    remaining -= length;
  }
  return points.last();
}

} // namespace muffin::mermaid::flowchart
