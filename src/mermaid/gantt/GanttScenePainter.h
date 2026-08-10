#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::gantt {

struct GanttScene;

void paintGanttScene(const GanttScene& scene, QPainter& painter,
                     const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::gantt
