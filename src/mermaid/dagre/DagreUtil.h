#pragma once

// Native port of dagre-d3-es/src/dagre/util.js — the helper layer shared by every
// runLayout phase. Only the helpers the compound pipeline (milestones C1-C3) and
// later phases need are ported here; more are added as the pipeline advances.

#include "mermaid/dagre/DagreLabels.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace muffin::mermaid::dagre {

// util.addDummyNode: create a node with a unique "<name><n>" id (counter on the
// graph label, deterministic per layout) and the `dummy` type tag set.
QString addDummyNode(DagreGraph& g, const QString& type, DagreNodeLabel attrs,
                     const QString& name);

// util.addBorderNode(g, prefix) — 2-arg form used by nesting-graph.
QString addBorderNode(DagreGraph& g, const QString& prefix);
// util.addBorderNode(g, prefix, rank, order) — 4-arg form; rank/order set only
// when both are supplied (mirrors `arguments.length >= 4`).
QString addBorderNode(DagreGraph& g, const QString& prefix, int rank, int order);

// util.maxRank: largest rank among nodes that have one, or nullopt if none.
std::optional<int> maxRank(const DagreGraph& g);

// util.buildLayerMatrix: layering[rank][order] = v for every node with a rank.
QVector<QVector<QString>> buildLayerMatrix(const DagreGraph& g);

// util.intersectRect: where a ray from `point` toward the rectangle's centre
// hits the rectangle border (node label carries x/y/width/height).
QPointF intersectRect(const DagreNodeLabel& rect, const QPointF& point);

// util.asNonCompoundGraph: a copy with only leaf nodes and all edges (rank() runs
// on this view so compound nodes are ignored).
DagreGraph asNonCompoundGraph(const DagreGraph& g);

// util.normalizeRanks: shift every ranked node so the minimum rank is 0.
void normalizeRanks(DagreGraph& g);

// util.removeEmptyRanks: collapse empty ranks while respecting nodeRankFactor
// (so the half-rank structure for edge labels is preserved).
void removeEmptyRanks(DagreGraph& g);

// util.partition: split a list into {lhs: fn=true, rhs: fn=false}.
template <typename T>
struct Partition {
  QVector<T> lhs;
  QVector<T> rhs;
};
template <typename T>
Partition<T> partition(const QVector<T>& collection, const std::function<bool(const T&)>& fn) {
  Partition<T> result;
  for (const T& value : collection) {
    if (fn(value))
      result.lhs.append(value);
    else
      result.rhs.append(value);
  }
  return result;
}

}  // namespace muffin::mermaid::dagre
