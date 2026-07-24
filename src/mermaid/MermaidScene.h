#pragma once

// Common base for every diagram scene (flowchart, sequence, class, state, er).
//
// The render cache and exporters hold scenes through this uniform pointer so
// they can paint and measure bounds without type-switching on the diagram
// family. Concrete scenes inherit and delegate paint() to their existing free
// painter; sceneBounds() returns the pre-computed `bounds` member.
//
// This is the foundational seam of the 1:1-parity architecture
// (docs/mermaid-architecture.md, L1): it lets a future Diagram interface +
// registry treat all diagram families uniformly.

#include "mermaid/MermaidPaintOptions.h"

#include <QRectF>

class QPainter;

namespace muffin::mermaid {

struct MermaidScene {
  virtual ~MermaidScene() = default;

  // Diagram bounds in scene coordinates (the concrete scene's `bounds`).
  virtual QRectF sceneBounds() const = 0;

  // Paint the scene. Callers that need a non-default paint mode (e.g. the
  // category-mask / golden pipeline) still call the concrete free painter
  // directly; this entry covers the normal color render path used by the
  // image and SVG backends.
  virtual void paint(QPainter& painter, const MermaidPaintOptions& options) const = 0;
};

}  // namespace muffin::mermaid
