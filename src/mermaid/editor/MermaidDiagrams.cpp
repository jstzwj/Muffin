#include "mermaid/editor/MermaidDiagrams.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace muffin::mermaid::editor {

// Centralized registry. Each family's render adapter lives in its own TU
// (<Family>DiagramAdapter.cpp) and is exposed as a lazy singleton; this composes
// them, so no consumer TU needs to know every native family. The singletons are
// function-local statics (lazy), so there is no static-initialization-order
// dependency between TUs.
const Diagram* findMermaidDiagram(const QString& type) {
  static const QVector<const Diagram*> kAll = {
      &stateDiagramAdapter(),       &classDiagramAdapter(),   &sequenceDiagramAdapter(),
      &flowchartDiagramAdapter(),   &erDiagramAdapter(),      &requirementDiagramAdapter(),
      &pieDiagramAdapter(),         &quadrantDiagramAdapter(), &journeyDiagramAdapter(),
      &radarDiagramAdapter(),       &xyChartDiagramAdapter(),   &timelineDiagramAdapter(),
      &packetDiagramAdapter(),      &kanbanDiagramAdapter()};
  static const QHash<QString, const Diagram*> kByType = [] {
    QHash<QString, const Diagram*> registry;
    for (const Diagram* diagram : kAll)
      for (const QString& id : diagram->ids()) registry.insert(id, diagram);
    return registry;
  }();
  return kByType.value(type, nullptr);
}

}  // namespace muffin::mermaid::editor
