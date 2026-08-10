#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::mindmap {
struct MindmapScene;
void paintMindmapScene(const MindmapScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options = {});
}  // namespace muffin::mermaid::mindmap
