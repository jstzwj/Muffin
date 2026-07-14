#pragma once

// Native port of dagre-d3-es/src/dagre/acyclic.js. run() reverses a feedback
// arc set (default: dfsFAS — mermaid does not set `acyclicer: 'greedy'`) so the
// graph becomes a DAG for ranking; undo() restores original edge direction.
// Reversed edges are re-added with a generated name and `reversed=true`.

#include "mermaid/dagre/DagreLabels.h"

namespace muffin::mermaid::dagre {

void runAcyclic(DagreGraph& g);
void undoAcyclic(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
