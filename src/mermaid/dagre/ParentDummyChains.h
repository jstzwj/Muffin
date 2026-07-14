#pragma once

// Native port of dagre-d3-es/src/dagre/parent-dummy-chains.js (C2 of the
// flowchart plan). Each long-edge dummy chain (created by normalize, listed in
// g.graph().dummyChains) is re-parented onto the cluster path between its
// endpoints' lowest common ancestor, so a long edge crossing cluster boundaries
// threads its dummy nodes through the right compound parents.

#include "mermaid/dagre/DagreLabels.h"

#include <QHash>
#include <QList>
#include <QString>

namespace muffin::mermaid::dagre {

struct PostorderEntry {
  int low = 0;
  int lim = 0;
};

// Made visible for unit tests.
QHash<QString, PostorderEntry> postorder(const DagreGraph& g);

struct ClusterPath {
  QList<QString> path;
  QString lca;
};
ClusterPath findPath(const DagreGraph& g, const QHash<QString, PostorderEntry>& postorderNums,
                     const QString& v, const QString& w);

void parentDummyChains(DagreGraph& g);

}  // namespace muffin::mermaid::dagre
