#pragma once

#include <QByteArray>
#include <QUrl>

namespace muffin::mermaid::editor {

struct MermaidRenderEntry;

struct MermaidSvgExportOptions {
  qsizetype instanceIndex = 0;
  // Mermaid.render(id, source) uses the caller's id for all marker ids. Empty
  // keeps Muffin's deterministic/content-derived root id.
  QString diagramId;
  // Browser Mermaid derives absolute marker references from window.location.
  // Standalone callers can supply the embedding/export document URL; an empty
  // URL retains relative fragment references.
  QUrl documentUrl;
};

// Serializes an already-built immutable scene through its production painter,
// then normalizes the SVG root and adds accessibility/link semantics.
QByteArray renderMermaidEntryToSvg(const MermaidRenderEntry& entry,
                                   qsizetype instanceIndex = 0);
QByteArray renderMermaidEntryToSvg(const MermaidRenderEntry& entry,
                                   const MermaidSvgExportOptions& options);

}  // namespace muffin::mermaid::editor
