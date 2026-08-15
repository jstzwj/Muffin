#pragma once

#include "mermaid/architecture/ArchitectureDiagram.h"

#include <QHash>
#include <QJsonValue>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace muffin::mermaid::architecture {

struct ArchitectureFcoseOptions {
  qreal iconSize = 80.0;
  qreal padding = 40.0;
  qreal fontSize = 16.0;
  qreal nodeSeparation = 75.0;
  qreal idealEdgeLengthMultiplier = 1.5;
  qreal edgeElasticity = 0.45;
  int numIter = 2500;
  bool randomize = false;
  quint32 seed = 1;
};

struct ArchitectureFcoseResult {
  // Mermaid's architecture renderer applies these values directly as SVG
  // group translations, so they are the leaf node's top-left coordinates.
  QHash<QString, QPointF> topLeft;
  QHash<QString, QRectF> groups;
};

ArchitectureFcoseResult layoutArchitectureFcose(
    const ArchitectureData& data, const ArchitectureFcoseOptions& options,
    const QHash<QString, qreal>& renderedNodeHeights);

}  // namespace muffin::mermaid::architecture
