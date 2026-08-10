#pragma once

#include <QPointF>
#include <QSizeF>
#include <QVector>

namespace muffin::mermaid::mindmap {

struct MindmapCoseNodeInput {
  int id = -1;
  QSizeF size;
};

struct MindmapCoseEdgeInput {
  int start = -1;
  int end = -1;
};

struct MindmapCoseResult {
  QVector<QPointF> centers;
};

// Flat-tree subset of layout-base 2.0.1 + cose-base 2.2.0 and
// cytoscape-cose-bilkent 4.1.0 (MIT, i-Vis Research Group / Cytoscape
// Consortium). Mindmap always supplies one connected, acyclic, non-compound
// graph, so compound graphs, tiling and random/non-forest initialization are
// deliberately outside this module. The radial initialization, rectangular
// clipping, FR-grid neighborhood, force constants, proof cooling schedule and
// final graph-margin transform are direct ports of those packages.
MindmapCoseResult layoutMindmapCoseFlatTree(
    const QVector<MindmapCoseNodeInput>& nodes,
    const QVector<MindmapCoseEdgeInput>& edges);

}  // namespace muffin::mermaid::mindmap
