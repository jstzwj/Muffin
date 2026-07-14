#pragma once

// Native port of mermaid 11.16.0's per-node / per-edge style cascade
// (chunk-BNCO5QFQ.mjs compileStyles/styles2String + chunk-YI7H2ERT.mjs
// getCompiledStyles). Merges classDef (cssCompiledStyles) + inline `style`
// (cssStyles) with last-wins, then splits into labelStyles (color/font-*) and
// nodeStyles (fill/stroke/...) each suffixed `!important` — the exact strings
// mermaid sets as the element's inline `style` attribute.
//
// Priority (low -> high): theme CSS defaults < classDef < `class` statement <
// inline `style` < `linkStyle` (edges). compileStyles concatenates the layers
// and styles2Map dedups by key (last-wins), so the merged result already
// honours the cascade. Theme defaults are NOT part of the inline style string
// (they come from the getStyles CSS); resolveNodeStyle returns them separately
// as the resolved fill/stroke/etc. for the painter.

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/theme/FlowTheme.h"

#include <QString>

namespace muffin::mermaid::flowstyle {

struct ResolvedNodeStyle {
  // The inline `style` attribute strings mermaid sets on the element
  // (classDef + inline merged, `!important`). nodeStyles -> .label-container,
  // labelStyles -> .label. These are the golden-comparable values.
  QString nodeStyles;
  QString labelStyles;
  // Resolved paint for the painter (theme defaults overridden by the cascade).
  QString fill;
  QString stroke;
  QString strokeWidth;   // e.g. "2px"
  QString color;
  QString fontFamily;
  QString fontSize;      // e.g. "16px"
  QString fontWeight;
};

struct ResolvedEdgeStyle {
  QString stroke;
  QString strokeWidth;
  QString strokeDasharray;
  QString fill;  // always "none" for edges (auto-added)
};

// Resolve a node's style. `classes` is FlowchartData.classes (the classDef
// table); vertex.classes names which apply. theme provides the defaults.
ResolvedNodeStyle resolveNodeStyle(const flowchart::FlowVertex& vertex,
                                   const QVector<flowchart::FlowClass>& classes,
                                   const flowtheme::FlowThemeVariables& theme);

// Resolve an edge's style. edge.style holds the parsed linkStyle entries
// (already including the auto "fill:none"). theme provides lineColor/strokeWidth.
ResolvedEdgeStyle resolveEdgeStyle(const flowchart::FlowEdge& edge,
                                   const flowtheme::FlowThemeVariables& theme);

// Whether a CSS property key is a label (text) style vs a node (box) style.
// Mirrors khroma/mermaid's isLabelStyle predicate (chunk-BNCO5QFQ.mjs:38).
bool isLabelStyle(const QString& key);

}  // namespace muffin::mermaid::flowstyle
