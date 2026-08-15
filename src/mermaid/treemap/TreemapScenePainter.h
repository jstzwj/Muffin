#pragma once

#include "mermaid/MermaidScene.h"

class QPainter;

namespace muffin::mermaid::treemap {
struct TreemapScene;
void paintTreemapScene(const TreemapScene &scene, QPainter &painter,
                       const MermaidPaintOptions &options);
} // namespace muffin::mermaid::treemap
