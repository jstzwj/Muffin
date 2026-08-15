#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::railroad {

struct RailroadScene;

void paintRailroadScene(const RailroadScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options = {});

}  // namespace muffin::mermaid::railroad
