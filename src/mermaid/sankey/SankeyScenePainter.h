#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::sankey {
struct SankeyScene;
void paintSankeyScene(const SankeyScene &scene, QPainter &painter,
                      const MermaidPaintOptions &options = {});
} // namespace muffin::mermaid::sankey
