#include "mermaid/theme/FlowStyleResolve.h"

#include "mermaid/theme/MermaidStyleResolve.h"

namespace muffin::mermaid::flowstyle {
namespace {

// Flowchart adapter: map the flowchart-specific inputs onto the family-agnostic
// style:: API. Behaviour is identical to the previous inline implementation
// (MermaidStyleResolve is a verbatim lift of it).

style::ThemeDefaults themeDefaults(const flowtheme::FlowThemeVariables& theme) {
  style::ThemeDefaults t;
  t.mainBkg = theme.mainBkg;
  t.nodeBorder = theme.nodeBorder;
  t.lineColor = theme.lineColor;
  t.strokeWidth = theme.strokeWidth;
  t.textColor = theme.textColor;
  t.nodeTextColor = theme.nodeTextColor;
  t.fontFamily = theme.fontFamily;
  t.fontSize = theme.fontSize;
  return t;
}

// A flowchart classDef contributes its node styles then its text styles.
QVector<style::ClassDef> classDefs(const QVector<flowchart::FlowClass>& classes) {
  QVector<style::ClassDef> out;
  out.reserve(classes.size());
  for (const flowchart::FlowClass& cls : classes) {
    style::ClassDef def;
    def.id = cls.id;
    def.declarations = cls.styles + cls.textStyles;
    out.append(def);
  }
  return out;
}

ResolvedNodeStyle toNodeResult(const style::ResolvedNodeStyle& s) {
  ResolvedNodeStyle r;
  r.nodeStyles = s.nodeStyles;
  r.labelStyles = s.labelStyles;
  r.fill = s.fill;
  r.stroke = s.stroke;
  r.strokeWidth = s.strokeWidth;
  r.color = s.color;
  r.fontFamily = s.fontFamily;
  r.fontSize = s.fontSize;
  r.fontWeight = s.fontWeight;
  return r;
}

ResolvedEdgeStyle toEdgeResult(const style::ResolvedEdgeStyle& s) {
  ResolvedEdgeStyle r;
  r.stroke = s.stroke;
  r.strokeWidth = s.strokeWidth;
  r.strokeDasharray = s.strokeDasharray;
  r.fill = s.fill;
  return r;
}

}  // namespace

QStringList compiledClassStyles(const QStringList& classNames,
                                const QVector<flowchart::FlowClass>& classes) {
  return style::compiledClassStyles(classNames, classDefs(classes));
}

bool isLabelStyle(const QString& key) { return style::isLabelStyle(key); }

ResolvedNodeStyle resolveNodeStyle(const flowchart::FlowVertex& vertex,
                                   const QVector<flowchart::FlowClass>& classes,
                                   const flowtheme::FlowThemeVariables& theme) {
  return toNodeResult(style::resolveNodeStyle(vertex.classes, vertex.styles,
                                              classDefs(classes), themeDefaults(theme)));
}

ResolvedEdgeStyle resolveEdgeStyle(const flowchart::FlowEdge& edge,
                                   const flowtheme::FlowThemeVariables& theme) {
  return toEdgeResult(style::resolveEdgeStyle(edge.stroke, edge.style,
                                              edge.animate, edge.animation,
                                              themeDefaults(theme)));
}

}  // namespace muffin::mermaid::flowstyle
