#pragma once

// Painter for the pie family. Renders the outer ring, the d3 pie/arc slices
// (rebuilt from the scene angles via QPainter arcTo — parseSvgPath does not
// support the SVG `A` command, and the byte-parity pathD string is an oracle
// concern, not a paint concern), the percentage labels, the title, and the
// legend block. Mirrors the classdiagram/er/requirement painter idiom.
//
// All chart geometry is painted in a group translated to the pie center
// (centerX, centerY) = (225, 225), matching mermaid's
// group.attr("transform", translate(pieWidth/2, height/2)).

class QPainter;
class QString;
class QColor;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::pie {

struct PieScene;

void paintPieScene(const PieScene& scene, QPainter& painter,
                   const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::pie
