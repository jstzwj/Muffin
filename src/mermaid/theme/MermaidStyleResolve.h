#pragma once

// Family-agnostic style cascade — the generalization of FlowStyleResolve.
// Native port of mermaid 11.16.0's per-node / per-edge style cascade
// (chunk-BNCO5QFQ.mjs compileStyles/styles2String + chunk-YI7H2ERT.mjs
// getCompiledStyles). Merges classDef + inline `style` with last-wins, splits
// into labelStyles (color/font-*) and nodeStyles (fill/stroke/...) each suffixed
// `!important`, and returns resolved paint (theme defaults overridden by the
// cascade).
//
// Priority (low -> high): theme defaults < classDef < `class` statement <
// inline `style` < `linkStyle` (edges). compileStyles concatenates the layers
// and dedups by key (last-wins).
//
// Each diagram family supplies an adapter that maps its per-element fields
// (classes, inline styles, classDef table, theme defaults) onto these generic
// inputs. FlowStyleResolve is the flowchart adapter.

#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::style {

// The subset of theme variables the cascade reads. Families fill this from
// their resolved theme (flowchart: FlowThemeVariables; class/state/er: the
// same FlowThemeVariables-derived values).
struct ThemeDefaults {
  QString mainBkg;
  QString nodeBorder;
  QString lineColor;
  qreal strokeWidth = 1.0;
  QString textColor;
  QString nodeTextColor;  // may be empty (falls back to textColor)
  QString fontFamily;
  QString fontSize;       // e.g. "16px"
};

// A classDef entry. `declarations` merges the class's node styles and text
// styles (in that order), matching mermaid's getCompiledStyles which pushes
// styles then textStyles.
struct ClassDef {
  QString id;
  QStringList declarations;
};

struct ResolvedNodeStyle {
  // Inline `style` attribute strings (classDef + inline merged, `!important`).
  // nodeStyles -> the box, labelStyles -> the label.
  QString nodeStyles;
  QString labelStyles;
  // Resolved paint (theme defaults overridden by the cascade).
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
  QString fill;  // "none" for edges (auto-added)
};

// Resolve a node's style. `classes` names the classDefs that apply (the
// "default" and "node" built-ins are auto-prepended inside, so `classDef
// default`/`classDef node` apply to every node). `inlineStyles` are the
// per-element `style` declarations, applied after classDef.
ResolvedNodeStyle resolveNodeStyle(const QStringList& classes,
                                   const QStringList& inlineStyles,
                                   const QVector<ClassDef>& classDefs,
                                   const ThemeDefaults& theme);

// Resolve an edge's style. `pattern` is the edge thickness token
// (normal/dotted/thick/invisible). `linkStyles` are the linkStyle declarations
// (classDef-via-setClass already merged in by the caller, plus the auto
// "fill:none"). `animate`/`animation` force the dash pattern.
ResolvedEdgeStyle resolveEdgeStyle(const QString& pattern,
                                   const QStringList& linkStyles,
                                   bool animate, const QString& animation,
                                   const ThemeDefaults& theme);

// Merged classDef declarations (key:value, last-wins) for the given class names
// — the edge/subgraph analogue of resolveNodeStyle without the inline `style`
// tail or the default/node prepend. Callers split by key.
QStringList compiledClassStyles(const QStringList& classNames,
                                const QVector<ClassDef>& classDefs);

// Whether a CSS property key is a label (text) style vs a node (box) style.
bool isLabelStyle(const QString& key);

}  // namespace muffin::mermaid::style
