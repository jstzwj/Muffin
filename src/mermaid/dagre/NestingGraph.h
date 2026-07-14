#pragma once

// Native port of dagre-d3-es/src/dagre/nesting-graph.js (C1 of the flowchart
// plan). A nesting graph adds dummy top/bottom border nodes around each cluster,
// wires them with nesting edges so ranking keeps clusters vertically compact,
// and connects the whole graph to a virtual root.
//
// Preconditions (per upstream): the input graph is a DAG and every edge has a
// `minlen`. Postconditions: the graph is connected; cluster border dummies
// exist; edge minlen is scaled so real nodes do not share a rank with border
// nodes; `nestingRoot` and `nodeRankFactor` are recorded for cleanup / rank
// compaction.

#include "mermaid/dagre/DagreLabels.h"

#include <QHash>
#include <QString>

namespace muffin::mermaid::dagre {

// Made visible for unit tests.
QHash<QString, int> treeDepths(const DagreGraph& g);

void runNestingGraph(DagreGraph& g);
void cleanupNestingGraph(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
