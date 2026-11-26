#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErRough.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::er {

// What paints one element of an erDiagram's document, as its stylesheet, the
// classDefs, the inline styles and themeCSS resolve together.
struct ErPaint {
  QString fill;   // empty where nothing fills
  QString stroke; // empty where nothing strokes; `url(#er-gradient)` where the
                  // neo look strokes with the theme's gradient
  qreal strokeWidth = 1.0;
  // The dash lengths as the document resolved them, "5 5" or the "0 0 101 0"
  // the neo look writes along a line; empty where the stroke is solid.
  QString strokeDasharray;
  // rough.js draws a shape by two paths, one that fills and one that strokes,
  // and the stroke of the filling path runs round the corners where the other
  // has none. The neo look's stylesheet strokes both.
  bool joined = false;
  // The handDrawn look fills with a sketch of lines in the fill's color, this
  // wide; 0 where the fill is not a sketch.
  qreal fillWidth = 0.0;
};

// The gradient the neo look strokes a node with where the theme has one: over
// the box of each shape that takes it, from left to right.
struct ErGradient {
  QString start;
  QString stop;
};

// The paint of an entity: its table (the rectangle, for an entity without
// attributes), its rows, and the lines between them; and the filter over the
// table, `none` or a shadow.
struct ErEntityPaint {
  ErPaint table, oddRows, evenRows, dividers;
  QString filter = QStringLiteral("none");
};

// A crow's-foot marker: what strokes it, and what paints the ring the "zero"
// markers carry.
struct ErMarkerPaint {
  qreal strokeWidth = 1.0;
  QString stroke;
  QString ringFill;
  QString ringStroke;
};

// A relationship line, and the background of its label.
struct ErRelationshipPaint {
  ErPaint line;
  QColor labelBackground;
};

// The key of a relationship's path where the document's paint keeps it: the
// relationship's id, or with the path's place for one of the three paths of a
// relationship from an entity to itself, which each carry a dash of their own.
QString erRelationshipKey(const QString& id, int loopIndex);

// The paint of a whole document, by entity id, by relationship path
// (erRelationshipKey()), of the markers by erMarkerKey(), and of the copies
// drawn behind the entities under the handDrawn look, by entity id. The scene
// style's own paint stands in for whatever is missing.
struct ErDocumentPaint {
  QHash<QString, ErEntityPaint> entities;
  QHash<QString, ErRelationshipPaint> relationships;
  QHash<QString, ErMarkerPaint> markers;
  // The copies behind the entities under the handDrawn look: what paints
  // them, and how their texts are set.
  QHash<QString, ErEntityPaint> backgrounds;
  QHash<QString, ErEntityTextStyle> backgroundTexts;
};

// What erDiagram's stylesheet paints with: its fonts, the theme variables it
// names, and the strokes that are not theme variables.
struct ErSceneStyle {
  // The stylesheet's font: `.label` for the texts of an entity, where neither
  // a classDef, an inline style nor themeCSS says otherwise, and
  // `.edgeLabel .label` for a relationship label.
  ErTextStyle text;
  ErTextStyle relationshipText;
  // `.node rect`: an entity without attributes. The table of an entity with
  // attributes starts from the same two.
  QString nodeFill;   // mainBkg
  QString nodeBorder;
  // The table's attribute rows. Empty: the theme defines no such color (neo
  // and the redux themes), and the rows stay unfilled.
  QString rowOddFill;
  QString rowEvenFill;
  // An entity without attributes is 1px wide of stroke, unless the look is
  // neo. Mermaid draws a table with rough.js, whose stroke defaults to 1.3.
  qreal boxStrokeWidth = 1.0;
  qreal tableStrokeWidth = 1.3;
  // `.relationshipLine`: lineColor, never filled.
  QString lineColor;
  qreal lineStrokeWidth = 1.0;
  // A relationship label's background: `.labelBkg`'s.
  QColor labelBackground;
  QColor svgLabelBackground; // `.edgeLabel .label rect`, under htmlLabels:false
  // `.marker`: 1px of the line's color, and a white ring.
  ErMarkerPaint marker;
  bool htmlLabels = true;
  bool relationshipHtmlLabels = true;
  // erBox scales both paddings by 1.25 unless `htmlLabels` is true.
  bool scalePaddings = true;
  // The neo look: markers are sized in user units and the "zero or more"
  // ring sits elsewhere; where the theme has a gradient, `url(#er-gradient)`
  // strokes with it.
  bool neo = false;
  ErGradient gradient;
  // The handDrawn look: rough.js draws the shapes from the seed, which is 0
  // by default, with which it draws at random; and behind every entity lies a
  // copy of it in the background color, which the theme's `background` names.
  bool handDrawn = false;
  quint32 handDrawnSeed = 0;
  QString background;
  QHash<QString, ErRoughEntity> rough; // by entity id
};

// A drawn text: its document, the box the document gives it, where its glyphs
// go, and the font it is drawn in.
struct ErSceneText {
  flowchart::FlowLabelDocument document;
  QRectF rect;     // the box Mermaid's document gives the text
  QRectF textBox;  // where its glyphs go: `rect`, unless an SVG label's box is larger
  ErTextStyle style;
  // An SVG <text>: its first baseline is a font size below the top of its
  // box, not an HTML line box.
  bool svgText = false;
};

// One attribute row of a table: its rectangle, its paint and its four cells.
// A cell with no text has an empty document and a null rectangle.
struct ErSceneRow {
  QRectF rect;
  ErPaint paint;
  ErSceneText type;
  ErSceneText name;
  ErSceneText keys;
  ErSceneText comment;
};

// The handDrawn look of an entity: the shapes rough.js draws in place of its
// rectangles, each with the box it takes.
struct ErSceneRough {
  ErRoughShape table;             // the outer rectangle
  QVector<ErRoughShape> rows;     // as many as the entity has rows
  QVector<ErRoughShape> dividers; // as many as the entity has dividers
};

// `bounds` is the table. An entity without attributes has a name only; one
// with attributes has rows, and the lines between the name and the rows and
// between the columns in `dividers`. Under the handDrawn look `bounds` is the
// box the drawn table takes, a row's rectangle the box of its drawn shape, and
// a divider the line through the box of its drawn shape; `rough` holds them.
struct ErSceneEntity {
  QString id;
  QRectF bounds;
  // Where dagre put the rectangle's own centre, from which erBox lays it out.
  QPointF centre;
  ErSceneText name;
  QVector<ErSceneRow> rows;
  QVector<QLineF> dividers;
  // The paint the table is drawn with.
  ErPaint table;
  ErPaint oddRows;
  ErPaint evenRows;
  ErPaint dividerPaint;
  // The filter over the table: `none`, or a shadow as a `drop-shadow()`.
  QString filter = QStringLiteral("none");
  QString textColor; // the name's
  // Interaction directives (click/href/tooltip), feeding interactionRegions().
  QString link;
  QString tooltip;
  // How many times the first divider, the line under the name, is drawn:
  // erBox appends it twice, as two elements of the document, whose
  // antialiasing adds up. The scene holds the line once.
  int nameDividerPasses = 1;
  std::optional<ErSceneRough> rough; // the handDrawn look
};

// One of Mermaid's crow's-foot markers (erMarkerDef): the geometry is placed
// with its (refX, refY) on the line's end and its x axis along the line, so a
// Start marker lies ahead of the start of the line and an End marker behind
// its end. The ring is the circle of the "zero" markers. Under the neo look
// the "zero or more" ring is nearer the crow's foot.
struct ErMarker {
  qreal refX = 0.0;
  qreal refY = 0.0;
  qreal width = 0.0;  // the marker's viewport
  qreal height = 0.0;
  QString path;
  bool hasRing = false;
  qreal ringX = 0.0;
  qreal ringY = 0.0;
  qreal ringRadius = 0.0;
};

// nullopt for MdParent, which has no marker.
std::optional<ErMarker> erMarker(ErCardinality card, bool start, bool neo = false);

// One drawn path of a relationship. A relationship from an entity to itself is
// three: the first carries the marker of entityA, the last that of entityB,
// the middle one the label. `loopIndex` is a path's place in such a loop, -1
// for a relationship drawn as one path. `identifying` selects the solid or
// the dashed line. Under the handDrawn look `path` is the path the layout
// gives, which rough.js draws as `rough`, with the markers on its ends.
struct ErSceneRelationship {
  QString id;
  QString path;
  int loopIndex = -1;
  std::optional<ErCardinality> cardA; // the marker at the start
  std::optional<ErCardinality> cardB; // the marker at the end
  bool identifying = true;
  QString label;
  ErSceneText labelText; // an empty document when there is no label
  QColor labelBackground;
  ErPaint line;
  std::optional<ErRoughShape> rough; // the handDrawn look
};

struct ErScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  // The drawing's box, sized as the browser sizes the SVG element for it:
  // the image and the SVG canvas are made from it.
  QRectF renderBounds() const override { return rasterBounds.isValid() ? rasterBounds : bounds; }
  // Upstream's viewBox is fractional and the browser screenshots it to the
  // nearest pixel, so the raster is rounded like the natural size.
  bool roundRasterExtentToNearestPixel() const override { return true; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  SvgMarkerProjection svgMarkerProjection() const override;
  const QVector<InteractionRegion>& interactionRegions() const override { return interactions; }

  QRectF bounds;
  // `bounds` at the size of erRasterExtent(bounds, the diagram's padding)
  // less that padding twice over; invalid where the two are the same to the
  // pixel.
  QRectF rasterBounds;
  QVector<ErSceneEntity> entities;
  QVector<ErSceneRelationship> relationships;
  // The handDrawn look: a copy of every entity in the theme's background
  // color, behind the drawn shapes, in the order of the entities.
  QVector<ErSceneEntity> backgrounds;
  QVector<InteractionRegion> interactions;
  // The marker definitions, in the order Mermaid writes them.
  QVector<QPair<ErCardinality, bool>> markerOrder;
  QHash<QString, ErMarkerPaint> markerPaint; // by erMarkerName() and its end
  ErSceneStyle style;
  QString role = QStringLiteral("graphics-document document");
  QString ariaRoleDescription = QStringLiteral("erDiagram");
};

// The raster a browser gives an erDiagram of these bounds: the image of the
// SVG element. Mermaid sizes the element's viewBox to the drawing and
// `padding` on every side, and caps its CSS width at the viewBox's own width,
// which Chrome floors to a layout cell of a sixty-fourth. The element keeps
// the viewBox's proportions, so a cap a hair under the width scales the height
// by as much: enough to round to the pixel the other way where the height
// lies by a half.
QSize erRasterExtent(const QRectF& bounds, qreal padding);

// Builds the immutable scene from the layout input, the table metrics measured
// for dagre and dagre's placement. The result is safe to copy and reuse across
// paint passes.
ErScene buildErScene(const ErLayoutInput& input, const ErPlacementResult& placement,
                     const ErLayoutMeasurements& measurements, ErSceneStyle style, const ErDocumentPaint& paint = {});

} // namespace muffin::mermaid::er
