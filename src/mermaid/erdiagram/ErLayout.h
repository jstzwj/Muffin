#pragma once

#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErRough.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QFont>
#include <QHash>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::er {

// Layout-projection model, mirroring StateLayout.h. The er diagram is projected
// into the flowchart dagre pipeline (vertices = entities, edges = relationships,
// no subgraphs) exactly as StateLayout.cpp projects state nodes/edges. See
// IMPLEMENTATION_SPEC.md for the projection rules.
struct ErLayoutEntityInput {
  QString id;
  QString name; // what the table shows: the alias, else the id
  QVector<ErAttribute> attributes;
  QString cssClasses; // classDef names applied via class
  QStringList styles; // inline `style` declarations (key:value)
  QString link;       // click/href interaction directive target (spec §514 gap)
  QString linkTarget; // link open target (unused by current seams, kept for parity)
  QString tooltip;    // click tooltip directive
};

struct ErLayoutRelationshipInput {
  QString id;
  QString entityA;
  QString entityB;
  ErCardinality cardA = ErCardinality::ExactlyOne;
  ErCardinality cardB = ErCardinality::ExactlyOne;
  bool identifying = true;
  QString label;
};

struct ErLayoutInput {
  QVector<ErLayoutEntityInput> entities;
  QVector<ErLayoutRelationshipInput> relationships;
  // TB, BT, RL or LR: the diagram's `direction` statement, TB by default.
  QString direction = QStringLiteral("TB");
};

// How one text of the diagram is set: by erDiagram's stylesheet, and over it
// by what a classDef, an inline `style` or themeCSS declares for it.
struct ErTextStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QFont::Weight fontWeight = QFont::Normal;
  bool italic = false;
  // Every label of erBox has `line-height: 1.5`: its line box is this many
  // times the font size, whatever the size.
  qreal lineHeightFactor = 1.5;
  QString color;
  qreal lineHeight() const { return fontSize * lineHeightFactor; }
};

// The kinds of text an entity has. A stylesheet rule may tell them apart by
// the class of their label, as `.node .label.name` does.
enum class ErTextKind { Name, Type, AttributeName, Keys, Comment };

struct ErEntityTextStyle {
  ErTextStyle name, type, attributeName, keys, comment;
  const ErTextStyle& of(ErTextKind kind) const {
    switch (kind) {
    case ErTextKind::Name:
      return name;
    case ErTextKind::Type:
      return type;
    case ErTextKind::AttributeName:
      return attributeName;
    case ErTextKind::Keys:
      return keys;
    case ErTextKind::Comment:
      return comment;
    }
    return name;
  }
};

struct ErTextOptions {
  ErTextStyle table;        // an entity's texts, where nothing says otherwise
  ErTextStyle relationship; // `.edgeLabel .label` is 14px
  // What the classDefs, the inline styles and themeCSS declare for the texts
  // of one entity, by entity id.
  QHash<QString, ErEntityTextStyle> entities;
  // The handDrawn look draws a copy of every entity behind its shapes, whose
  // labels take what the classDefs and themeCSS declare for them, and not the
  // inline styles, which the copy does not carry.
  QHash<QString, ErEntityTextStyle> backgrounds;
  // The two keys are read separately. An entity's texts follow `htmlLabels`:
  // false draws them as SVG <text> instead of HTML. A relationship label
  // follows that key too where it is set, and else `flowchart.htmlLabels`,
  // the key it shared with flowcharts once: a false there moves the label
  // to SVG alone, and leaves every entity in HTML.
  bool htmlLabels = true;
  bool relationshipHtmlLabels = true;
  // erBox scales both paddings by 1.25 unless `htmlLabels` is true. The key
  // is unset by default, and erBox's test reads the raw value.
  bool scalePaddings = true;
};

// A text as Mermaid lays the diagram out by.
struct ErTextMetrics {
  flowchart::FlowLabelDocument document; // wrapped where Mermaid wraps it
  // The box the layout reads, from the text's origin: an HTML label's own,
  // or the one an SVG text reports as it is appended.
  QRectF box;
  ErTextStyle style; // the font it is drawn in
  QSizeF size() const { return box.size(); }
};

// One attribute row of an entity table, in the sizes it is drawn.
struct ErTableRowMetrics {
  ErTextMetrics type, name, keys, comment;
  QString keysText;
  qreal height = 0.0;
  qreal top = 0.0; // from the bottom of the name band
};

// An entity as Mermaid's erBox lays it out: the name over rows of four cells
// (type, name, keys, comment), each column as wide as its widest text plus the
// padding, and a column with no text in any row left out. An entity without
// attributes has a name and a size only.
struct ErTableMetrics {
  QSizeF size; // the outer rectangle
  ErTextMetrics name;
  qreal nameTop = 0.0;     // the name's origin, from the table's top edge
  qreal nameHeight = 0.0;  // the name band: the name and the padding under it
  qreal padding = 0.0;     // added to each cell's width, and between the table's edge and the first cell's text
  qreal textPadding = 0.0; // added under the name and under each row's tallest text
  qreal typeWidth = 0.0, nameWidth = 0.0, keysWidth = 0.0, commentWidth = 0.0;
  QVector<ErTableRowMetrics> rows;
};

// A relationship's label. `size` is the box dagre reserves for it. `drawnBox`
// is that box from the point dagre places the label at, and `textBox` where
// the glyphs go: an HTML label fills its box, an SVG one is centred in it.
struct ErRelationshipLabelMetrics {
  flowchart::FlowLabelDocument document;
  QSizeF size;
  QRectF drawnBox;
  QRectF textBox;
  ErTextStyle style; // the font the text is drawn in
};

struct ErLayoutMeasurements {
  QMap<QString, QSizeF> entities;
  QMap<QString, QSizeF> relationships;
  QHash<QString, ErTableMetrics> tables;
  QHash<QString, ErRelationshipLabelMetrics> labels;
  // The handDrawn look: the rectangle rough.js draws for each entity, and the
  // box it takes, about the rectangle's own centre. An entity is laid out by
  // the box, which is larger than the rectangle, and its centre is the box's.
  // Empty in the other looks.
  QHash<QString, ErRoughShape> rough;
};

struct ErPlacementEntity {
  QString id;
  QPointF center;
  QSizeF size;
};

// One drawn path of a relationship. A relationship from an entity to itself is
// laid out through two invisible points and drawn as three paths: the marker
// of entityA on the first, no marker on the middle one, the marker of entityB
// on the last, and the label on the middle one. `loopIndex` is a path's place
// in that loop, -1 for a relationship drawn as one path.
struct ErPlacementRelationship {
  QString id;
  QString path;
  QVector<QPointF> points;
  int loopIndex = -1;
  // Which ends of the whole relationship this path is: the start marker sits
  // on the first path, the end marker on the last.
  bool markerStart = true;
  bool markerEnd = true;
  std::optional<QPointF> labelPosition;
  QSizeF labelSize;
};

struct ErPlacementResult {
  QVector<ErPlacementEntity> entities;
  QVector<ErPlacementRelationship> relationships;
  // The 0.1 x 0.1 marks Mermaid leaves at the invisible points of a loop.
  QVector<QRectF> loopMarks;
};

ErLayoutInput buildErLayoutInput(const ErDiagramData& data);

// A text as createText lays it out for erBox: Markdown, and broken where
// createFormattedText breaks an SVG text where `maximumLineWidth` is positive.
ErTextMetrics measureErText(const QString& text, const ErTextStyle& style, bool htmlLabels,
                            qreal maximumLineWidth = 0.0);

// The box an SVG text is drawn in, from its origin: the box its layout gave
// it, which is where its glyphs go.
QRectF measureErSvgTextDrawnBox(const flowchart::FlowLabelDocument& document, const ErTextStyle& style);

// A relationship's label. An HTML one is the box its text takes at the
// relationship font size. An SVG one is sized before the stylesheet's 14px
// reaches it, at `rootSize`, and drawn 2px inside that box.
ErRelationshipLabelMetrics measureErRelationshipLabel(const ErLayoutRelationshipInput& relationship,
                                                      const ErTextStyle& style, bool htmlLabels, qreal rootSize);

// What erBox reads from the configuration besides the fonts.
struct ErMeasureOptions {
  // The stylesheet's font, which erBox sizes an entity without attributes by
  // before it looks at the minimum width, whatever a style says of the entity.
  ErTextStyle siteStyle;
  qreal minEntityWidth = 100.0;
  qreal diagramPadding = 20.0;
  qreal entityPadding = 15.0;
  // The width an entity without attributes wraps a long name at.
  qreal wrappingWidth = 200.0;
  // The handDrawn look: what rough.js draws each entity's rectangle with, by
  // entity id. An entity is then sized by the box its shape takes. Empty in
  // the other looks.
  QHash<QString, ErRoughOptions> rough;
};

// erBox's layout of one entity, for the sizes dagre needs and for drawing.
// An entity without attributes has `minEntityWidth` for its width where its
// name and the padding fit in it, and wraps a longer name at the wrapping
// width otherwise.
ErTableMetrics measureErTable(const ErLayoutEntityInput& entity, const ErEntityTextStyle& styles, bool htmlLabels,
                              bool scalePaddings, const ErMeasureOptions& options);

ErLayoutMeasurements measureErLayoutInput(const ErLayoutInput& input, const ErTextOptions& text,
                                          ErMeasureOptions options = {});

// Dagre's placement of the entities and the relationships between them, with
// erDiagram's default spacing (its `nodeSpacing` is 140, where the other
// diagrams share 50).
ErPlacementResult layoutErDiagramDagre(const ErLayoutInput& input, const ErLayoutMeasurements& measurements,
                                       qreal entitySpacing = 140.0, qreal rankSpacing = 80.0);

// The kind of a marker, as Mermaid names its definitions: onlyOne,
// zeroOrOne... Empty for MdParent, which has no marker.
QString erMarkerName(ErCardinality card);

// The definition's own name, with its end: onlyOneStart, zeroOrOneEnd...
QString erMarkerKey(ErCardinality card, bool start);

} // namespace muffin::mermaid::er
