#pragma once

// Layout projection for the requirementDiagram family. Requirement nodes
// (shape "requirementBox") + relationship edges are projected into the shared
// flowchart dagre pipeline (flowchart::layoutFlowchartNodesDagre) exactly as
// ER/State/Class project their nodes/edges — zero custom layout code.
//
// See src/mermaid/classdiagram/ClassLayout.cpp (lines ~407-448) and
// src/mermaid/erdiagram/ErLayout.cpp (lines ~188-237) for the projection
// template. Requirement reuses the same FlowchartData → dagre → FlowLayoutResult
// round-trip, then maps the result back to RequirementPlacement* structs.

#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/requirement/RequirementComputedStyle.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/requirement/RequirementTextStyle.h"

#include <QMap>
#include <QColor>
#include <QFont>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::requirement {

// One text row of a requirementBox: the text + whether it is the bold name line.
struct RequirementLayoutRow {
  QString text;
  bool bold = false;
  RequirementTextStyle resolvedStyle;
  bool hasResolvedStyle = false;
  qreal opacity = 1.0;
  bool visible = true;
  bool hasBox = true;
  bool rootHasBox = true;
  RequirementComputedElement wrapperComputed;
  RequirementComputedElement paintedTextComputed;
};

struct RequirementResolvedShapeStyle {
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  qreal opacity = 1.0;
  bool visible = true;
  bool hasBox = true;
  bool rootHasBox = true;
};

// Per-node layout input. The node's visual rows (type line, name, body rows)
// are pre-computed so the measurer + scene builder can share them without
// re-deriving the row model from RequirementNode/ElementNode.
struct RequirementLayoutNodeInput {
  QString id;            // = name
  QString name;          // display name
  QString displayType;   // "Functional Requirement" / "Element" / ...
  bool isElement = false;
  QVector<RequirementLayoutRow> rows;  // top-to-bottom: type, name, [body...]
  bool hasBody = false;  // whether any body row is present (drives the divider)
  QStringList cssClasses;
  QStringList cssStyles;
  bool groupVisible = true;
  bool groupHasBox = true;
  bool groupRootHasBox = true;
  bool hasResolvedBoxStyle = false;
  RequirementResolvedShapeStyle boxStyle;
  bool hasResolvedDividerStyle = false;
  RequirementResolvedShapeStyle dividerStyle;
  RequirementComputedElement dividerWrapperComputed;
  QVector<RequirementComputedElement> dividerChildPathComputed;
};

struct RequirementLayoutEdgeInput {
  QString id;
  QString start;
  QString end;
  QString label;             // "<<contains>>" etc. (decoded)
  QString relationshipType;  // "contains" / "copies" / ...
  bool isContains = false;   // contains: solid + start marker; else dashed + end marker
  bool hasResolvedPathStyle = false;
  RequirementResolvedShapeStyle pathStyle;
  RequirementLayoutRow resolvedLabel;
  RequirementResolvedShapeStyle labelBackgroundStyle;
  RequirementComputedElement outerLabelComputed;
  RequirementComputedElement innerLabelComputed;
  RequirementComputedElement containerBgComputed;
  RequirementComputedElement paintedSpanComputed;
  bool hasLabelCascade = false;
};

struct RequirementLayoutInput {
  QVector<RequirementLayoutNodeInput> nodes;
  QVector<RequirementLayoutEdgeInput> edges;
  QString direction = QStringLiteral("TB");
};

// Per-node measurements: the box size + the list of row heights (used by the
// scene builder to position rows).
struct RequirementNodeMeasurement {
  QSizeF boxSize;
  QVector<qreal> rowHeights;  // one per input row
  QVector<QRectF> rowInkBounds;  // raw DOM/text bbox before label transforms
  QVector<flowchart::FlowLabelDocument> rowDocuments;
  qreal typeHeight = 0.0;     // height of the first (type) row
  qreal nameHeight = 0.0;     // height of the name row
  bool hasBody = false;
};

using RequirementLayoutMeasurements = QMap<QString, RequirementNodeMeasurement>;

struct RequirementPlacementNode {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
};

struct RequirementPlacementEdge {
  QString id;
  QString path;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  std::optional<QPointF> labelPosition;
};

struct RequirementPlacementResult {
  QVector<RequirementPlacementNode> nodes;
  QVector<RequirementPlacementEdge> edges;
};

RequirementLayoutInput buildRequirementLayoutInput(const RequirementDiagramData& data);

// Effective resolved font-size/family for a node: the style override, else the
// theme base. (style.fontSizePx < 0 / fontFamily empty => theme.)
qreal requirementEffectiveFontSize(const RequirementTextStyle& style, qreal themeFontSize);
QString requirementEffectiveFontFamily(const RequirementTextStyle& style,
                                       const QString& themeFontFamily);

// Prepared row document shared by layout measurement and scene paint so the
// measured width matches the drawn ink. Commit 3: applies the node's text-transform
// to `text` BEFORE parseFlowLabel, and sets the Commit-2 FlowLabelDocument fields
// (baseWeight/baseStyle/letter-spacing/word-spacing/decoration) from `style`; the
// name row's bold is layered on top of the base weight. effectiveFontSizePx is the
// style override (or themeFontSize) used for math preparation.
flowchart::FlowLabelDocument requirementRowDocument(const QString& text, bool bold,
    const RequirementTextStyle& style, qreal themeFontSize,
    bool htmlLabels = true);

// Measures each node's box + row heights. Mirrors the requirementBox geometry:
// padding = 20, gap = 20 between name and the first body row. Commit 3: resolves
// each node's text style and measures with the effective font + applies the node's
// text-transform; font-size:0 / line-height:0 collapse the node to 20x20 (no ink).
RequirementLayoutMeasurements measureRequirementLayoutInput(
    const RequirementLayoutInput& input, const QString& fontFamily = QStringLiteral("Noto Sans"),
    qreal fontSize = 16.0, QFont::Weight themeFontWeight = QFont::Normal,
    bool htmlLabels = true);

RequirementPlacementResult layoutRequirementDiagramDagre(
    const RequirementLayoutInput& input, const RequirementLayoutMeasurements& measurements,
    qreal nodeSpacing = 50.0, qreal rankSpacing = 50.0,
    const QString& fontFamily = QStringLiteral("Noto Sans"), qreal fontSize = 16.0,
    bool htmlLabels = true);

// htmlLabels:false edge-label box (upstream insertEdgeLabel + createText →
// createFormattedText): the dagre/painted size is the SVG <text> getBBox —
// advance width × (hhea cell height + (rows-1)·1.1em) — expanded by the 2px
// background-rect padding per side. CSS line-height does not move SVG tspans,
// so the row pitch is the fixed 1.1em dy regardless of themeCSS.
QSizeF requirementSvgEdgeLabelSize(const flowchart::FlowLabelDocument& document,
                                   const QString& fontFamily, qreal fontSize);

}  // namespace muffin::mermaid::requirement
