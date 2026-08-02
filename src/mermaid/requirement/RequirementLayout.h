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
#include "mermaid/flowchart/FlowLabel.h"

#include <QMap>
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
};

struct RequirementLayoutEdgeInput {
  QString id;
  QString start;
  QString end;
  QString label;             // "<<contains>>" etc. (decoded)
  QString relationshipType;  // "contains" / "copies" / ...
  bool isContains = false;   // contains: solid + start marker; else dashed + end marker
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

// Prepared row document (markdown mode, FlowForeignObjectFlex context, optional
// bold) shared by layout measurement and scene paint so the measured width
// matches the drawn ink — markdown markers like **bold** are measured as their
// rendered form, not literal asterisks.
flowchart::FlowLabelDocument requirementRowDocument(const QString& text, qreal fontSize, bool bold);

// Measures each node's box + row heights. Mirrors the requirementBox geometry:
// padding = 20, gap = 20 between name and the first body row. Each row height =
// ink height + 6 (matching mermaid addText3's htmlLabels:false path, which the
// oracle fixture uses for determinism).
RequirementLayoutMeasurements measureRequirementLayoutInput(
    const RequirementLayoutInput& input, const QString& fontFamily = QStringLiteral("Noto Sans"),
    qreal fontSize = 16.0);

RequirementPlacementResult layoutRequirementDiagramDagre(
    const RequirementLayoutInput& input, const RequirementLayoutMeasurements& measurements,
    qreal nodeSpacing = 50.0, qreal rankSpacing = 50.0,
    const QString& fontFamily = QStringLiteral("Noto Sans"), qreal fontSize = 16.0);

}  // namespace muffin::mermaid::requirement
