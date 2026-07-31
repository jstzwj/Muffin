#pragma once

#include "mermaid/MermaidDiagram.h"

#include <QString>

namespace muffin::mermaid::editor {

// Each family's render adapter lives in its own TU (<Family>DiagramAdapter.cpp)
// and is exposed as a lazy singleton; findMermaidDiagram() composes the registry
// from these so no TU needs to know all five families.
const Diagram& flowchartDiagramAdapter();
const Diagram& sequenceDiagramAdapter();
const Diagram& classDiagramAdapter();
const Diagram& stateDiagramAdapter();
const Diagram& erDiagramAdapter();

// Returns the Diagram that handles the detected type id, or nullptr if the
// type is not natively rendered. The registry is the single source of truth
// for which diagram types render natively; renderSource() dispatches through
// it instead of an if/else chain.
const Diagram* findMermaidDiagram(const QString& type);

}  // namespace muffin::mermaid::editor
