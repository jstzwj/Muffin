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

// Parse a pie slice fill value (theme palette entries are either "#RRGGBB" hex
// or "hsl(H, S%, L%)") into a QColor. Returns an invalid color for an empty
// fill (mermaid's dark-theme pie12 is unset — the slice paints no fill).
QColor parsePieColor(const QString& value);

}  // namespace muffin::mermaid::pie
