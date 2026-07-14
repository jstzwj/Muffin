#pragma once

// Native port of dagre-d3-es/src/dagre/add-border-segments.js (C3 of the
// flowchart plan). For each cluster that has minRank/maxRank (set earlier by
// assignRankMinMax), create a left and right border dummy per rank, parent each
// to the cluster, and link consecutive same-side dummies with weight-1 edges.
// These border dummies let the ordering and BK phases treat the cluster's
// vertical extent as real participants.

#include "mermaid/dagre/DagreLabels.h"

namespace muffin::mermaid::dagre {

void addBorderSegments(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
