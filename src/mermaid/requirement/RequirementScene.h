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
#include "mermaid/requirement/RequirementTextStyle.h"

#include <QColor>
#include <QFont>
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
  // The SVG-inherited foreground used when an inline fill/color value is
  // invalid (mermaid drops the declaration; the path inherits the svg `color`).
  // = theme textColor (#333 default / #ccc dark). Used by invalid-value fallback.
  QString foregroundFallback = QStringLiteral("#333333");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  // SVG-root base font-weight (the weight bolder/lighter resolve against at L1).
  QFont::Weight fontWeight = QFont::Normal;
  // requirementBox default border size (userNodeOverrides: strokeWidth = sw || 1.3);
  // the box outline AND the divider both render at this width unless overridden.
  qreal strokeWidth = 1.3;
};

// One text row in a requirementBox. `document` is pre-shaped so the painter
// reuses cached glyph runs (paintFlowLabel) instead of re-measuring.
struct RequirementSceneRow {
  QString text;
  QPointF center;     // relative to node center
  QSizeF size;
  bool bold = false;
  flowchart::FlowLabelDocument document;
  // Resolved per-node font/color (Commit 3); -1 / empty => use scene.style.
  qreal fontPixelSize = -1.0;
  QString fontFamily;
  qreal lineHeight = -1.0;
  QColor color;  // invalid => scene.style.textColor
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
  // Resolved box paint (compileStyles last-wins over the theme base). The box
  // OUTLINE and the DIVIDER are tracked independently — they share strokeWidth
  // and dashArray, but a `stroke` value of none/inherit/invalid hides only the
  // outline path (the divider keeps the theme color) EXCEPT `none`, which hides
  // both (probed against mermaid 11.16.0; see resolveBoxStyle).
  QString fill;
  bool fillNone = false;          // explicit fill:none -> paint no fill (NoBrush)
  QString outlineStroke;          // resolved box-outline color (theme / explicit / black for currentColor)
  bool outlineVisible = true;     // false => NoPen outline (stroke none/inherit/invalid or width<=0)
  QString dividerStroke;          // resolved divider color (theme divider / explicit / black)
  bool dividerVisible = true;     // false => NoPen divider (stroke none or width<=0)
  qreal strokeWidth = 1.3;
  QVector<qreal> dashArray = {0.0, 0.0};
  // Resolved text style (Commit 3): font-size/weight/style/family/line-height/
  // spacing/decoration/transform/color from the node's labelStyle, last-wins over
  // the theme base. Sentinel defaults mean "use the scene base".
  RequirementTextStyle text;
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
