#pragma once

// Painter for the quadrantChart family: quadrant rects + labels, border lines,
// data-point circles + labels, rotated axis labels, and the title. Mirrors the
// pie/requirement painter idiom. Geometry is painted in scene (absolute)
// coordinates — the layout is already absolute (0..chartWidth, 0..chartHeight).

class QPainter;
class QColor;
class QString;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::quadrant {

struct QuadrantScene;

void paintQuadrantScene(const QuadrantScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::quadrant
