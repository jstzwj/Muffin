#pragma once

// Typed label set for the native Dagre layout pipeline. These mirror the
// whitelisted attribute subsets that dagre's layout.js copies onto its layout
// graph (buildLayoutGraph) plus the transient fields each runLayout phase
// introduces (nesting-graph, normalize, add-border-segments, order, position/bk).
//
// Upstream: node_modules/dagre-d3-es/src/dagre/layout.js (graphNumAttrs /
// nodeNumAttrs / edgeNumAttrs / edgeAttrs and per-phase field usage).
//
// `dummy` is a QString type tag ("root" | "border" | "edge" | "edge-label" |
// "edge-proxy" | "selfedge"); an empty string means "not a dummy", matching
// graphlib's truthy `node.dummy` checks. Optional scalars use std::optional so
// presence mirrors lodash `_.has` / `_.isUndefined`.

#include "mermaid/graphlib/Graph.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::dagre {

// Edge label: defined first because node labels reference it by value.
struct DagreEdgeLabel {
  int minlen = 1;
  int weight = 1;
  qreal width = 0.0;
  qreal height = 0.0;
  int labeloffset = 10;
  QString labelpos = QStringLiteral("r");
  QVector<QPointF> points;
  std::optional<qreal> x;
  std::optional<qreal> y;
  std::optional<int> labelRank;
  bool nestingEdge = false;
  bool reversed = false;  // set by acyclic.run, undone by acyclic.undo
  std::optional<QString> forwardName;  // original edge name, stashed by acyclic.run
};

// A captured self-edge stored on its node before being re-inserted as a dummy.
struct DagreSelfEdge {
  graphlib::Edge e;
  DagreEdgeLabel label;
};

struct DagreNodeLabel {
  qreal width = 0.0;
  qreal height = 0.0;
  std::optional<int> rank;
  std::optional<int> order;
  std::optional<qreal> x;
  std::optional<qreal> y;
  QString dummy;  // type tag; empty = real node
  // Compound-cluster border bookkeeping (nesting-graph / assignRankMinMax /
  // add-border-segments).
  std::optional<int> minRank;
  std::optional<int> maxRank;
  QString borderTop;
  QString borderBottom;
  QVector<QString> borderLeft;
  QVector<QString> borderRight;
  QString borderType;  // "borderLeft" | "borderRight"
  // Transient single-string border ids used inside the order phase's layer graph
  // (buildLayerGraph sets these to borderLeft[rank]/borderRight[rank]).
  QString layerBorderLeft;
  QString layerBorderRight;
  // Edge-dummy chain bookkeeping (normalize).
  std::optional<graphlib::Edge> edgeObj;
  std::optional<DagreEdgeLabel> edgeLabel;
  QString labelpos;  // set on edge-label dummies
  // Self-edge re-insertion (layout.js removeSelfEdges/insertSelfEdges).
  QVector<DagreSelfEdge> selfEdges;
};

struct DagreGraphLabel {
  QString rankdir = QStringLiteral("tb");  // tb | bt | lr | rl
  qreal nodesep = 50.0;
  qreal edgesep = 20.0;
  qreal ranksep = 50.0;
  qreal marginx = 0.0;
  qreal marginy = 0.0;
  std::optional<int> maxRank;
  int nodeRankFactor = 0;
  QString nestingRoot;            // node id of the virtual root (nesting-graph)
  QVector<QString> dummyChains;   // chain-head node ids (normalize)
  qreal width = 0.0;
  qreal height = 0.0;
  QString align;  // UL/UR/DL/DR override for BK balance; empty = median balance
  QString ranker;  // "" | "network-simplex" | "tight-tree" | "longest-path"
  // Deterministic dummy-id generator backing util.addDummyNode. JS uses a
  // module-global _.uniqueId counter; per-graph state keeps a layout reproducible
  // and independent of other layouts in the same process.
  quint64 nextDummyId = 0;
};

using DagreGraph = graphlib::Graph<DagreNodeLabel, DagreEdgeLabel, DagreGraphLabel>;

}  // namespace muffin::mermaid::dagre
