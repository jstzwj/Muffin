#pragma once

// Native port of dagre-d3-es/src/dagre/rank/ (network-simplex ranker).
// rankGraph builds a simplified leaf-only view of the compound graph, ranks it
// with network-simplex, and writes the ranks back onto the compound graph's
// leaf nodes. (dagre relies on JS label-reference sharing to do this
// implicitly; with value semantics we write back explicitly.)

#include "mermaid/dagre/DagreLabels.h"

namespace muffin::mermaid::dagre {

// Rank every leaf node. Honours g.graph()->ranker ("tight-tree" / "longest-path"
// / "network-simplex" / default). Default is network-simplex.
void rankGraph(DagreGraph& g);

// Exposed for unit tests:
void longestPath(DagreGraph& g);
int slack(const DagreGraph& g, const graphlib::Edge& e);
void networkSimplex(DagreGraph& g);  // input must already be simplified

}  // namespace muffin::mermaid::dagre
