#pragma once

// Native port of dagre-d3-es/src/dagre/layout.js orchestration. runDagreLayout
// runs the full 27-phase runLayout pipeline on a fully-built DagreGraph (nodes
// with width/height, edges with minlen/weight/labelpos/labeloffset/width/height,
// compound parents for clusters, graph label with rankdir/nodesep/edgesep/
// ranksep/marginx/marginy). After it returns, every real node has x/y/width/
// height, every edge has points (+ x/y for labelled edges), and clusters have
// width/height (border dummies removed).

#include "mermaid/dagre/DagreLabels.h"

#include <QJsonObject>
#include <QString>

#include <vector>

namespace muffin::mermaid::dagre {

// A captured intermediate-state snapshot of the layout graph at a named phase
// boundary (milestone G2). `state` is an implementation-independent census
// (real-node rank/order/parent/minRank/maxRank + a per-type dummy count + edge
// minlen/weight with dummy endpoints abstracted to their type tag), so native
// and upstream (whose dummy-id counters differ) can be compared without id
// matching. The contract is compared at <=0.002 on numerics, exact on tags.
struct DagreSnapshot {
  QString phase;
  QJsonObject state;
};

// Runs the full pipeline. When `snapshots` is non-null, a DagreSnapshot is
// appended after each of the rank / parentDummyChains / addBorderSegments /
// order phases (the stable, diagnostically meaningful boundaries). Null by
// default → zero overhead for the production/editor path.
void runDagreLayout(DagreGraph& g, std::vector<DagreSnapshot>* snapshots = nullptr);

}  // namespace muffin::mermaid::dagre
