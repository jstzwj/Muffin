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

#include <QFontMetricsF>
#include <QSizeF>
#include <QString>
#include <QTextCharFormat>

#include <algorithm>

namespace {

constexpr qreal kPadding = 20.0;  // requirementBox padding (upstream hardcoded)
constexpr qreal kGap = 20.0;      // gap between name and body (upstream hardcoded)

}  // namespace

namespace muffin::mermaid::requirement {

qreal requirementEffectiveFontSize(const RequirementTextStyle& style, qreal themeFontSize) {
  return style.fontSizePx >= 0.0 ? style.fontSizePx : themeFontSize;
}

QString requirementEffectiveFontFamily(const RequirementTextStyle& style,
                                       const QString& themeFontFamily) {
  return style.fontFamily.isEmpty() ? themeFontFamily : style.fontFamily;
}

flowchart::FlowLabelDocument requirementRowDocument(const QString& text, bool bold,
    const RequirementTextStyle& style, qreal themeFontSize) {
  // Commit 3: apply the node's text-transform to the source BEFORE parseFlowLabel
  // (so offsets are consistent with the transformed string; ß->SS expands here).
  const QString transformed = applyRequirementTextTransform(text, style.transform);
  auto document = flowchart::parseFlowLabel(transformed, QStringLiteral("markdown"), true);
  document.formattingContext = flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
  // Commit-2 FlowLabelDocument fields (applied across the whole measure/wrap/layout/
  // paint chain via makeFlowLabelFont). The name row's bold is an absolute override
  // (mermaid reqTitle is font-weight:bold regardless of the label's font-weight).
  document.baseWeight = style.fontWeight;
  document.baseStyle = style.fontStyle;
  document.letterSpacingPx = style.letterSpacingPx;
  document.wordSpacingPx = style.wordSpacingPx;
  document.underline = style.underline;
  document.overline = style.overline;
  document.strikeOut = style.strikeOut;
  // The name row is font-weight:bold by default (reqTitle), but a DECLARED node
  // font-weight wins on both name and body rows (probe: font-weight:100 -> name
  // AND body both 100; bolder -> both 900). So apply the default-bold override
  // ONLY when no valid font-weight was declared; otherwise the resolved
  // baseWeight (already set above) stands for the name row too.
  if (bold && !document.text.isEmpty() && !style.fontWeightResolved) {
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    document.formats.append({0, static_cast<int>(document.text.size()), format});
  }
  flowchart::prepareFlowLabelMath(document, requirementEffectiveFontSize(style, themeFontSize));
  return document;
}

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
    const RequirementLayoutInput& input, const QString& fontFamily, qreal fontSize,
    QFont::Weight themeFontWeight) {
  RequirementLayoutMeasurements result;
  // Per-row height model: the mermaid requirementBox measures each row at the
  // standard mermaid line-height (fontSize × 1.5) by default. Commit 3 resolves a
  // per-node text style (line-height / font-size) from the node's labelStyle:
  //   - line-height:normal -> the font's natural height (QFontMetricsF::height()).
  //   - line-height:<length>/<number> -> the resolved px (number is a multiplier).
  //   - unset/invalid -> fontSize × 1.5 (the deterministic oracle default).
  // The per-row WIDTH is measured with the same prepared markdown document the
  // painter draws (text-transform + font applied) so the box width matches ink.
  const qreal themeLineHeight = fontSize * 1.5;
  for (const RequirementLayoutNodeInput& node : input.nodes) {
    RequirementNodeMeasurement m;
    m.hasBody = node.hasBody;
    const RequirementTextStyle style = resolveRequirementTextStyle(
        node.cssStyles, fontFamily, fontSize, themeFontWeight, themeLineHeight);
    const qreal effSize = requirementEffectiveFontSize(style, fontSize);
    const QString effFamily = requirementEffectiveFontFamily(style, fontFamily);
    // font-size:0 or line-height:0 -> the node collapses to mermaid's 20x20 min
    // box with no text ink (probed: gNode 20x20, ink=0). See STEP0F §2. Check
    // BEFORE building the natural-height QFont so a 0px font (font-size:0 with
    // line-height:normal) never reaches Qt's positive-pixel-size contract;
    // line-height:0 collapses via lineHeightPx==0 here (it never reaches the
    // normal branch). Equivalent to the prior effLineHeight==0 check because the
    // only way effLineHeight could be 0 was an explicit line-height:0.
    if (effSize == 0.0 || style.lineHeightPx == 0.0) {
      for (int i = 0; i < node.rows.size(); ++i) m.rowHeights.append(0.0);
      m.typeHeight = 0.0;
      m.nameHeight = 0.0;
      m.boxSize = QSizeF(20.0, 20.0);
      result.insert(node.id, m);
      continue;
    }
    qreal effLineHeight;
    if (style.lineHeightNormal) {
      const QFont f = flowchart::makeFlowLabelFont(effFamily, effSize, style.fontWeight, style.fontStyle);
      effLineHeight = QFontMetricsF(f).height();
    } else {
      // Unset line-height -> the resolved font's 1.5x default (mermaid's DIV
      // line-height:1.5 applies to the actual font-size). Default config has
      // effSize == theme fontSize, so this stays 24 (byte-identical).
      effLineHeight = style.lineHeightPx >= 0.0 ? style.lineHeightPx : effSize * 1.5;
    }
    m.rowHeights.reserve(node.rows.size());
    qreal maxRowWidth = 0.0;
    for (const RequirementLayoutRow& row : node.rows) {
      m.rowHeights.append(effLineHeight);
      const flowchart::FlowLabelDocument doc =
          requirementRowDocument(row.text, row.bold, style, fontSize);
      const QRectF ink = flowchart::measureFlowSvgTextBounds(doc, effFamily, effSize);
      maxRowWidth = std::max(maxRowWidth, ink.width());
    }
    if (!m.rowHeights.isEmpty()) {
      m.typeHeight = m.rowHeights.at(0);
      m.nameHeight = m.rowHeights.value(1, 0.0);
    }
    qreal bboxHeight = 0.0;
    for (qreal h : m.rowHeights) bboxHeight += h;
    if (node.hasBody) bboxHeight += kGap;
    m.boxSize = QSizeF(maxRowWidth + kPadding, bboxHeight + kPadding);
    result.insert(node.id, m);
  }
  return result;
}

RequirementPlacementResult layoutRequirementDiagramDagre(
    const RequirementLayoutInput& input, const RequirementLayoutMeasurements& measurements,
    qreal nodeSpacing, qreal rankSpacing, const QString& fontFamily, qreal fontSize) {
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
  textOptions.fontFamily = fontFamily;
  textOptions.fontPixelSize = fontSize;
  textOptions.lineHeight = fontSize * 1.5;
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
