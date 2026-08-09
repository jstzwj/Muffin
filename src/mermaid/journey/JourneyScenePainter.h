#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::journey {

struct JourneyScene;

void paintJourneyScene(const JourneyScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::journey
