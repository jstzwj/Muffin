#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::eventmodeling {

struct EventModelingScene;

void paintEventModelingScene(const EventModelingScene& scene,
                             QPainter& painter,
                             const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::eventmodeling
