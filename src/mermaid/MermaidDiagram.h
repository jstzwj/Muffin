#pragma once

// Diagram contract for the 1:1-parity architecture (docs/mermaid-architecture.md,
// L1). Each diagram family implements this interface and registers under its
// detected type id(s); the render cache dispatches renderSource() through the
// registry instead of an if/else chain, so adding a diagram family no longer
// touches the orchestrator.
//
// MermaidRenderEntry is forward-declared (defined in MermaidRenderCache.h);
// MermaidPreprocessResult is pulled in by value via its light header. Diagram
// lives in the editor namespace alongside the render cache that drives it.

#include "mermaid/MermaidPreprocessor.h"

#include <QString>
#include <QStringList>

namespace muffin::mermaid::editor {

struct MermaidRenderEntry;

struct Diagram {
  virtual ~Diagram() = default;

  // Detected type id(s) this diagram handles, e.g. {"er"} or
  // {"state", "stateDiagram"}.
  virtual QStringList ids() const = 0;

  // Run the full pipeline (parse -> measure -> layout -> scene) and return a
  // Ready entry, or throw a diagram-specific parse error (caught centrally by
  // the orchestrator).
  virtual MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                                    const QString& type,
                                    const QString& theme) const = 0;
};

}  // namespace muffin::mermaid::editor
