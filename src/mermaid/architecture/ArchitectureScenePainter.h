#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::architecture {

struct ArchitectureScene;
void paintArchitectureScene(const ArchitectureScene& scene, QPainter& painter,
                            const MermaidPaintOptions& options = {});

}  // namespace muffin::mermaid::architecture
