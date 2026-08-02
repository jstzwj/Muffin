// Layout projection for requirementDiagram. Mirrors the ER/State/Class pattern:
// project nodes + edges into flowchart::FlowchartData, call the shared dagre
// pipeline, map FlowLayoutResult back.
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and public functions use
// fully-qualified names.

#include "mermaid/requirement/RequirementLayout.h"

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QSizeF>
#include <QString>

#include <algorithm>

namespace {

constexpr qreal kPadding = 20.0;  // requirementBox padding (upstream hardcoded)
constexpr qreal kGap = 20.0;      // gap between name and body (upstream hardcoded)

}  // namespace

namespace muffin::mermaid::requirement {

RequirementLayoutInput
buildRequirementLayoutInput(const RequirementDiagramData& data) {
  RequirementLayoutInput input;
  input.direction = data.direction.isEmpty() ? QStringLiteral("TB") : data.direction;
  // Requirements first, then elements — matching RequirementDB.getData() order,
  // which assigns colorIndex by node insertion order.
  for (const RequirementNode& req : data.requirements) {
    RequirementLayoutNodeInput node;
    node.id = req.name;
    node.name = req.name;
    node.displayType = req.type;
    node.isElement = false;
    node.cssClasses = req.cssClasses;
    node.cssStyles = req.cssStyles;
    // Type line: <<Type>>
    RequirementLayoutRow typeRow;
    typeRow.text = QStringLiteral("<<") + req.type + QStringLiteral(">>");
    node.rows.append(typeRow);
    // Name line (bold)
    RequirementLayoutRow nameRow;
    nameRow.text = req.name;
    nameRow.bold = true;
    node.rows.append(nameRow);
    // Body rows (only non-empty fields).
    if (!req.requirementId.isEmpty())
      node.rows.append({QStringLiteral("ID: ") + req.requirementId, false});
    if (!req.text.isEmpty())
      node.rows.append({QStringLiteral("Text: ") + req.text, false});
    if (!req.risk.isEmpty())
      node.rows.append({QStringLiteral("Risk: ") + req.risk, false});
    if (!req.verifyMethod.isEmpty())
      node.rows.append({QStringLiteral("Verification: ") + req.verifyMethod, false});
    node.hasBody = node.rows.size() > 2;
    input.nodes.append(std::move(node));
  }
  for (const ElementNode& elem : data.elements) {
    RequirementLayoutNodeInput node;
    node.id = elem.name;
    node.name = elem.name;
    node.displayType = QStringLiteral("Element");
    node.isElement = true;
    node.cssClasses = elem.cssClasses;
    node.cssStyles = elem.cssStyles;
    RequirementLayoutRow typeRow;
    typeRow.text = QStringLiteral("<<Element>>");
    node.rows.append(typeRow);
    RequirementLayoutRow nameRow;
    nameRow.text = elem.name;
    nameRow.bold = true;
    node.rows.append(nameRow);
    if (!elem.type.isEmpty())
      node.rows.append({QStringLiteral("Type: ") + elem.type, false});
    if (!elem.docRef.isEmpty())
      node.rows.append({QStringLiteral("Doc Ref: ") + elem.docRef, false});
    node.hasBody = node.rows.size() > 2;
    input.nodes.append(std::move(node));
  }
  // Edges — mirror RequirementDB.getData() relationship edge construction.
  for (const Relationship& rel : data.relations) {
    RequirementLayoutEdgeInput edge;
    edge.id = rel.src + QStringLiteral("-") + rel.dst;
    edge.start = rel.src;
    edge.end = rel.dst;
    edge.relationshipType = rel.type;
    edge.isContains = rel.type == QLatin1String("contains");
    edge.label = QStringLiteral("<<") + rel.type + QStringLiteral(">>");
    input.edges.append(std::move(edge));
  }
  return input;
}

RequirementLayoutMeasurements measureRequirementLayoutInput(
    const RequirementLayoutInput& input, const QString& fontFamily, qreal fontSize) {
  RequirementLayoutMeasurements result;
  // Per-row height model: the mermaid requirementBox with htmlLabels:false
  // measures each row as SVG-text getBBox().height + 6. Empirically (trebuchet
  // ms @ 16px) this averages ~fontSize × 1.5, matching the standard mermaid
  // line-height. Using fontSize × 1.5 gives stable cross-platform heights that
  // match Chrome's average within sub-pixel; per-row ink-height measurement via
  // measureFlowSvgTextBounds differs from Chrome by ~4px/row (Qt/Chromium SVG
  // bbox discrepancy), so the deterministic lineHeight is the safer oracle
  // contract. The per-row WIDTH is still measured (for box width, reported).
  const qreal rowHeight = fontSize * 1.5;
  for (const RequirementLayoutNodeInput& node : input.nodes) {
    RequirementNodeMeasurement m;
    m.hasBody = node.hasBody;
    m.rowHeights.reserve(node.rows.size());
    qreal maxRowWidth = 0.0;
    for (const RequirementLayoutRow& row : node.rows) {
      m.rowHeights.append(rowHeight);
      const flowchart::FlowLabelDocument doc =
          flowchart::parseFlowLabel(row.text, QStringLiteral("text"));
      const QRectF ink = flowchart::measureFlowSvgTextBounds(doc, fontFamily, fontSize);
      maxRowWidth = std::max(maxRowWidth, ink.width());
    }
    if (!m.rowHeights.isEmpty()) {
      m.typeHeight = m.rowHeights.at(0);
      m.nameHeight = m.rowHeights.value(1, 0.0);
    }
    qreal bboxHeight = 0.0;
    for (qreal h : m.rowHeights) bboxHeight += h;
    if (node.hasBody) bboxHeight += kGap;
    const qreal totalWidth = maxRowWidth + kPadding;
    const qreal totalHeight = bboxHeight + kPadding;
    m.boxSize = QSizeF(totalWidth, totalHeight);
    result.insert(node.id, m);
  }
  return result;
}

RequirementPlacementResult layoutRequirementDiagramDagre(
    const RequirementLayoutInput& input, const RequirementLayoutMeasurements& measurements,
    qreal nodeSpacing, qreal rankSpacing) {
  RequirementPlacementResult result;
  if (input.nodes.isEmpty()) return result;
  flowchart::FlowchartData projected;
  projected.direction = input.direction.isEmpty() ? QStringLiteral("TB") : input.direction;
  for (const RequirementLayoutNodeInput& node : input.nodes) {
    flowchart::FlowVertex vertex;
    vertex.id = node.id;
    vertex.text = node.name;
    vertex.type = QStringLiteral("rect");
    projected.vertices.append(std::move(vertex));
  }
  QMap<QString, QSizeF> measuredNodes;
  for (const RequirementLayoutNodeInput& node : input.nodes) {
    const auto it = measurements.constFind(node.id);
    if (it != measurements.constEnd()) measuredNodes.insert(node.id, it.value().boxSize);
  }
  QMap<QString, QSizeF> measuredEdgeLabels;
  flowchart::FlowTextOptions textOptions;
  textOptions.fontPixelSize = 16.0;
  textOptions.lineHeight = 16.0 * 1.5;
  for (const RequirementLayoutEdgeInput& edge : input.edges) {
    flowchart::FlowEdge projectedEdge;
    projectedEdge.id = edge.id;
    projectedEdge.start = edge.start;
    projectedEdge.end = edge.end;
    projectedEdge.text = edge.label;
    projectedEdge.labelType = QStringLiteral("markdown");
    projected.edges.append(std::move(projectedEdge));
    // Measure the edge label so dagre reserves space for it.
    measuredEdgeLabels.insert(edge.id, flowchart::measureLabel(
        edge.label, QStringLiteral("markdown"), textOptions));
  }
  flowchart::FlowLayoutOptions options;
  options.nodeSpacing = nodeSpacing;
  options.rankSpacing = rankSpacing;
  options.measuredEdgeLabels = measuredEdgeLabels;
  const flowchart::FlowLayoutResult placed =
      flowchart::layoutFlowchartNodesDagre(projected, measuredNodes, options);
  for (const flowchart::FlowLayoutNode& node : placed.nodes)
    result.nodes.append({node.id, node.x, node.y, node.width, node.height});
  for (const flowchart::FlowLayoutEdge& edge : placed.edges) {
    RequirementPlacementEdge projectedEdge;
    projectedEdge.id = edge.id;
    projectedEdge.path = edge.path;
    projectedEdge.points = edge.points;
    projectedEdge.segments = edge.segments;
    if (edge.hasLabelPosition)
      projectedEdge.labelPosition = QPointF(edge.labelX, edge.labelY);
    result.edges.append(std::move(projectedEdge));
  }
  return result;
}

}  // namespace muffin::mermaid::requirement
