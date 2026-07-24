#pragma once

#include <QByteArray>

namespace muffin::mermaid::editor {

struct MermaidRenderEntry;

// Serializes an already-built immutable scene through its production painter,
// then normalizes the SVG root and adds accessibility/link semantics.
QByteArray renderMermaidEntryToSvg(const MermaidRenderEntry& entry,
                                   qsizetype instanceIndex = 0);

}  // namespace muffin::mermaid::editor
