#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::radar {

struct RadarScene;

void paintRadarScene(const RadarScene& scene, QPainter& painter,
                     const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::radar
