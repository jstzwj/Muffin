#pragma once

// Painter for the requirementDiagram family. Renders requirementBox nodes
// (rounded rect + <<Type>> + bold name + divider + body rows), relationship
// edges (solid/dashed + 2 markers), and centered edge labels. Mirrors the
// classdiagram::ClassScenePainter / er::ErScenePainter idiom.

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::requirement {

struct RequirementScene;

void paintRequirementScene(const RequirementScene& scene, QPainter& painter,
                           const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::requirement
