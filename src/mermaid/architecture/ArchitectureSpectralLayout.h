#pragma once

#include <QPointF>
#include <QVector>

#include <functional>

namespace muffin::mermaid::architecture {

struct ArchitectureSpectralRelativeConstraint {
  bool horizontal = true;
  int before = -1;
  int after = -1;
};

// The transformed fCoSE graph contains only childless nodes. Adjacency order
// is significant because the upstream greedy BFS sampler observes it.
QVector<QPointF> layoutArchitectureSpectral(
    const QVector<QVector<int>>& adjacency, qreal nodeSeparation,
    const std::function<double()>& random);

void transformArchitectureSpectralConstraints(
    QVector<QPointF>& positions,
    const QVector<QVector<int>>& verticalAlignments,
    const QVector<QVector<int>>& horizontalAlignments,
    const QVector<ArchitectureSpectralRelativeConstraint>& relative);

}  // namespace muffin::mermaid::architecture
