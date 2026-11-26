#include "mermaid/erdiagram/ErScene.h"

#include "mermaid/erdiagram/ErRough.h"
#include "mermaid/scene/SvgPathParse.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

// erDiagram's scene: the tables Mermaid's erBox draws, where dagre put them,
// and what paints each of their elements. Per IMPLEMENTATION_SPEC.md this .cpp
// defines its free functions with fully-qualified names and keeps its helpers
// in an anonymous namespace.

namespace er = muffin::mermaid::er;
namespace flowchart = muffin::mermaid::flowchart;
namespace scene = muffin::mermaid::scene;

namespace {

qreal r3(qreal value) {
  return std::round(value * 1000.0) / 1000.0;
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), r3(rect.x())},
          {QStringLiteral("y"), r3(rect.y())},
          {QStringLiteral("width"), r3(rect.width())},
          {QStringLiteral("height"), r3(rect.height())}};
}

QJsonObject lineJson(const QLineF& line) {
  return {{QStringLiteral("x1"), r3(line.x1())},
          {QStringLiteral("y1"), r3(line.y1())},
          {QStringLiteral("x2"), r3(line.x2())},
          {QStringLiteral("y2"), r3(line.y2())}};
}

QJsonValue textJson(const er::ErSceneText& text) {
  if (text.document.text.isEmpty())
    return QJsonValue();
  return QJsonObject{{QStringLiteral("text"), text.document.text}, {QStringLiteral("rect"), rectJson(text.rect)}};
}

QString cardName(er::ErCardinality card) {
  switch (card) {
  case er::ErCardinality::ExactlyOne:
    return QStringLiteral("ExactlyOne");
  case er::ErCardinality::ZeroOrOne:
    return QStringLiteral("ZeroOrOne");
  case er::ErCardinality::OneOrMore:
    return QStringLiteral("OneOrMore");
  case er::ErCardinality::ZeroOrMore:
    return QStringLiteral("ZeroOrMore");
  case er::ErCardinality::MdParent:
    return QStringLiteral("MdParent");
  }
  return {};
}

// The canvas Mermaid sizes its SVG to: everything it drew, by its own extent.
// A path's point, the edge of a label and a loop's 0.1px mark all count, where
// QRectF::united would drop them as zero-size rectangles.
class Canvas {
public:
  void add(const QPointF& point) {
    if (empty_) {
      left_ = right_ = point.x();
      top_ = bottom_ = point.y();
      empty_ = false;
      return;
    }
    left_ = qMin(left_, point.x());
    right_ = qMax(right_, point.x());
    top_ = qMin(top_, point.y());
    bottom_ = qMax(bottom_, point.y());
  }

  void add(const QRectF& rect) {
    const QRectF normal = rect.normalized();
    add(normal.topLeft());
    add(normal.bottomRight());
  }

  QRectF bounds() const { return empty_ ? QRectF() : QRectF(QPointF(left_, top_), QPointF(right_, bottom_)); }

private:
  qreal left_ = 0.0, right_ = 0.0, top_ = 0.0, bottom_ = 0.0;
  bool empty_ = true;
};

// The markers of erMarkerDef. The neo look's put the "zero or more" ring
// nearer the crow's foot.
er::ErMarker marker(er::ErCardinality card, bool start, bool neo) {
  using C = er::ErCardinality;
  switch (card) {
  case C::ExactlyOne:
    // A box of 18 x 18 with two perpendicular ticks.
    if (start)
      return {0.0, 9.0, 18.0, 18.0, QStringLiteral("M9,0 L9,18 M15,0 L15,18")};
    return {18.0, 9.0, 18.0, 18.0, QStringLiteral("M3,0 L3,18 M9,0 L9,18")};
  case C::ZeroOrOne:
    // A box of 30 x 18 with a tick and a ring.
    if (start)
      return {0.0, 9.0, 30.0, 18.0, QStringLiteral("M9,0 L9,18"), true, 21.0, 9.0, 6.0};
    return {30.0, 9.0, 30.0, 18.0, QStringLiteral("M21,0 L21,18"), true, 9.0, 9.0, 6.0};
  case C::OneOrMore:
    // A box of 45 x 36 with a crow's foot and a tick.
    if (start)
      return {18.0, 18.0, 45.0, 36.0, QStringLiteral("M0,18 Q 18,0 36,18 Q 18,36 0,18 M42,9 L42,27")};
    return {27.0, 18.0, 45.0, 36.0, QStringLiteral("M3,9 L3,27 M9,18 Q27,0 45,18 Q27,36 9,18")};
  case C::ZeroOrMore:
    // A box of 57 x 36 with a crow's foot and a ring.
    if (start)
      return {18.0, 18.0, 57.0, 36.0, QStringLiteral("M0,18 Q18,0 36,18 Q18,36 0,18"), true, neo ? 45.5 : 48.0,
              18.0, 6.0};
    return {39.0, 18.0, 57.0, 36.0, QStringLiteral("M21,18 Q39,0 57,18 Q39,36 21,18"), true, neo ? 11.0 : 9.0,
            18.0, 6.0};
  case C::MdParent:
    return {};
  }
  return {};
}

// A measured text at its origin, where erBox places it. An HTML text fills
// the box it was measured in; an SVG one reports the box it has once laid out,
// which is also where its glyphs go.
er::ErSceneText text(const er::ErTextMetrics& metrics, const QPointF& origin, bool htmlLabels) {
  er::ErSceneText result;
  result.document = metrics.document;
  result.style = metrics.style;
  result.svgText = !htmlLabels;
  if (htmlLabels) {
    result.rect = metrics.box.translated(origin);
  } else {
    result.rect = er::measureErSvgTextDrawnBox(metrics.document, metrics.style).translated(origin);
  }
  result.textBox = result.rect;
  return result;
}

// What the scene style paints an entity with, where the document says no
// more: the table, its rows and its lines from the theme's own colors.
er::ErEntityPaint themeEntityPaint(const er::ErSceneStyle& sceneStyle, bool attributeless) {
  er::ErEntityPaint paint;
  paint.table.fill = sceneStyle.nodeFill;
  paint.table.stroke = sceneStyle.nodeBorder;
  paint.table.strokeWidth = attributeless ? sceneStyle.boxStrokeWidth : sceneStyle.tableStrokeWidth;
  paint.oddRows = paint.evenRows = paint.dividers = paint.table;
  paint.dividers.strokeWidth = sceneStyle.tableStrokeWidth;
  paint.oddRows.fill = sceneStyle.rowOddFill;
  paint.evenRows.fill = sceneStyle.rowEvenFill;
  return paint;
}

er::ErRelationshipPaint themeRelationshipPaint(const er::ErSceneStyle& sceneStyle) {
  er::ErRelationshipPaint paint;
  paint.line.stroke = sceneStyle.lineColor;
  paint.line.strokeWidth = sceneStyle.lineStrokeWidth;
  paint.labelBackground =
      sceneStyle.relationshipHtmlLabels ? sceneStyle.labelBackground : sceneStyle.svgLabelBackground;
  return paint;
}

// The polygon rough.js draws a divider by: the line, a hundredth of a pixel
// thick, as lineToPolygon makes it.
QVector<QPointF> dividerPolygon(const QLineF& line) {
  constexpr qreal thickness = 0.0001;
  if (line.x1() == line.x2())
    return {{line.x1() - thickness / 2.0, line.y1()},
            {line.x1() + thickness / 2.0, line.y1()},
            {line.x2() + thickness / 2.0, line.y2()},
            {line.x2() - thickness / 2.0, line.y2()}};
  return {{line.x1(), line.y1() - thickness / 2.0},
          {line.x1(), line.y1() + thickness / 2.0},
          {line.x2(), line.y2() + thickness / 2.0},
          {line.x2(), line.y2() - thickness / 2.0}};
}

// The line through the middle of a drawn divider, the long way of the box it
// takes.
QLineF dividerLine(const QRectF& bounds) {
  return bounds.width() >= bounds.height()
             ? QLineF(bounds.left(), bounds.center().y(), bounds.right(), bounds.center().y())
             : QLineF(bounds.center().x(), bounds.top(), bounds.center().x(), bounds.bottom());
}

// The table of an entity, as erBox lays it out: the name centered over rows of
// four cells, a column as wide as its widest text plus the padding, and the
// lines between the name and the rows and between the columns.
//
// `centre` is where dagre put the entity. In the classic and the neo look that
// is the centre of the rectangle erBox lays out; under the handDrawn look
// rough.js draws the rectangle, dagre lays the entity out by the box the
// drawing takes, and the rectangle lies off that centre, which `drawn` says.
// `textStyle` restyles the texts, for the copy drawn behind the shapes.
er::ErSceneEntity buildEntity(const er::ErPlacementEntity& placed, const er::ErLayoutEntityInput& source,
                              const er::ErTableMetrics& table, const er::ErSceneStyle& sceneStyle,
                              const er::ErEntityPaint& paint, const er::ErEntityTextStyle* textStyle,
                              const er::ErRoughShape* drawn, const er::ErRoughEntity* rough) {
  er::ErSceneEntity entity;
  entity.id = placed.id;
  entity.link = source.link;
  entity.tooltip = source.tooltip;
  entity.table = paint.table;
  entity.oddRows = paint.oddRows;
  entity.evenRows = paint.evenRows;
  entity.dividerPaint = paint.dividers;
  entity.filter = paint.filter;

  const qreal width = table.size.width();
  const qreal height = table.size.height();
  // erBox draws about the rectangle's own centre, which is where dagre put
  // the entity. Under the handDrawn look dagre lays the entity out by the box
  // of what was drawn, whose centre that is not.
  const QPointF centre = placed.center;
  entity.centre = centre;
  if (drawn) {
    entity.bounds = drawn->bounds.translated(centre);
    entity.rough.emplace();
    entity.rough->table = er::roughTranslated(*drawn, centre);
  } else {
    entity.bounds = QRectF(QPointF(centre.x() - width / 2.0, centre.y() - height / 2.0), table.size);
  }
  const qreal x0 = centre.x() - width / 2.0;
  const qreal y0 = centre.y() - height / 2.0;
  const auto drawText = [&](const er::ErTextMetrics& metrics, const QPointF& origin, const er::ErTextStyle& style) {
    er::ErSceneText result = text(metrics, origin, sceneStyle.htmlLabels);
    result.style = style;
    return result;
  };
  // The name, centered across the table.
  entity.name = drawText(table.name, QPointF(x0 + (width - table.name.size().width()) / 2.0, y0 + table.nameTop),
                         textStyle ? textStyle->name : table.name.style);
  entity.textColor = entity.name.style.color;

  if (table.rows.isEmpty())
    return entity;
  entity.nameDividerPasses = 2;

  const qreal bodyTop = y0 + table.nameHeight;
  const qreal textLeft[4] = {x0 + table.padding / 2.0, x0 + table.padding / 2.0 + table.typeWidth,
                             x0 + table.padding / 2.0 + table.typeWidth + table.nameWidth,
                             x0 + table.padding / 2.0 + table.typeWidth + table.nameWidth + table.keysWidth};
  const qreal localX = -width / 2.0, localY = -height / 2.0;
  for (qsizetype index = 0; index < table.rows.size(); ++index) {
    const er::ErTableRowMetrics& metrics = table.rows.at(index);
    const qreal top = bodyTop + metrics.top;
    er::ErSceneRow row;
    // erBox numbers the rows from 1, after the name, and paints every second
    // one with the other color.
    const bool odd = index % 2 == 0;
    row.paint = odd ? paint.oddRows : paint.evenRows;
    if (entity.rough) {
      // rough.js draws the row about the rectangle's own centre.
      const QRectF local(localX, table.nameHeight + localY + metrics.top, width, metrics.height);
      entity.rough->rows.append(
          er::roughTranslated(er::roughRectangle(local, odd ? rough->oddRows : rough->evenRows), centre));
      row.rect = entity.rough->rows.last().bounds;
    } else {
      row.rect = QRectF(x0, top, width, metrics.height);
    }
    const qreal textTop = top + table.textPadding / 2.0;
    // A copy behind the shapes sets its texts as its own labels are set; an
    // entity's cells each keep the style they were measured in.
    const auto styleOf = [&](er::ErTextKind kind, const er::ErTextStyle& measured) {
      return textStyle ? textStyle->of(kind) : measured;
    };
    row.type = drawText(metrics.type, QPointF(textLeft[0], textTop), styleOf(er::ErTextKind::Type, metrics.type.style));
    row.name = drawText(metrics.name, QPointF(textLeft[1], textTop),
                        styleOf(er::ErTextKind::AttributeName, metrics.name.style));
    row.keys = drawText(metrics.keys, QPointF(textLeft[2], textTop), styleOf(er::ErTextKind::Keys, metrics.keys.style));
    row.comment = drawText(metrics.comment, QPointF(textLeft[3], textTop),
                           styleOf(er::ErTextKind::Comment, metrics.comment.style));
    entity.rows.append(std::move(row));
  }

  // The dividers, in the order erBox appends them, each in the coordinates it
  // draws them in: the line under the name, the one after the type column,
  // and then one after each column beside a column with a width.
  const qreal localBottom = localY + height;
  const qreal dividerTop = table.nameHeight + localY;
  const qreal columns[3] = {table.typeWidth, table.typeWidth + table.nameWidth,
                            table.typeWidth + table.nameWidth + table.keysWidth};
  QVector<QLineF> lines{QLineF(localX, dividerTop, localX + width, dividerTop),
                        QLineF(localX + columns[0], dividerTop, localX + columns[0], localBottom)};
  if (table.keysWidth > 0.0)
    lines.append(QLineF(localX + columns[1], dividerTop, localX + columns[1], localBottom));
  if (table.commentWidth > 0.0)
    lines.append(QLineF(localX + columns[2], dividerTop, localX + columns[2], localBottom));
  for (const QLineF& line : lines) {
    if (!entity.rough) {
      entity.dividers.append(line.translated(centre));
      continue;
    }
    entity.rough->dividers.append(er::roughTranslated(er::roughPolygon(dividerPolygon(line), rough->table), centre));
    entity.dividers.append(dividerLine(entity.rough->dividers.last().bounds));
  }
  return entity;
}

QJsonObject entityJson(const er::ErSceneEntity& entity) {
  QJsonObject object;
  object[QStringLiteral("id")] = entity.id;
  object[QStringLiteral("bounds")] = rectJson(entity.bounds);
  object[QStringLiteral("centre")] =
      QJsonObject{{QStringLiteral("x"), r3(entity.centre.x())}, {QStringLiteral("y"), r3(entity.centre.y())}};
  object[QStringLiteral("name")] = textJson(entity.name);
  QJsonArray rowsArray;
  for (const er::ErSceneRow& row : entity.rows) {
    QJsonObject rowObject;
    rowObject[QStringLiteral("rect")] = rectJson(row.rect);
    rowObject[QStringLiteral("type")] = textJson(row.type);
    rowObject[QStringLiteral("name")] = textJson(row.name);
    rowObject[QStringLiteral("keys")] = textJson(row.keys);
    rowObject[QStringLiteral("comment")] = textJson(row.comment);
    rowObject[QStringLiteral("fill")] = row.paint.fill;
    rowObject[QStringLiteral("stroke")] = row.paint.stroke;
    rowObject[QStringLiteral("strokeWidth")] = row.paint.strokeWidth;
    rowObject[QStringLiteral("strokeDasharray")] = row.paint.strokeDasharray;
    rowsArray.append(rowObject);
  }
  object[QStringLiteral("rows")] = rowsArray;
  QJsonArray dividersArray;
  for (const QLineF& divider : entity.dividers)
    dividersArray.append(lineJson(divider));
  object[QStringLiteral("dividers")] = dividersArray;
  object[QStringLiteral("fill")] = entity.table.fill;
  object[QStringLiteral("stroke")] = entity.table.stroke;
  object[QStringLiteral("strokeWidth")] = entity.table.strokeWidth;
  object[QStringLiteral("strokeDasharray")] = entity.table.strokeDasharray;
  object[QStringLiteral("textColor")] = entity.textColor;
  return object;
}

// The path a relationship is drawn as: the one the layout gave, or the one
// rough.js drew from it under the handDrawn look.
QString drawnPath(const er::ErSceneRelationship& relationship) {
  if (!relationship.rough || relationship.rough->drawable.sets.isEmpty())
    return relationship.path;
  return er::roughPathData(relationship.rough->drawable.sets.last());
}

} // namespace

QSize er::erRasterExtent(const QRectF& bounds, qreal padding) {
  const qreal viewBoxWidth = bounds.width() + 2.0 * padding;
  const qreal viewBoxHeight = bounds.height() + 2.0 * padding;
  if (!(viewBoxWidth > 0.0))
    return QSize();
  // The cap, and the height it leaves the element, each floored to a layout
  // cell: 189.387 caps at 189.375, and leaves 548.503 at 548.453.
  const qreal width = std::floor(viewBoxWidth * 64.0) / 64.0;
  const qreal height = std::floor(viewBoxHeight * width / viewBoxWidth * 64.0) / 64.0;
  return QSize(qRound(width), qRound(height));
}

QString er::erRelationshipKey(const QString& id, int loopIndex) {
  static const char* const parts[3] = {"1", "mid", "2"};
  return loopIndex < 0 ? id : id + QStringLiteral("-loop-") + QLatin1String(parts[std::clamp(loopIndex, 0, 2)]);
}

std::optional<er::ErMarker> er::erMarker(ErCardinality card, bool start, bool neo) {
  const er::ErMarker result = marker(card, start, neo);
  if (result.path.isEmpty())
    return std::nullopt;
  return result;
}

er::ErScene er::buildErScene(const ErLayoutInput& input, const ErPlacementResult& placement,
                             const ErLayoutMeasurements& measurements, ErSceneStyle sceneStyle,
                             const ErDocumentPaint& documentPaint) {
  er::ErScene result;
  result.style = std::move(sceneStyle);
  const er::ErSceneStyle& style = result.style;
  for (er::ErCardinality card : {er::ErCardinality::ExactlyOne, er::ErCardinality::ZeroOrOne,
                                 er::ErCardinality::OneOrMore, er::ErCardinality::ZeroOrMore}) {
    result.markerOrder.append({card, true});
    result.markerOrder.append({card, false});
  }
  for (const auto& [card, start] : result.markerOrder) {
    const QString key = er::erMarkerKey(card, start);
    result.markerPaint.insert(key, documentPaint.markers.value(key, style.marker));
  }

  QHash<QString, const er::ErLayoutEntityInput*> entitiesById;
  for (const er::ErLayoutEntityInput& entity : input.entities)
    entitiesById.insert(entity.id, &entity);
  QHash<QString, const er::ErLayoutRelationshipInput*> relationshipsById;
  for (const er::ErLayoutRelationshipInput& relationship : input.relationships)
    relationshipsById.insert(relationship.id, &relationship);

  Canvas canvas;
  const auto addRough = [&canvas](const er::ErSceneRough& rough) {
    canvas.add(rough.table.bounds);
    for (const er::ErRoughShape& row : rough.rows)
      canvas.add(row.bounds);
    for (const er::ErRoughShape& divider : rough.dividers)
      canvas.add(divider.bounds);
  };
  // The copies behind the drawn shapes come first, as erBox draws each of them
  // before the shapes of its entity.
  for (const er::ErPlacementEntity& placed : placement.entities) {
    if (!style.handDrawn)
      break;
    const er::ErLayoutEntityInput* source = entitiesById.value(placed.id);
    if (!source)
      continue;
    const auto measured = measurements.tables.constFind(placed.id);
    const er::ErTableMetrics table =
        measured == measurements.tables.cend()
            ? er::measureErTable(*source,
                                 er::ErEntityTextStyle{style.text, style.text, style.text, style.text, style.text},
                                 style.htmlLabels, style.scalePaddings, er::ErMeasureOptions{style.text})
            : measured.value();
    const auto texts = documentPaint.backgroundTexts.constFind(placed.id);
    const er::ErEntityTextStyle* const textStyle =
        texts == documentPaint.backgroundTexts.cend() ? nullptr : &texts.value();
    er::ErSceneEntity background = buildEntity(placed, *source, table, style,
                                               documentPaint.backgrounds.value(placed.id), textStyle, nullptr, nullptr);
    background.id = placed.id + QStringLiteral("-background");
    background.nameDividerPasses = 1;
    canvas.add(background.bounds);
    result.backgrounds.append(std::move(background));
  }

  for (const er::ErPlacementEntity& placed : placement.entities) {
    const er::ErLayoutEntityInput* source = entitiesById.value(placed.id);
    if (!source)
      continue;
    const auto measured = measurements.tables.constFind(placed.id);
    // A caller that laid the entities out without measuring them for this
    // input gets erBox's default paddings.
    const er::ErTableMetrics table =
        measured == measurements.tables.cend()
            ? er::measureErTable(*source,
                                 er::ErEntityTextStyle{style.text, style.text, style.text, style.text, style.text},
                                 style.htmlLabels, style.scalePaddings, er::ErMeasureOptions{style.text})
            : measured.value();
    const bool attributeless = source->attributes.isEmpty();
    const er::ErEntityPaint paint = documentPaint.entities.contains(placed.id) ? documentPaint.entities.value(placed.id)
                                                                               : themeEntityPaint(style, attributeless);
    // Under the handDrawn look the entity was laid out by the box of its
    // drawn rectangle, which the measurement keeps.
    const auto drawn = measurements.rough.constFind(placed.id);
    const auto options = style.rough.constFind(placed.id);
    er::ErSceneEntity entity = buildEntity(placed, *source, table, style, paint, nullptr,
                                           drawn == measurements.rough.cend() ? nullptr : &drawn.value(),
                                           options == style.rough.cend() ? nullptr : &options.value());
    canvas.add(entity.bounds);
    if (entity.rough)
      addRough(*entity.rough);
    if (!style.htmlLabels) {
      // An SVG text is an element of the document whatever it holds: the
      // empty one of an absent cell still ends the canvas at its origin.
      canvas.add(entity.name.rect.topLeft());
      for (const er::ErSceneRow& row : entity.rows)
        for (const er::ErSceneText* cell : {&row.type, &row.name, &row.keys, &row.comment})
          canvas.add(cell->rect.topLeft());
    }
    // The whole table is the link region; the tooltip doubles as the label.
    if (!entity.link.isEmpty()) {
      InteractionRegion region;
      region.bounds = entity.bounds;
      region.href = entity.link;
      region.toolTip = entity.tooltip;
      region.accessibleLabel = entity.tooltip;
      result.interactions.append(std::move(region));
    }
    result.entities.append(std::move(entity));
  }

  for (const er::ErPlacementRelationship& placed : placement.relationships) {
    er::ErSceneRelationship relationship;
    relationship.id = placed.id;
    relationship.path = placed.path;
    relationship.loopIndex = placed.loopIndex;
    // A path of a loop is painted by itself; one the document's style says
    // nothing of takes its relationship's paint, and failing that the theme's.
    const QString pathKey = er::erRelationshipKey(placed.id, placed.loopIndex);
    const bool ownPaint = documentPaint.relationships.contains(pathKey);
    const bool relationshipPaint = documentPaint.relationships.contains(placed.id);
    er::ErRelationshipPaint paint = ownPaint            ? documentPaint.relationships.value(pathKey)
                                    : relationshipPaint ? documentPaint.relationships.value(placed.id)
                                                        : themeRelationshipPaint(style);
    relationship.line = paint.line;
    relationship.labelBackground = paint.labelBackground;
    if (const er::ErLayoutRelationshipInput* source = relationshipsById.value(placed.id)) {
      relationship.identifying = source->identifying;
      // A relationship the document's style says nothing of is dashed where it
      // is not identifying: 8 on, 8 off.
      if (!ownPaint && !relationshipPaint && !source->identifying)
        relationship.line.strokeDasharray = QStringLiteral("8 8");
      relationship.label = source->label;
      if (placed.markerStart)
        relationship.cardA = source->cardA;
      if (placed.markerEnd)
        relationship.cardB = source->cardB;
      if (!relationship.label.isEmpty() && placed.labelPosition) {
        // The label where dagre placed it, in the box measured for it.
        const er::ErRelationshipLabelMetrics label =
            measurements.labels.contains(placed.id)
                ? measurements.labels.value(placed.id)
                : er::measureErRelationshipLabel(*source, style.relationshipText, style.relationshipHtmlLabels,
                                                 style.text.fontSize);
        relationship.labelText.document = label.document;
        relationship.labelText.style = label.style;
        relationship.labelText.svgText = !style.relationshipHtmlLabels;
        relationship.labelText.rect = label.drawnBox.translated(*placed.labelPosition);
        relationship.labelText.textBox = label.textBox.translated(*placed.labelPosition);
        canvas.add(relationship.labelText.rect);
      }
    }
    // Under the handDrawn look rough.js draws the path.
    if (style.handDrawn)
      relationship.rough = er::roughRelationship(placed.path, style.handDrawnSeed);
    canvas.add(relationship.rough ? relationship.rough->bounds : scene::parseSvgPath(relationship.path).boundingRect());
    result.relationships.append(std::move(relationship));
  }
  // The invisible points of a loop stay in the drawing as 0.1 x 0.1 rectangles.
  for (const QRectF& mark : placement.loopMarks)
    canvas.add(mark);
  result.bounds = canvas.bounds();
  return result;
}

QJsonObject er::ErScene::toJsonObject() const {
  QJsonObject out;
  out[QStringLiteral("role")] = role;
  out[QStringLiteral("ariaRoleDescription")] = ariaRoleDescription;
  out[QStringLiteral("bounds")] = rectJson(bounds);

  QJsonArray entitiesArray;
  for (const er::ErSceneEntity& entity : entities)
    entitiesArray.append(entityJson(entity));
  out[QStringLiteral("entities")] = entitiesArray;

  QJsonArray relationshipsArray;
  for (const er::ErSceneRelationship& relationship : relationships) {
    QJsonObject object;
    object[QStringLiteral("id")] = relationship.id;
    object[QStringLiteral("path")] = drawnPath(relationship);
    object[QStringLiteral("loopIndex")] = relationship.loopIndex;
    object[QStringLiteral("cardA")] = relationship.cardA ? QJsonValue(cardName(*relationship.cardA)) : QJsonValue();
    object[QStringLiteral("cardB")] = relationship.cardB ? QJsonValue(cardName(*relationship.cardB)) : QJsonValue();
    object[QStringLiteral("identifying")] = relationship.identifying;
    object[QStringLiteral("stroke")] = relationship.line.stroke;
    object[QStringLiteral("strokeWidth")] = QString::number(relationship.line.strokeWidth) + QStringLiteral("px");
    object[QStringLiteral("strokeDasharray")] = relationship.line.strokeDasharray;
    if (!relationship.labelText.document.text.isEmpty()) {
      object[QStringLiteral("label")] = relationship.label;
      object[QStringLiteral("labelRect")] = rectJson(relationship.labelText.rect);
      object[QStringLiteral("labelTextColor")] = relationship.labelText.style.color;
      object[QStringLiteral("labelBackground")] = relationship.labelBackground.name(QColor::HexArgb);
    }
    relationshipsArray.append(std::move(object));
  }
  out[QStringLiteral("relationships")] = relationshipsArray;

  // Only the handDrawn look draws copies behind the entities.
  if (!backgrounds.isEmpty()) {
    QJsonArray backgroundsArray;
    for (const er::ErSceneEntity& background : backgrounds)
      backgroundsArray.append(entityJson(background));
    out[QStringLiteral("backgrounds")] = backgroundsArray;
  }

  QJsonArray markersArray;
  for (const auto& [card, start] : markerOrder) {
    const QString name = er::erMarkerKey(card, start);
    const er::ErMarkerPaint& paint = markerPaint.value(name);
    QJsonObject object;
    object[QStringLiteral("id")] = name;
    object[QStringLiteral("strokeWidth")] = paint.strokeWidth;
    object[QStringLiteral("stroke")] = paint.stroke;
    object[QStringLiteral("ringFill")] = paint.ringFill;
    object[QStringLiteral("ringStroke")] = paint.ringStroke;
    markersArray.append(std::move(object));
  }
  out[QStringLiteral("markers")] = markersArray;
  return out;
}

muffin::mermaid::SvgMarkerProjection er::ErScene::svgMarkerProjection() const {
  SvgMarkerProjection projection;
  // One definition per cardinality and end, in the order Mermaid's erMarkerDef
  // writes them.
  for (const auto& [card, start] : markerOrder) {
    const er::ErMarker marker = *er::erMarker(card, start, style.neo);
    const QString key = er::erMarkerKey(card, start);
    const er::ErMarkerPaint& paint = markerPaint.value(key);
    SvgMarkerDefinition definition;
    definition.key = key;
    definition.idSuffix = QStringLiteral("_er-") + key;
    definition.refX = marker.refX;
    definition.refY = marker.refY;
    definition.markerWidth = marker.width;
    definition.markerHeight = marker.height;
    // The neo look's markers are sized in user units, and each of their
    // children carries the theme's stroke width; the others are scaled by the
    // line's own, and stroke 1px whatever the line.
    if (style.neo) {
      definition.markerUnits = QStringLiteral("userSpaceOnUse");
    }
    if (marker.hasRing) {
      // The attribute is the one erMarkerDef writes: white, or the background
      // under the neo look. It is the stylesheet that leaves the ring unfilled
      // (`.marker { fill: none !important }`), and the exported SVG carries
      // that stylesheet, as upstream's does.
      SvgMarkerChild ring;
      ring.tag = QStringLiteral("circle");
      ring.cx = marker.ringX;
      ring.cy = marker.ringY;
      ring.radius = marker.ringRadius;
      ring.fill = paint.ringFill;
      ring.stroke = paint.ringStroke;
      ring.strokeWidth = style.neo ? QString::number(paint.strokeWidth) : QString();
      definition.children.append(std::move(ring));
    }
    SvgMarkerChild child;
    child.tag = QStringLiteral("path");
    child.path = marker.path;
    child.fill = QStringLiteral("none");
    child.stroke = paint.stroke;
    child.strokeWidth = style.neo ? QString::number(paint.strokeWidth) : QString();
    definition.children.append(std::move(child));
    projection.definitions.append(std::move(definition));
  }
  for (const er::ErSceneRelationship& relationship : relationships) {
    SvgMarkerEdge edge;
    edge.id = relationship.id;
    edge.cssClass = QStringLiteral("relationshipLine");
    edge.path = drawnPath(relationship);
    // The key of the marker's definition; the exporter writes its id.
    if (relationship.cardA)
      edge.markerStart = er::erMarkerKey(*relationship.cardA, true);
    if (relationship.cardB)
      edge.markerEnd = er::erMarkerKey(*relationship.cardB, false);
    edge.stroke = relationship.line.stroke;
    edge.strokeWidth = QString::number(relationship.line.strokeWidth);
    // Chrome computes stroke-dasharray "8px, 8px" on .edge-pattern-dashed (the
    // er stylesheet's own 8,8 rule overrides the common sheet's `3`).
    if (!relationship.line.strokeDasharray.isEmpty())
      edge.strokeDasharray = relationship.line.strokeDasharray;
    projection.edges.append(std::move(edge));
  }
  return projection;
}
