#pragma once

// Native C++ port of dagre-d3-es graphlib's `Graph`.
//
// Upstream (compatibility baseline mermaid 11.16.0):
//   node_modules/dagre-d3-es/src/graphlib/graph.js
//
// This is the compound multigraph that the Dagre layout pipeline (layout.js,
// nesting-graph.js, parent-dummy-chains.js, add-border-segments.js, order/*,
// position/bk.js) operates on. Milestone B of the mermaid flowchart native port
// (see docs/mermaid-flowchart-remaining-plan.md) ports this data structure
// faithfully before the layout pipeline is switched over to it.
//
// Determinism contract: graphlib iterates its internal dicts with lodash
// `_.keys`/`_.values`, which yield STRING-KEY INSERTION ORDER. The layout
// output depends on that order, and Qt's QHash/QMap do not preserve it, so
// every map that graphlib iterates for ordering is wrapped in `OrderedMap`,
// which preserves insertion order and matches the JS delete-then-re-add
// semantics (a re-added key moves to the end).

#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::graphlib {
namespace detail {

// Reserved node/edge tokens, copied verbatim from graph.js.
inline const QString kGraphNode = QChar(u'\x0000');  // GRAPH_NODE: virtual root of compound forest
inline const QString kDefaultEdgeName = QChar(u'\x0000');  // sentinel for an unnamed multiedge
inline const QChar kEdgeKeyDelim = QChar(u'\x0001');  // EDGE_KEY_DELIM

// Insertion-order-preserving map. Mirrors the iteration behaviour of a plain
// JavaScript object under lodash `_.keys`/`_.values`: keys are returned in the
// order they were first inserted; removing a key and re-inserting it later
// appends it to the end. Removal is O(n) in the number of keys (graphlib's
// `_.keys` is O(n) as well), which is acceptable for layout-sized graphs.
template <typename K, typename V>
class OrderedMap {
public:
  bool contains(const K& key) const { return map_.contains(key); }

  const V* value(const K& key) const {
    const auto it = map_.constFind(key);
    return it == map_.cend() ? nullptr : &it.value();
  }

  V* value(const K& key) {
    const auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it.value();
  }

  // Returns a reference to the entry, default-constructing (and appending) if
  // absent. Equivalent to `map[k]` in JavaScript for objects.
  V& ref(const K& key) {
    if (!map_.contains(key)) order_.append(key);
    return map_[key];
  }

  // Insert-or-update. A new key is appended; an existing key keeps its slot.
  void set(const K& key, V val) {
    if (map_.contains(key)) {
      map_[key] = std::move(val);
    } else {
      order_.append(key);
      map_.insert(key, std::move(val));
    }
  }

  // Remove by linear scan of the order list — O(n), matching graphlib's
  // `delete obj[k]` + `_.keys` cost. No stored position hint to go stale.
  bool remove(const K& key) {
    if (!map_.contains(key)) return false;
    map_.remove(key);
    order_.removeOne(key);
    return true;
  }

  QList<K> keys() const { return order_; }

  QList<V> values() const {
    QList<V> result;
    result.reserve(order_.size());
    for (const K& key : order_) result.append(map_.value(key));
    return result;
  }

  qsizetype size() const { return order_.size(); }
  bool isEmpty() const { return order_.isEmpty(); }

private:
  QHash<K, V> map_;
  QList<K> order_;
};

}  // namespace detail

// An edge identity object. Mirrors graph.js `EdgeObj { v, w, name? }`. The
// `name` is only meaningful in a multigraph and is absent (`hasName == false`)
// for an unnamed edge.
struct Edge {
  QString v;
  QString w;
  QString name;
  bool hasName = false;

  bool operator==(const Edge&) const = default;
};

// Compound multigraph. Templated on the node/edge/graph label types because
// dagre stores heterogeneous label objects on nodes (rank/order/x/y/borderTop
// ...) and edges (minlen/weight/labelpos ...). Mirrors `Graph` in graph.js.
template <typename NodeLabel, typename EdgeLabel, typename GraphLabel = NodeLabel>
class Graph {
public:
  struct Options {
    bool directed = true;
    bool multigraph = false;
    bool compound = false;
  };

  using NodeFn = std::function<NodeLabel(const QString&)>;
  using EdgeFn = std::function<EdgeLabel(const QString&, const QString&, const QString&)>;

  Graph() = default;
  explicit Graph(Options opts) : isDirected_(opts.directed), isMultigraph_(opts.multigraph), isCompound_(opts.compound) {
    if (isCompound_) children_[detail::kGraphNode];  // root child set
  }

  // === Graph functions ===
  bool isDirected() const { return isDirected_; }
  bool isMultigraph() const { return isMultigraph_; }
  bool isCompound() const { return isCompound_; }

  void setGraph(GraphLabel label) { graphLabel_ = std::move(label); }
  const GraphLabel* graph() const { return graphLabel_ ? &*graphLabel_ : nullptr; }
  GraphLabel* graph() { return graphLabel_ ? &*graphLabel_ : nullptr; }

  void setDefaultNodeLabel(NodeFn fn) { defaultNode_ = std::move(fn); }
  void setDefaultNodeLabel(NodeLabel constant) {
    defaultNode_ = [c = std::move(constant)](const QString&) { return c; };
  }
  void setDefaultEdgeLabel(EdgeFn fn) { defaultEdge_ = std::move(fn); }
  void setDefaultEdgeLabel(EdgeLabel constant) {
    defaultEdge_ = [c = std::move(constant)](const QString&, const QString&, const QString&) { return c; };
  }

  qsizetype nodeCount() const { return nodeOrder_.size(); }
  qsizetype edgeCount() const { return edgeOrder_.size(); }

  // === Node functions ===
  QList<QString> nodes() const { return nodeOrder_; }

  QList<QString> sources() const {
    QList<QString> result;
    for (const QString& v : nodeOrder_)
      if (in_[v].isEmpty()) result.append(v);
    return result;
  }

  QList<QString> sinks() const {
    QList<QString> result;
    for (const QString& v : nodeOrder_)
      if (out_[v].isEmpty()) result.append(v);
    return result;
  }

  // setNode(v): create with default label if absent.
  void setNode(const QString& v) { ensureNode(v); }

  void setNode(const QString& v, NodeLabel value) {
    ensureNode(v);
    nodeLabel_[v] = std::move(value);
  }

  // nullptr if v is not in the graph (mirrors `node()` returning undefined).
  const NodeLabel* node(const QString& v) const {
    const auto it = nodeLabel_.constFind(v);
    return it == nodeLabel_.cend() ? nullptr : &it.value();
  }
  NodeLabel* node(const QString& v) {
    const auto it = nodeLabel_.find(v);
    return it == nodeLabel_.end() ? nullptr : &it.value();
  }

  bool hasNode(const QString& v) const { return nodeLabel_.contains(v); }

  void removeNode(const QString& v) {
    if (!nodeLabel_.contains(v)) return;
    // Collect incident edge ids first (graphlib iterates _.keys(this._in[v])).
    QList<QString> incident = in_[v].keys();
    for (const QString& e : out_[v].keys())
      if (!incident.contains(e)) incident.append(e);
    for (const QString& e : incident) removeEdgeObj(e);

    nodeLabel_.remove(v);
    nodeOrder_.removeOne(v);
    if (isCompound_) {
      removeFromParentsChildList(v);
      parent_.remove(v);
      // Re-parent direct children to the graph root.
      const QList<QString> kids = children_[v].keys();
      for (const QString& child : kids) setParent(child, detail::kGraphNode);
      children_.remove(v);
    }
    in_.remove(v);
    preds_.remove(v);
    out_.remove(v);
    sucs_.remove(v);
  }

  // === Compound functions ===
  void setParent(const QString& v, const QString& parent = detail::kGraphNode) {
    if (!isCompound_) throw std::runtime_error("Cannot set parent in a non-compound graph");
    QString resolved = parent;
    if (resolved != detail::kGraphNode) {
      // Cycle check: walk the ancestor chain of `parent` iteratively (graph.js
      // line 570-574). A deep chain must not overflow the stack.
      for (QString ancestor = resolved; !ancestor.isNull(); ancestor = parentOf(ancestor)) {
        if (ancestor == v)
          throw std::runtime_error("Setting " + resolved.toStdString() + " as parent of " +
                                   v.toStdString() + " would create a cycle");
      }
      ensureNode(resolved);
    }
    ensureNode(v);
    removeFromParentsChildList(v);
    parent_[v] = resolved;
    children_[resolved].set(v, true);
  }

  // nullptr means "no parent" (top-level). Mirrors `parent()` returning undefined
  // for the graph root or non-members.
  QString parentOf(const QString& v) const {
    if (!isCompound_) return {};
    const auto it = parent_.constFind(v);
    if (it == parent_.cend() || it.value() == detail::kGraphNode) return {};
    return it.value();
  }

  // children(): top-level nodes when v is omitted. Returns nullopt if v is not
  // in the graph (mirrors the JS `undefined`).
  std::optional<QList<QString>> children(const QString& v = detail::kGraphNode) const {
    if (isCompound_) {
      const auto it = children_.constFind(v);
      if (it == children_.cend()) return std::nullopt;
      return it.value().keys();
    }
    if (v == detail::kGraphNode) return nodes();
    if (hasNode(v)) return QList<QString>{};
    return std::nullopt;
  }

  QList<QString> predecessors(const QString& v) const {
    const auto it = preds_.constFind(v);
    return it == preds_.cend() ? QList<QString>{} : it.value().keys();
  }

  QList<QString> successors(const QString& v) const {
    const auto it = sucs_.constFind(v);
    return it == sucs_.cend() ? QList<QString>{} : it.value().keys();
  }

  QList<QString> neighbors(const QString& v) const {
    QList<QString> result = predecessors(v);
    for (const QString& s : successors(v))
      if (!result.contains(s)) result.append(s);
    return result;
  }

  bool isLeaf(const QString& v) const {
    return isDirected() ? successors(v).isEmpty() : neighbors(v).isEmpty();
  }

  // === Edge functions ===
  QList<Edge> edges() const {
    QList<Edge> result;
    result.reserve(edgeOrder_.size());
    for (const QString& e : edgeOrder_) result.append(edgeObjs_[e]);
    return result;
  }

  // setEdge with explicit name (multigraph). Throws if named edge used in a
  // non-multigraph.
  void setEdge(const QString& v, const QString& w, EdgeLabel value, const QString& name = {}) {
    setEdgeCore(v, w, std::move(value), name, /*hasName=*/!name.isNull(), /*valueSpecified=*/true);
  }

  // setEdge without a value (uses default edge label).
  void setEdge(const QString& v, const QString& w, const QString& name = {}) {
    setEdgeCore(v, w, EdgeLabel{}, name, /*hasName=*/!name.isNull(), /*valueSpecified=*/false);
  }

  // setEdge from an EdgeObj (name taken from edge.hasName).
  void setEdge(const Edge& edge, EdgeLabel value) {
    setEdgeCore(edge.v, edge.w, std::move(value), edge.name, edge.hasName, /*valueSpecified=*/true);
  }
  void setEdge(const Edge& edge) {
    setEdgeCore(edge.v, edge.w, EdgeLabel{}, edge.name, edge.hasName, /*valueSpecified=*/false);
  }

  const EdgeLabel* edge(const QString& v, const QString& w, const QString& name = {}) const {
    const QString e = edgeIdFor(v, w, name.isNull() ? false : true, name);
    const auto it = edgeLabels_.constFind(e);
    return it == edgeLabels_.cend() ? nullptr : &it.value();
  }
  EdgeLabel* edge(const QString& v, const QString& w, const QString& name = {}) {
    const QString e = edgeIdFor(v, w, name.isNull() ? false : true, name);
    const auto it = edgeLabels_.find(e);
    return it == edgeLabels_.end() ? nullptr : &it.value();
  }
  const EdgeLabel* edge(const Edge& edge) const {
    const QString e = edgeIdFor(edge.v, edge.w, edge.hasName, edge.name);
    const auto it = edgeLabels_.constFind(e);
    return it == edgeLabels_.cend() ? nullptr : &it.value();
  }
  EdgeLabel* edge(const Edge& edge) {
    const QString e = edgeIdFor(edge.v, edge.w, edge.hasName, edge.name);
    const auto it = edgeLabels_.find(e);
    return it == edgeLabels_.end() ? nullptr : &it.value();
  }

  bool hasEdge(const QString& v, const QString& w, const QString& name = {}) const {
    return edgeLabels_.contains(edgeIdFor(v, w, !name.isNull(), name));
  }

  void removeEdge(const QString& v, const QString& w, const QString& name = {}) {
    removeEdgeObj(edgeIdFor(v, w, !name.isNull(), name));
  }
  void removeEdge(const Edge& edge) {
    removeEdgeObj(edgeIdFor(edge.v, edge.w, edge.hasName, edge.name));
  }

  QList<Edge> inEdges(const QString& v, const QString& u = {}) const {
    const auto it = in_.constFind(v);
    if (it == in_.cend()) return {};
    QList<Edge> all = it.value().values();
    if (u.isNull()) return all;
    QList<Edge> filtered;
    for (const Edge& edge : all)
      if (edge.v == u) filtered.append(edge);
    return filtered;
  }

  QList<Edge> outEdges(const QString& v, const QString& w = {}) const {
    const auto it = out_.constFind(v);
    if (it == out_.cend()) return {};
    QList<Edge> all = it.value().values();
    if (w.isNull()) return all;
    QList<Edge> filtered;
    for (const Edge& edge : all)
      if (edge.w == w) filtered.append(edge);
    return filtered;
  }

  QList<Edge> nodeEdges(const QString& v, const QString& w = {}) const {
    QList<Edge> result = inEdges(v, w);
    for (const Edge& edge : outEdges(v, w)) result.append(edge);
    return result;
  }

  // Establish edges along a node path. Mirrors `setPath`.
  template <typename Iterable>
  void setPath(const Iterable& vs, EdgeLabel value) {
    bool first = true;
    QString prev;
    for (const QString& node : vs) {
      if (!first) setEdge(prev, node, value);
      prev = node;
      first = false;
    }
  }

private:
  void ensureNode(const QString& v) {
    if (nodeLabel_.contains(v)) return;
    nodeLabel_.insert(v, defaultNode_ ? defaultNode_(v) : NodeLabel{});
    nodeOrder_.append(v);
    if (isCompound_) {
      parent_[v] = detail::kGraphNode;
      children_[v];  // create empty child set
      children_[detail::kGraphNode].set(v, true);
    }
    in_[v];
    preds_[v];
    out_[v];
    sucs_[v];
  }

  void removeFromParentsChildList(const QString& v) {
    const auto pit = parent_.find(v);
    if (pit == parent_.end()) return;
    const auto cit = children_.find(pit.value());
    if (cit != children_.end()) cit.value().remove(v);
  }

  void setEdgeCore(const QString& vIn, const QString& wIn, EdgeLabel value, const QString& name,
                   bool hasName, bool valueSpecified) {
    const QString e = edgeIdFor(vIn, wIn, hasName, name);
    if (edgeLabels_.contains(e)) {
      if (valueSpecified) edgeLabels_[e] = std::move(value);
      return;
    }
    if (hasName && !isMultigraph_)
      throw std::runtime_error("Cannot set a named edge when isMultigraph = false");

    ensureNode(vIn);
    ensureNode(wIn);

    edgeLabels_[e] = valueSpecified ? std::move(value)
                                    : (defaultEdge_ ? defaultEdge_(vIn, wIn, name) : EdgeLabel{});

    // Normalize v/w for undirected graphs so storage is consistent.
    Edge edgeObj = makeEdgeObj(vIn, wIn, hasName, name);
    edgeObjs_[e] = edgeObj;
    edgeOrder_.append(e);

    incrementOrInit(preds_[edgeObj.w], edgeObj.v);
    incrementOrInit(sucs_[edgeObj.v], edgeObj.w);
    in_[edgeObj.w].set(e, edgeObj);
    out_[edgeObj.v].set(e, edgeObj);
  }

  void removeEdgeObj(const QString& e) {
    const auto it = edgeObjs_.find(e);
    if (it == edgeObjs_.end()) return;
    const Edge edge = it.value();
    edgeLabels_.remove(e);
    edgeObjs_.erase(it);
    edgeOrder_.removeOne(e);
    decrementOrRemove(preds_[edge.w], edge.v);
    decrementOrRemove(sucs_[edge.v], edge.w);
    in_[edge.w].remove(e);
    out_[edge.v].remove(e);
  }

  Edge makeEdgeObj(const QString& v, const QString& w, bool hasName, const QString& name) const {
    QString a = v, b = w;
    if (!isDirected_ && a > b) std::swap(a, b);
    Edge obj{a, b, hasName ? name : QString(), hasName};
    return obj;
  }

  QString edgeIdFor(const QString& v, const QString& w, bool hasName, const QString& name) const {
    QString a = v, b = w;
    if (!isDirected_ && a > b) std::swap(a, b);
    QString id = a;
    id.append(detail::kEdgeKeyDelim);
    id.append(b);
    id.append(detail::kEdgeKeyDelim);
    id.append(hasName ? name : detail::kDefaultEdgeName);
    return id;
  }

  static void incrementOrInit(detail::OrderedMap<QString, int>& map, const QString& key) {
    if (int* slot = map.value(key))
      ++(*slot);
    else
      map.set(key, 1);
  }

  static void decrementOrRemove(detail::OrderedMap<QString, int>& map, const QString& key) {
    int* slot = map.value(key);
    if (!slot) return;
    if (--(*slot) == 0) map.remove(key);
  }

  bool isDirected_ = true;
  bool isMultigraph_ = false;
  bool isCompound_ = false;

  std::optional<GraphLabel> graphLabel_;
  NodeFn defaultNode_;
  EdgeFn defaultEdge_;

  QHash<QString, NodeLabel> nodeLabel_;
  QList<QString> nodeOrder_;
  QHash<QString, QString> parent_;
  QHash<QString, detail::OrderedMap<QString, bool>> children_;
  QHash<QString, detail::OrderedMap<QString, Edge>> in_;
  QHash<QString, detail::OrderedMap<QString, Edge>> out_;
  QHash<QString, detail::OrderedMap<QString, int>> preds_;
  QHash<QString, detail::OrderedMap<QString, int>> sucs_;
  QHash<QString, Edge> edgeObjs_;
  QHash<QString, EdgeLabel> edgeLabels_;
  QList<QString> edgeOrder_;
};

// --- Compound query helpers (Milestone B unit-test surface) ---
// graph.js has no `ancestors`/`lca`; these are pure queries over `parent()`
// kept iterative so an arbitrarily deep compound chain cannot overflow.

template <typename G>
QList<QString> ancestorsOf(const G& g, QString v) {
  QList<QString> result;
  QString guard;
  while (!(guard = g.parentOf(v)).isNull()) {
    result.append(guard);
    v = guard;
  }
  return result;
}

// Lowest common ancestor of two nodes by depth (the node closer to the root
// walks up first). Returns the empty string if they share no ancestor.
template <typename G>
QString lowestCommonAncestor(const G& g, const QString& a, const QString& b) {
  QList<QString> aa = ancestorsOf(g, a);  // a, parent, ...
  QList<QString> bb = ancestorsOf(g, b);
  QHash<QString, bool> seen;
  seen.insert(a, true);
  for (const QString& p : aa) seen.insert(p, true);
  if (seen.contains(b)) return b;
  for (const QString& p : bb)
    if (seen.contains(p)) return p;
  return {};
}

}  // namespace muffin::mermaid::graphlib
