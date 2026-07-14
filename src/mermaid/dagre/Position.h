#pragma once

// Native port of dagre-d3-es/src/dagre/position/bk.js (C5 of the flowchart
// plan): Brandes-Kopf horizontal coordinate assignment, INCLUDING the compound
// findType2Conflicts border scan and block-graph compaction that the flat
// pipeline lacks.

#include "mermaid/dagre/DagreLabels.h"

#include <QHash>
#include <QString>

namespace muffin::mermaid::dagre {

// Assigns an `x` coordinate to every node (TB orientation; LR/RL handled by
// the coordinate-system swap). Returns the final balanced x map and also writes
// node->x on the graph.
QHash<QString, qreal> positionX(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
