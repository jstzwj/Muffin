#pragma once

// Native port of dagre-d3-es/src/dagre/coordinate-system.js. adjust() swaps
// width/height for LR/RL layouts so the TB-oriented rank/order/position phases
// can run unchanged; undo() flips Y for BT/RL and swaps X/Y back for LR/RL.

#include "mermaid/dagre/DagreLabels.h"

namespace muffin::mermaid::dagre {

void adjustCoordinateSystem(DagreGraph& g);
void undoCoordinateSystem(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
