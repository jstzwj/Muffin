#include "mermaid/erdiagram/ErLayout.h"

#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowchartLayout.h"

#include <algorithm>

namespace er = muffin::mermaid::er;
namespace flowchart = muffin::mermaid::flowchart;

// erDiagram's projection into the dagre pipeline, and erBox's measurement of
// its texts and tables. Per IMPLEMENTATION_SPEC.md this .cpp defines its free
// functions with fully-qualified names and keeps its helpers in an anonymous
// namespace.

namespace {

using muffin::mermaid::er::ErLayoutEntityInput;
using muffin::mermaid::er::ErLayoutRelationshipInput;
using muffin::mermaid::er::ErMeasureOptions;
using muffin::mermaid::er::ErTableMetrics;
using muffin::mermaid::er::ErTextMetrics;
using muffin::mermaid::er::ErEntityTextStyle;
using muffin::mermaid::er::ErTextStyle;

flowchart::FlowTextOptions textOptions(const ErTextStyle& style, qreal maximumLineWidth) {
  flowchart::FlowTextOptions options;
  options.fontFamily = style.fontFamily;
  options.fontPixelSize = style.fontSize;
  options.fontWeight = style.fontWeight;
  options.lineHeight = style.lineHeight();
  options.maximumLineWidth = maximumLineWidth;
  return options;
}

// The box of an SVG text. Where a run cannot be shaped over its font's
// tables, the text keeps the box of the fonts Qt falls back to.
QRectF svgTextBox(const flowchart::FlowLabelDocument& document, const ErTextStyle& style) {
  const std::optional<QRectF> box = flowchart::measureSvgTextBox(document, style.fontFamily, style.fontSize);
  if (box)
    return *box;
  return flowchart::measureFlowSvgTextBounds(document, style.fontFamily, style.fontSize);
}

// One style for every text of an entity, where nothing tells them apart.
ErEntityTextStyle uniformStyles(const ErTextStyle& style) {
  ErEntityTextStyle styles;
  styles.name = styles.type = styles.attributeName = styles.keys = styles.comment = style;
  return styles;
}

// A style's weight and italics are the text's own: erBox writes them on every
// tspan of a styled label.
void applyStyle(flowchart::FlowLabelDocument& document, const ErTextStyle& style) {
  document.baseWeight = style.fontWeight;
  document.baseStyle = style.italic ? QFont::StyleItalic : QFont::StyleNormal;
}

} // namespace

muffin::mermaid::er::ErLayoutInput muffin::mermaid::er::buildErLayoutInput(const ErDiagramData& data) {
  ErLayoutInput input;
  input.direction = data.direction;
  for (const ErEntity& entity : data.entities) {
    ErLayoutEntityInput out;
    out.id = entity.id;
    out.name = entity.label();
    out.attributes = entity.attributes;
    out.cssClasses = entity.cssClasses;
    out.styles = entity.styles;
    out.link = entity.link;
    out.linkTarget = entity.linkTarget;
    out.tooltip = entity.tooltip;
    input.entities.append(std::move(out));
  }
  qsizetype counter = 0;
  for (const ErRelationship& rel : data.relationships) {
    ErLayoutRelationshipInput out;
    out.id = QStringLiteral("id_%1_%2_%3").arg(rel.entityA, rel.entityB).arg(counter++);
    out.entityA = rel.entityA;
    out.entityB = rel.entityB;
    out.cardA = rel.cardA;
    out.cardB = rel.cardB;
    out.identifying = rel.identifying;
    // Mermaid puts back the entities encodeEntities hid when it draws a label.
    out.label = decodeMermaidEntities(rel.label);
    input.relationships.append(std::move(out));
  }
  return input;
}

muffin::mermaid::er::ErTextMetrics muffin::mermaid::er::measureErText(const QString& text, const ErTextStyle& style,
                                                                      bool htmlLabels, qreal maximumLineWidth) {
  ErTextMetrics result;
  result.style = style;
  if (htmlLabels) {
    const flowchart::FlowTextOptions options = textOptions(style, maximumLineWidth);
    result.document = flowchart::flowLabelDocument(text, QStringLiteral("markdown"), options);
    applyStyle(result.document, style);
    result.box = QRectF(QPointF(0.0, 0.0), flowchart::measureLabel(text, QStringLiteral("markdown"), options));
  } else {
    result.document = flowchart::parseFlowSvgLabel(text, QStringLiteral("markdown"));
    applyStyle(result.document, style);
    // An SVG text is broken where createFormattedText breaks one, into lines
    // 1.1em apart, and erBox takes the box it has as it is appended.
    if (maximumLineWidth > 0.0) {
      result.document = flowchart::wrapSvgText(result.document, style.fontFamily, style.fontSize, maximumLineWidth);
      result.document.visualLineAdvance = flowchart::flowSvgFormattedTextLineStep(style.fontSize);
    }
    result.box = svgTextBox(result.document, style);
  }
  return result;
}

// The box an SVG text is drawn in, from its origin.
QRectF muffin::mermaid::er::measureErSvgTextDrawnBox(const flowchart::FlowLabelDocument& document,
                                                     const ErTextStyle& style) {
  return svgTextBox(document, style);
}

muffin::mermaid::er::ErRelationshipLabelMetrics
muffin::mermaid::er::measureErRelationshipLabel(const ErLayoutRelationshipInput& relationship, const ErTextStyle& style,
                                                bool htmlLabels, qreal rootSize) {
  ErRelationshipLabelMetrics result;
  result.style = style;
  if (htmlLabels) {
    // createText's width: an edge label wraps at 200.
    const flowchart::FlowTextOptions options = textOptions(style, 200.0);
    result.document = flowchart::flowLabelDocument(relationship.label, QStringLiteral("markdown"), options);
    applyStyle(result.document, style);
    result.size = flowchart::measureLabel(relationship.label, QStringLiteral("markdown"), options);
    result.drawnBox = QRectF(QPointF(-result.size.width() / 2.0, -result.size.height() / 2.0), result.size);
    result.textBox = result.drawnBox;
    return result;
  }
  // An SVG edge label is measured before the stylesheet's font size reaches
  // it: it wraps at createText's 200 and its box is its text's box at the root
  // size with 2px on every side. The text itself is drawn at its own size.
  result.document = flowchart::parseFlowSvgLabel(relationship.label, QStringLiteral("markdown"));
  applyStyle(result.document, style);
  ErTextStyle rootStyle = style;
  rootStyle.fontSize = rootSize;
  result.document = flowchart::wrapSvgText(result.document, style.fontFamily, rootSize, 200.0);
  result.document.visualLineAdvance = flowchart::flowSvgFormattedTextLineStep(style.fontSize);
  // Each line is anchored at its middle, and so starts half its advance
  // back; the box is the union of the lines' boxes at the root size, with 2px
  // on every side.
  const auto anchored = [](const flowchart::SvgTextLayout& layout) {
    QRectF unioned;
    for (qsizetype index = 0; index < layout.lines.size(); ++index)
      unioned = index == 0 ? layout.lines.at(index).translated(-layout.lineCells.at(index) / 2.0, 0.0)
                           : unioned.united(layout.lines.at(index).translated(-layout.lineCells.at(index) / 2.0, 0.0));
    return unioned;
  };
  const flowchart::SvgTextLayout root =
      flowchart::layoutSvgText(result.document, style.fontFamily, rootSize)
          .value_or(flowchart::SvgTextLayout{svgTextBox(result.document, rootStyle), {}, {}});
  const QRectF rootBox = anchored(root);
  result.size = QSizeF(std::max<qreal>(30.0, rootBox.width() + 4.0), rootBox.height() + 4.0);
  // At its own font size the label group is moved so that the middle of the
  // text's box is the point the label is at.
  const flowchart::SvgTextLayout own =
      flowchart::layoutSvgText(result.document, style.fontFamily, style.fontSize)
          .value_or(flowchart::SvgTextLayout{svgTextBox(result.document, style), {}, {}});
  const QRectF ownBox = anchored(own);
  const QPointF group = -ownBox.center();
  result.textBox = own.box.translated(group);
  result.drawnBox = QRectF(group + rootBox.topLeft() - QPointF(2.0, 2.0), result.size);
  return result;
}

muffin::mermaid::er::ErTableMetrics muffin::mermaid::er::measureErTable(const ErLayoutEntityInput& entity,
                                                                        const ErEntityTextStyle& styles,
                                                                        bool htmlLabels, bool scalePaddings,
                                                                        const ErMeasureOptions& options) {
  ErTableMetrics table;
  if (entity.attributes.isEmpty()) {
    // erBox's drawRect branch. Its label is never wider than the minimum
    // entity width where the name fits in that with its padding, and a longer
    // name wraps at the wrapping width.
    const qreal unwrapped = measureErText(entity.name, options.siteStyle, htmlLabels, 0.0).size().width();
    const qreal limit = unwrapped + 2.0 * options.diagramPadding < options.minEntityWidth ? options.minEntityWidth
                                                                                          : options.wrappingWidth;
    table.name = measureErText(entity.name, styles.name, htmlLabels, limit);
    // The name centered, a padding on each side and a padding and a half
    // above and below, and never narrower than the minimum.
    const qreal height = table.name.size().height() + 2.0 * options.diagramPadding * 1.5;
    table.padding = options.diagramPadding;
    table.nameTop = (height - table.name.size().height()) / 2.0;
    table.nameHeight = height;
    table.size =
        QSizeF(std::max(table.name.size().width() + 2.0 * options.diagramPadding, options.minEntityWidth), height);
    return table;
  }

  // With attributes erBox scales both paddings by 1.25 unless `htmlLabels`
  // is true.
  const qreal scale = scalePaddings ? 1.25 : 1.0;
  table.padding = options.diagramPadding * scale;
  table.textPadding = options.entityPadding * scale;
  table.name = measureErText(entity.name, styles.name, htmlLabels, 0.0);
  qreal rowsHeight = 0.0;
  for (const ErAttribute& attribute : entity.attributes) {
    er::ErTableRowMetrics row;
    row.type = measureErText(attribute.attributeType, styles.of(ErTextKind::Type), htmlLabels, 0.0);
    row.name = measureErText(attribute.attributeName, styles.of(ErTextKind::AttributeName), htmlLabels, 0.0);
    row.keysText = attribute.keys.join(QLatin1Char(','));
    row.keys = measureErText(row.keysText, styles.of(ErTextKind::Keys), htmlLabels, 0.0);
    row.comment = measureErText(attribute.comment, styles.of(ErTextKind::Comment), htmlLabels, 0.0);
    row.height = std::max({row.type.size().height(), row.name.size().height(), row.keys.size().height(),
                           row.comment.size().height()}) +
                 table.textPadding;
    row.top = rowsHeight;
    rowsHeight += row.height;
    table.typeWidth = std::max(table.typeWidth, row.type.size().width() + table.padding);
    table.nameWidth = std::max(table.nameWidth, row.name.size().width() + table.padding);
    table.keysWidth = std::max(table.keysWidth, row.keys.size().width() + table.padding);
    table.commentWidth = std::max(table.commentWidth, row.comment.size().width() + table.padding);
    table.rows.append(std::move(row));
  }
  // A column with no text in any row is left out.
  int columns = 4;
  if (table.keysWidth <= table.padding) {
    table.keysWidth = 0.0;
    --columns;
  }
  if (table.commentWidth <= table.padding) {
    table.commentWidth = 0.0;
    --columns;
  }
  // A name wider than the columns widens them evenly.
  const qreal difference = table.name.size().width() + 2.0 * table.padding -
                           (table.typeWidth + table.nameWidth + table.keysWidth + table.commentWidth);
  if (difference > 0.0) {
    table.typeWidth += difference / columns;
    table.nameWidth += difference / columns;
    if (table.keysWidth > 0.0)
      table.keysWidth += difference / columns;
    if (table.commentWidth > 0.0)
      table.commentWidth += difference / columns;
  }
  table.nameTop = table.textPadding / 2.0;
  table.nameHeight = table.name.size().height() + table.textPadding;
  table.size =
      QSizeF(table.typeWidth + table.nameWidth + table.keysWidth + table.commentWidth, table.nameHeight + rowsHeight);
  return table;
}

muffin::mermaid::er::ErLayoutMeasurements muffin::mermaid::er::measureErLayoutInput(const ErLayoutInput& input,
                                                                                    const ErTextOptions& text,
                                                                                    ErMeasureOptions options) {
  ErLayoutMeasurements result;
  for (const ErLayoutEntityInput& entity : input.entities) {
    const ErTableMetrics table = measureErTable(entity, text.entities.value(entity.id, uniformStyles(text.table)),
                                                text.htmlLabels, text.scalePaddings, options);
    result.tables.insert(entity.id, table);
    result.entities.insert(entity.id, table.size);
    // Under the handDrawn look the rectangle is drawn by rough.js, and the
    // entity is laid out by the box the drawing takes.
    if (const auto rough = options.rough.constFind(entity.id); rough != options.rough.cend()) {
      ErRoughShape shape = er::roughRectangle(
          QRectF(QPointF(-table.size.width() / 2.0, -table.size.height() / 2.0), table.size), rough.value());
      result.entities.insert(entity.id, shape.bounds.size());
      result.rough.insert(entity.id, std::move(shape));
    }
  }
  for (const ErLayoutRelationshipInput& rel : input.relationships) {
    if (rel.label.isEmpty())
      continue;
    ErRelationshipLabelMetrics label =
        measureErRelationshipLabel(rel, text.relationship, text.relationshipHtmlLabels, text.table.fontSize);
    result.relationships.insert(rel.id, label.size);
    result.labels.insert(rel.id, std::move(label));
  }
  return result;
}

QString muffin::mermaid::er::erMarkerKey(ErCardinality card, bool start) {
  return erMarkerName(card) + (start ? QStringLiteral("Start") : QStringLiteral("End"));
}

QString muffin::mermaid::er::erMarkerName(ErCardinality card) {
  switch (card) {
  case ErCardinality::ExactlyOne:
    return QStringLiteral("onlyOne");
  case ErCardinality::ZeroOrOne:
    return QStringLiteral("zeroOrOne");
  case ErCardinality::OneOrMore:
    return QStringLiteral("oneOrMore");
  case ErCardinality::ZeroOrMore:
    return QStringLiteral("zeroOrMore");
  case ErCardinality::MdParent:
    return QString();
  }
  return QString();
}

muffin::mermaid::er::ErPlacementResult
muffin::mermaid::er::layoutErDiagramDagre(const ErLayoutInput& input, const ErLayoutMeasurements& measurements,
                                          qreal entitySpacing, qreal rankSpacing) {
  ErPlacementResult result;
  if (input.entities.isEmpty())
    return result;
  flowchart::FlowchartData projected;
  projected.direction = input.direction;
  for (const ErLayoutEntityInput& entity : input.entities) {
    flowchart::FlowVertex vertex;
    vertex.id = entity.id;
    vertex.text = entity.name;
    vertex.type = QStringLiteral("rect");
    projected.vertices.append(std::move(vertex));
  }
  for (const ErLayoutRelationshipInput& rel : input.relationships) {
    flowchart::FlowEdge edge;
    edge.id = rel.id;
    edge.start = rel.entityA;
    edge.end = rel.entityB;
    edge.text = rel.label;
    edge.labelType = QStringLiteral("markdown");
    projected.edges.append(std::move(edge));
  }
  flowchart::FlowLayoutOptions options;
  options.nodeSpacing = entitySpacing;
  options.rankSpacing = rankSpacing;
  options.nodePadding = 8.0;
  options.measuredEdgeLabels = measurements.relationships;
  const flowchart::FlowLayoutResult placed =
      flowchart::layoutFlowchartNodesDagre(projected, measurements.entities, options);
  // FlowLayoutNode.x/y are the node CENTER (per FlowchartLayout.h, and as
  // StateLayout.cpp consumes them), so they map directly to ErPlacementEntity.center.
  for (const flowchart::FlowLayoutNode& node : placed.nodes) {
    ErPlacementEntity entity;
    entity.id = node.id;
    entity.center = QPointF(node.x, node.y);
    entity.size = QSizeF(node.width, node.height);
    result.entities.append(std::move(entity));
  }
  // Mermaid draws the relationship paths in the order of the relationships,
  // a loop's three where the relationship stands.
  QVector<flowchart::FlowLayoutEdge> edges = placed.edges;
  QHash<QString, qsizetype> order;
  for (qsizetype index = 0; index < input.relationships.size(); ++index)
    order.insert(input.relationships.at(index).id, index);
  std::stable_sort(edges.begin(), edges.end(),
                   [&](const flowchart::FlowLayoutEdge& a, const flowchart::FlowLayoutEdge& b) {
                     return order.value(a.id) < order.value(b.id);
                   });
  for (const flowchart::FlowLayoutEdge& edge : edges) {
    // Mermaid leaves a 0.1 x 0.1 rectangle at each invisible point of a loop.
    for (const QPointF& mark : edge.loopMarks)
      result.loopMarks.append(QRectF(mark, QSizeF(0.1, 0.1)));
    if (edge.segments.isEmpty()) {
      ErPlacementRelationship relationship;
      relationship.id = edge.id;
      relationship.path = edge.path;
      relationship.points = edge.points;
      relationship.labelSize = edge.labelSize;
      if (edge.hasLabelPosition)
        relationship.labelPosition = QPointF(edge.labelX, edge.labelY);
      result.relationships.append(std::move(relationship));
      continue;
    }
    // A relationship from an entity to itself is routed through two invisible
    // points as three paths. Mermaid merges them into one loop for flowcharts
    // and state diagrams only; er diagrams draw all three.
    for (qsizetype index = 0; index < edge.segments.size(); ++index) {
      ErPlacementRelationship segment;
      segment.id = edge.id;
      segment.points = edge.segments.at(index);
      segment.path = flowchart::d3curve::pathForCurve(segment.points, options.curve);
      segment.loopIndex = static_cast<int>(index);
      segment.markerStart = index == 0;
      segment.markerEnd = index + 1 == edge.segments.size();
      // The label sits on the middle path.
      if (index == 1) {
        if (edge.hasLabelPosition)
          segment.labelPosition = QPointF(edge.labelX, edge.labelY);
        segment.labelSize = edge.labelSize;
      }
      result.relationships.append(std::move(segment));
    }
  }
  return result;
}
