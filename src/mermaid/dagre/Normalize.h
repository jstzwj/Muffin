#pragma once

// Native port of dagre-d3-es/src/dagre/normalize.js. run() splits long edges
// into unit-length segments via dummy nodes (recording chain heads in
// g.graph().dummyChains); undo() collapses the chains back, collecting the
// positioned dummy coordinates into the restored edge label.

#include "mermaid/dagre/DagreLabels.h"

namespace muffin::mermaid::dagre {

void runNormalize(DagreGraph& g);
void undoNormalize(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
