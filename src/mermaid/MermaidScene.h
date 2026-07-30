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

#include <QJsonObject>
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

  // Canonical structural+geometric+style dump shared by (a) the deterministic
  // SVG root-id digest (MermaidSvgExporter) and (b) parity/regression oracles.
  // Each concrete scene serializes bounds + its element arrays (id/geometry/
  // style/label), with numbers rounded to 0.001 — mirrors FlowScene::toJson.
  virtual QJsonObject toJsonObject() const = 0;

  // The full scene rect a raster image should cover, given a symmetric padding
  // hint. The generic image/SVG-canvas paths use this so they need not know the
  // family. Default: scene bounds expanded by padding on every side; SequenceScene
  // overrides (its margins are asymmetric and already baked into its viewport).
  virtual QRectF renderBounds(qreal padding) const {
    const QRectF b = sceneBounds();
    return b.adjusted(-padding, -padding, padding, padding);
  }

  // True if the scene has time-animated elements (e.g. animated flowchart edges).
  // Default false; FlowScene overrides. Drives the editor's repaint timer.
  virtual bool hasAnimation() const { return false; }
};

}  // namespace muffin::mermaid
