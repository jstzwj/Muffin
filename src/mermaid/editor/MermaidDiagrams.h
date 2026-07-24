#pragma once

#include "mermaid/MermaidDiagram.h"

#include <QString>

namespace muffin::mermaid::editor {

// Returns the Diagram that handles the detected type id, or nullptr if the
// type is not natively rendered. The registry is the single source of truth
// for which diagram types render natively; renderSource() dispatches through
// it instead of an if/else chain.
const Diagram* findMermaidDiagram(const QString& type);

}  // namespace muffin::mermaid::editor
