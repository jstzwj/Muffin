// erDiagram layout projection. The er diagram is projected into the flowchart
// dagre pipeline exactly as StateLayout.cpp projects state nodes/edges: entities
// become flat flowchart vertices (no subgraphs, no clusters) and relationships
// become edges. See IMPLEMENTATION_SPEC.md §4 for the projection rules.
//
// Per CLAUDE.md / IMPLEMENTATION_SPEC.md this .cpp contains NO `namespace
// muffin {}` block (the lupdate convention, kept even though the module has no
// tr()). Functions are defined with fully-qualified `muffin::mermaid::er::`
// names; file-local helpers live in an anonymous namespace at file scope.

#include "mermaid/erdiagram/ErLayout.h"

#include "mermaid/flowchart/FlowchartLayout.h"

#include <QJsonArray>

#include <algorithm>

namespace {
// Entity table sizing constants (spec §4). The entity is laid out as a
// fixed-row table — a header row (entity name) plus one row per attribute — so
// the box height is deterministic from the row count and the line height, not
// derived from the multi-line text block (which would drift from the painter's
// fixed-row layout in ErScene).
constexpr qreal kEntityHorizontalPadding = 8.0;
constexpr qreal kEntityVerticalPadding = 6.0;
constexpr qreal kHeaderDividerBand = 1.0;

// ErCardinality → enum name string. Spec §3: cardinality serializes as the enum
// name ("ExactlyOne" etc.), matching ErDiagram::toJson. Used by the layout
// golden only.
QString erCardinalityName(muffin::mermaid::er::ErCardinality card) {
  switch (card) {
    case muffin::mermaid::er::ErCardinality::ExactlyOne:
      return QStringLiteral("ExactlyOne");
    case muffin::mermaid::er::ErCardinality::ZeroOrOne:
      return QStringLiteral("ZeroOrOne");
    case muffin::mermaid::er::ErCardinality::OneOrMore:
      return QStringLiteral("OneOrMore");
    case muffin::mermaid::er::ErCardinality::ZeroOrMore:
      return QStringLiteral("ZeroOrMore");
  }
  return QString{};
}

// Short key token placed in the formatted attribute line. Spec §4 uses the
// short PK/FK/UK form (NOT the enum name); ErScene re-parses these tokens back
// into ErSceneAttribute.keyType (which is "" / "PK" / "FK" / "UK").
QString erAttributeKeyShortName(muffin::mermaid::er::ErAttributeKeyType key) {
  switch (key) {
    case muffin::mermaid::er::ErAttributeKeyType::PrimaryKey:
      return QStringLiteral("PK");
    case muffin::mermaid::er::ErAttributeKeyType::ForeignKey:
      return QStringLiteral("FK");
    case muffin::mermaid::er::ErAttributeKeyType::UniqueKey:
      return QStringLiteral("UK");
    case muffin::mermaid::er::ErAttributeKeyType::None:
      break;
  }
  return QString{};
}

// Formats one attribute row for table sizing:
//   "<type> <name>" with an optional " (PK|FK|UK)" key marker and an optional
//   " (\"comment\")" comment marker. The two paren forms are disjoint — a key
// marker is a bare PK/FK/UK token, a comment is always double-quoted — so
// ErScene can re-split each line back into text / keyType / comment.
QString formatErAttributeLine(const muffin::mermaid::er::ErAttribute& attr) {
  QString line = attr.attributeType + QLatin1Char(' ') + attr.attributeName;
  const QString key = erAttributeKeyShortName(attr.keyType);
  if (!key.isEmpty()) line += QLatin1String(" (") + key + QLatin1Char(')');
  if (!attr.comment.isEmpty())
    line += QLatin1String(" (\"") + attr.comment + QLatin1String("\")");
  return line;
}

QJsonArray strings(const QStringList& values) {
  QJsonArray result;
  for (const QString& value : values) result.append(value);
  return result;
}
}  // namespace

muffin::mermaid::er::ErLayoutInput
muffin::mermaid::er::buildErLayoutInput(const ErDiagramData& data) {
  ErLayoutInput input;
  // erDb defaults direction to "TB"; the erDiagram grammar exposes no keyword
  // to change it, so this is the only value ever emitted.
  input.direction = QStringLiteral("TB");
  for (const ErEntity& entity : data.entities) {
    ErLayoutEntityInput out;
    out.id = entity.id;
    out.name = entity.name;
    for (const ErAttribute& attr : entity.attributes) {
      out.attributeLines.append(formatErAttributeLine(attr));
      // Placeholder — upstream populates compiled CSS per row; deferred (spec §9).
      out.attributeStyles.append(QString{});
    }
    input.entities.append(std::move(out));
  }
  for (const ErRelationship& rel : data.relationships) {
    ErLayoutRelationshipInput out;
    out.id = rel.id;
    out.entityA = rel.entityA;
    out.entityB = rel.entityB;
    out.cardA = rel.cardA;
    out.cardB = rel.cardB;
    out.identifying = rel.identifying;
    out.label = rel.label;
    out.roleA = rel.roleA;
    out.roleB = rel.roleB;
    input.relationships.append(std::move(out));
  }
  return input;
}

muffin::mermaid::er::ErLayoutMeasurements
muffin::mermaid::er::measureErLayoutInput(const ErLayoutInput& input,
                                          QString fontFamily, qreal fontSize,
                                          qreal minEntityWidth, qreal minEntityHeight) {
  ErLayoutMeasurements result;
  flowchart::FlowTextOptions options;
  options.fontFamily = std::move(fontFamily);
  options.fontPixelSize = fontSize;
  options.lineHeight = fontSize * 1.5;
  options.horizontalPadding = 16.0;
  options.verticalPadding = 16.0;
  for (const ErLayoutEntityInput& entity : input.entities) {
    // Multi-line block = name (header) + attribute rows. measureLabel returns
    // the max per-line advance as the width, i.e. the widest table row.
    QStringList lines;
    lines.append(entity.name);
    lines.append(entity.attributeLines);
    const QSizeF measured = flowchart::measureLabel(
        lines.join(QLatin1Char('\n')), QStringLiteral("markdown"), options);
    const QSizeF nameMeasured = flowchart::measureLabel(
        entity.name, QStringLiteral("markdown"), options);
    const qreal width =
        std::max(measured.width(), nameMeasured.width()) + 2.0 * kEntityHorizontalPadding;
    // Fixed-row table: header + N attribute rows, each one line tall, plus
    // vertical padding and the 1px header divider band the painter draws.
    const int rowCount = 1 + entity.attributeLines.size();
    const qreal height = static_cast<qreal>(rowCount) * options.lineHeight +
                         2.0 * kEntityVerticalPadding + kHeaderDividerBand;
    // mermaid clamps each entity box to er.minEntityWidth/minEntityHeight
    // (defaults 100/75); a content-sized box would otherwise shrink single
    // entities below the crow's-foot markers.
    result.entities.insert(
        entity.id, QSizeF(std::max(width, minEntityWidth), std::max(height, minEntityHeight)));
  }
  for (const ErLayoutRelationshipInput& rel : input.relationships) {
    if (rel.label.isEmpty()) continue;
    result.relationships.insert(rel.id, flowchart::measureLabel(
        rel.label, QStringLiteral("markdown"), options));
  }
  return result;
}

muffin::mermaid::er::ErPlacementResult
muffin::mermaid::er::layoutErDiagramDagre(const ErLayoutInput& input,
                                          const ErLayoutMeasurements& measurements,
                                          qreal entitySpacing, qreal rankSpacing) {
  ErPlacementResult result;
  if (input.entities.isEmpty()) return result;
  flowchart::FlowchartData projected;
  projected.direction = input.direction;
  for (const ErLayoutEntityInput& entity : input.entities) {
    flowchart::FlowVertex vertex;
    vertex.id = entity.id;
    vertex.text = entity.name;
    vertex.type = QStringLiteral("rect");
    projected.vertices.append(std::move(vertex));
  }
  // Roles are NOT dagre edge labels — they render as crow's-foot-adjacent text
  // in the painter. Only the relationship `label` (after `:`) feeds dagre.
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
  for (const flowchart::FlowLayoutEdge& edge : placed.edges) {
    ErPlacementRelationship rel;
    rel.id = edge.id;
    rel.path = edge.path;
    rel.points = edge.points;
    rel.segments = edge.segments;
    if (edge.hasLabelPosition)
      rel.labelPosition = QPointF(edge.labelX, edge.labelY);
    result.relationships.append(std::move(rel));
  }
  return result;
}

QJsonObject
muffin::mermaid::er::erLayoutInputToJson(const ErLayoutInput& input) {
  QJsonArray entities;
  for (const ErLayoutEntityInput& entity : input.entities) {
    entities.append(QJsonObject{
        {QStringLiteral("id"), entity.id},
        {QStringLiteral("name"), entity.name},
        {QStringLiteral("attributeLines"), strings(entity.attributeLines)},
        {QStringLiteral("attributeStyles"), strings(entity.attributeStyles)}});
  }
  QJsonArray relationships;
  for (const ErLayoutRelationshipInput& rel : input.relationships) {
    relationships.append(QJsonObject{
        {QStringLiteral("id"), rel.id},
        {QStringLiteral("entityA"), rel.entityA},
        {QStringLiteral("entityB"), rel.entityB},
        {QStringLiteral("cardA"), erCardinalityName(rel.cardA)},
        {QStringLiteral("cardB"), erCardinalityName(rel.cardB)},
        {QStringLiteral("identifying"), rel.identifying},
        {QStringLiteral("label"), rel.label},
        {QStringLiteral("roleA"), rel.roleA},
        {QStringLiteral("roleB"), rel.roleB}});
  }
  return {{QStringLiteral("direction"), input.direction},
          {QStringLiteral("entities"), entities},
          {QStringLiteral("relationships"), relationships}};
}
