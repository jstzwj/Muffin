#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::er {

struct ErScene;

// Paints the scene with QPainter, antialiased, culling each primitive on its
// own bounds as MermaidPaintOptions asks. QPainter has no marker primitive,
// so the crow's-foot markers are drawn flattened; SVG export asks the scene
// for svgMarkerProjection() instead.
void paintErScene(const ErScene& scene, QPainter& painter, const MermaidPaintOptions& options = {});

} // namespace muffin::mermaid::er
