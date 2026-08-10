#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::timeline {

struct TimelineScene;

void paintTimelineScene(const TimelineScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::timeline
