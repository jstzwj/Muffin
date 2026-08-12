#pragma once

#include "mermaid/MermaidDiagram.h"

#include <QString>

namespace muffin::mermaid::editor {

// Each family's render adapter lives in its own TU (<Family>DiagramAdapter.cpp)
// and is exposed as a lazy singleton. findMermaidDiagram() is the single TU that
// composes them; every consumer (renderSource) goes through findMermaidDiagram(type)
// and stays family-agnostic — so adding a family touches one adapter TU + this
// registry + CMake + the accessor declaration, but no consumer code.
const Diagram& flowchartDiagramAdapter();
const Diagram& swimlaneDiagramAdapter();
const Diagram& sequenceDiagramAdapter();
const Diagram& classDiagramAdapter();
const Diagram& stateDiagramAdapter();
const Diagram& erDiagramAdapter();
const Diagram& requirementDiagramAdapter();
const Diagram& pieDiagramAdapter();
const Diagram& quadrantDiagramAdapter();
const Diagram& journeyDiagramAdapter();
const Diagram& radarDiagramAdapter();
const Diagram& xyChartDiagramAdapter();
const Diagram& timelineDiagramAdapter();
const Diagram& packetDiagramAdapter();
const Diagram& kanbanDiagramAdapter();
const Diagram& mindmapDiagramAdapter();
const Diagram& gitGraphDiagramAdapter();
const Diagram& c4DiagramAdapter();
const Diagram& ganttDiagramAdapter();
const Diagram& infoDiagramAdapter();
const Diagram& treeViewDiagramAdapter();
const Diagram& eventModelingDiagramAdapter();
const Diagram& ishikawaDiagramAdapter();
const Diagram& vennDiagramAdapter();
const Diagram& sankeyDiagramAdapter();
const Diagram& treemapDiagramAdapter();
const Diagram& cynefinDiagramAdapter();
const Diagram& wardleyDiagramAdapter();
const Diagram& architectureDiagramAdapter();
const Diagram& blockDiagramAdapter();

// Returns the Diagram that handles the detected type id, or nullptr if the
// type is not natively rendered. The registry is the single source of truth
// for which diagram types render natively; renderSource() dispatches through
// it instead of an if/else chain.
const Diagram* findMermaidDiagram(const QString& type);

}  // namespace muffin::mermaid::editor
