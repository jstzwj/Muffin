#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::venn {

struct VennScene;

void paintVennScene(const VennScene& scene, QPainter& painter,
                    const MermaidPaintOptions& options = {});

}  // namespace muffin::mermaid::venn
