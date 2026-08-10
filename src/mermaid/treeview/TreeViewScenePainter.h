#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::treeview {

struct TreeViewScene;

void paintTreeViewScene(QPainter& painter, const TreeViewScene& scene,
                        const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::treeview
