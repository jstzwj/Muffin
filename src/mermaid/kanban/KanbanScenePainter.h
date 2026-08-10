#pragma once

#include "mermaid/kanban/KanbanScene.h"

class QPainter;

namespace muffin::mermaid::kanban {

void paintKanbanScene(const KanbanScene& scene, QPainter& painter,
                      const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::kanban
