#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::wardley {

struct WardleyScene;

void paintWardleyScene(const WardleyScene &scene, QPainter &painter,
                       const MermaidPaintOptions &options);

} // namespace muffin::mermaid::wardley
