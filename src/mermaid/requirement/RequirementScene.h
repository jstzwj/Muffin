#pragma once

// Immutable scene for the requirementDiagram family. Holds the resolved
// requirementBox geometry + per-row text documents + relationship edges +
// markers, and delegates paint() to paintRequirementScene().
//
// Mirrors classdiagram::ClassScene / er::ErScene: a struct inheriting
// MermaidScene with QRectF bounds, QVector<Node>, QVector<Edge>, QVector<Marker>
// + a style struct. toJsonObject() dumps the font-independent + font-coupled
// fields used by the geometry oracle (node count, row model, height; edge
// relationship-type multiset).

#include "mermaid/MermaidScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/requirement/RequirementLayout.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>

class QPainter;

namespace muffin::mermaid::requirement {

struct RequirementSceneStyle {
  QString boxFill = QStringLiteral("#ECECFF");       // theme mainBkg
  QString boxStroke = QStringLiteral("#9370DB");      // theme border1
  QString textColor = QStringLiteral("#131300");      // theme primaryTextColor
  QString dividerColor = QStringLiteral("#9370DB");   // theme nodeBorder
  QString lineColor = QStringLiteral("#333333");      // theme lineColor
  QString edgeLabelFill = QStringLiteral("#ECECFF");  // theme edgeLabelBackground
  QString edgeLabelColor = QStringLiteral("#333333"); // theme textColor
  QString titleColor = QStringLiteral("#333333");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  qreal strokeWidth = 1.0;
};

// One text row in a requirementBox. `document` is pre-shaped so the painter
// reuses cached glyph runs (paintFlowLabel) instead of re-measuring.
struct RequirementSceneRow {
  QString text;
  QPointF center;     // relative to node center
  QSizeF size;
  bool bold = false;
  flowchart::FlowLabelDocument document;
};

struct RequirementSceneNode {
  QString id;
  QString displayType;  // "Functional Requirement" / "Element" / ...
  bool isElement = false;
  QPointF center;
  QSizeF size;          // box size (width × height)
  // Rows: type line, name (bold), body rows (only non-empty fields).
  QVector<RequirementSceneRow> rows;
  bool hasDivider = false;   // body rows present → divider under the name
  qreal dividerY = 0.0;      // divider line Y relative to node center (body top)
  int bodyRowCount = 0;      // count of body rows (font-independent)
  // Resolved paint (inline style / classDef — deferred for the pilot).
  QString fill;
  QString stroke;
};

struct RequirementSceneEdge {
  QString id;
  QString path;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  QString label;             // "<<contains>>"
  QString relationshipType;  // "contains" / "copies" / ...
  bool isContains = false;
  QString markerStart;  // "requirement_contains" or ""
  QString markerEnd;    // "requirement_arrow" or ""
  QString pattern;      // "normal" (solid) / "dashed"
  std::optional<QPointF> labelPosition;
  flowchart::FlowLabelDocument labelDocument;
  QSizeF labelSize;
  QRectF pathBounds;
  QRectF labelBounds;
};

// Marker definition (rendered in the painter's <defs> equivalent).
struct RequirementMarkerChild {
  QString tag;       // "path" / "circle" / "line" / "g"
  QString path;      // SVG path d (for path children)
  qreal cx = 0.0;    // circle center (for circle children)
  qreal cy = 0.0;
  qreal radius = 0.0;
  // line children (for requirement_contains cross):
  qreal x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
};

struct RequirementMarkerDefinition {
  QString type;       // "requirement_contains" / "requirement_arrow"
  QString suffix;     // "Start" / "End"
  qreal refX = 0.0;
  qreal refY = 0.0;
  qreal markerWidth = 20.0;
  qreal markerHeight = 20.0;
  // contains = g(circle + 2 lines); arrow = single path.
  bool isContains = false;
  // arrow path data:
  QString arrowPath;
};

struct RequirementScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QVector<RequirementSceneNode> nodes;
  QVector<RequirementSceneEdge> edges;
  QVector<RequirementMarkerDefinition> markers;
  RequirementSceneStyle style;
};

// The two requirement markers (contains-start, arrow-end), matching
// chunk-52WLFC77.mjs ~1034-1039 / ~1013-1021.
QVector<RequirementMarkerDefinition> requirementMarkerDefinitions();

RequirementScene buildRequirementScene(
    const RequirementLayoutInput& input,
    const RequirementLayoutMeasurements& measurements,
    const RequirementPlacementResult& placement,
    RequirementSceneStyle style = {});

}  // namespace muffin::mermaid::requirement
