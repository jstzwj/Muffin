#include "mermaid/block/BlockScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/BlinkSvgPathMetrics.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/scene/FlowScenePainter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>

namespace muffin::mermaid::block {
namespace {

struct JsScalar {
  bool string = false;
  QString text;
  qreal number = 0.0;

  static JsScalar fromNumber(qreal value) { return {false, {}, value}; }
  static JsScalar fromConfig(const QJsonValue& value, qreal fallback) {
    if (value.isString()) return {true, value.toString(), 0.0};
    const double converted = editor::jsNumberValue(value);
    return fromNumber(std::isfinite(converted) ? qreal(converted) : fallback);
  }
  qreal toNumber() const {
    return string ? qreal(editor::jsNumberValue(QJsonValue(text))) : number;
  }
  QString toString() const {
    return string ? text : editor::jsNumberToString(number);
  }
  bool truthy() const {
    return string ? !text.isEmpty() : number != 0.0 && !std::isnan(number);
  }
};

JsScalar jsAdd(const JsScalar& left, const JsScalar& right) {
  if (left.string || right.string)
    return {true, left.toString() + right.toString(), 0.0};
  return JsScalar::fromNumber(left.number + right.number);
}

JsScalar jsSubtract(const JsScalar& left, qreal right) {
  return JsScalar::fromNumber(left.toNumber() - right);
}

JsScalar jsMultiply(const JsScalar& left, qreal right) {
  return JsScalar::fromNumber(left.toNumber() * right);
}

struct LayoutNode {
  BlockNode block;
  QSizeF labelSize;
  QSizeF naturalSize;
  QSizeF size;
  QSizeF paintSize;
  QPointF center;
  JsScalar rawWidth;
  JsScalar rawHeight;
  JsScalar rawX;
  JsScalar rawY;
  QVector<LayoutNode> children;
};

void setSize(LayoutNode& node, const JsScalar& width,
             const JsScalar& height) {
  node.rawWidth = width;
  node.rawHeight = height;
  node.size = QSizeF(width.toNumber(), height.toNumber());
}

void setCenter(LayoutNode& node, const JsScalar& x, const JsScalar& y) {
  node.rawX = x;
  node.rawY = y;
  node.center = QPointF(x.toNumber(), y.toNumber());
}

QString mappedShape(const QString& type) {
  if (type == QLatin1String("square") || type == QLatin1String("composite") ||
      type == QLatin1String("na")) return QStringLiteral("rect");
  if (type == QLatin1String("rect_left_inv_arrow")) return QStringLiteral("odd");
  if (type == QLatin1String("doublecircle")) return QStringLiteral("double_circle");
  return type;
}

QSizeF labelSize(const BlockNode& node, const QString& family, qreal fontSize,
                 QFont::Weight fontWeight, bool htmlLabels) {
  if (node.label.isEmpty() || !(fontSize > 0.0)) return {};
  if (htmlLabels) {
    auto document = flowchart::parseFlowLabel(node.label, QStringLiteral("text"));
    document.baseWeight = fontWeight;
    QSizeF size = flowchart::measureFlowLabel(document, family, fontSize,
                                              fontSize * 1.5);
    size.setWidth(flowchart::measureChromiumInlineLayoutWidth(
        document, family, fontSize));
    return size;
  }
  auto document = flowchart::parseFlowSvgLabel(node.label, QStringLiteral("text"));
  document.baseWeight = fontWeight;
  return flowchart::measureChromiumSvgTextLayoutBounds(
      document, family, fontSize).size();
}

QSizeF naturalSize(const BlockNode& node, const QSizeF& label,
                   const JsScalar& padding) {
  const qreal bw = label.width();
  const qreal bh = label.height();
  const qreal pad = padding.toNumber();
  if (node.type == QLatin1String("space")) return {};
  if (node.type == QLatin1String("square") ||
      node.type == QLatin1String("round") ||
      node.type == QLatin1String("na")) {
    // rect() writes bbox + padding to width/height attributes, but x/y use
    // padding / 2. With a string padding those are different JS coercions;
    // the enclosing <g>.getBBox() is the union of the label and used rect.
    const qreal attrWidth =
        jsAdd(JsScalar::fromNumber(bw), padding).toNumber();
    const qreal attrHeight =
        jsAdd(JsScalar::fromNumber(bh), padding).toNumber();
    const qreal left = -bw / 2.0 - pad / 2.0;
    const qreal top = -bh / 2.0 - pad / 2.0;
    const qreal right = std::max(bw / 2.0, left + attrWidth);
    const qreal bottom = std::max(bh / 2.0, top + attrHeight);
    return {right - std::min(-bw / 2.0, left),
            bottom - std::min(-bh / 2.0, top)};
  }
  if (node.type == QLatin1String("circle")) {
    const qreal diameter = bw + pad;
    return {diameter, diameter};
  }
  if (node.type == QLatin1String("doublecircle")) {
    const qreal diameter = bw + pad + 10.0;
    return {diameter, diameter};
  }
  if (node.type == QLatin1String("diamond")) {
    const qreal diameter = bw + bh + 2.0 * pad;
    return {diameter, diameter};
  }
  if (node.type == QLatin1String("hexagon")) {
    const qreal h = bh + pad;
    return {bw + h / 2.0 + pad, h};
  }
  if (node.type == QLatin1String("stadium")) {
    const qreal h = bh + pad;
    return {bw + h / 4.0 + pad, h};
  }
  if (node.type == QLatin1String("subroutine"))
    return {bw + pad + 16.0, bh + pad};
  if (node.type == QLatin1String("cylinder")) {
    const qreal w = jsAdd(JsScalar::fromNumber(bw), padding).toNumber();
    const qreal ry = (w / 2.0) / (2.5 + w / 50.0);
    const qreal bodyHeight =
        jsAdd(JsScalar::fromNumber(bh + ry), padding).toNumber();
    flowchart::BlinkPathBounds bounds;
    flowchart::BlinkFloatPoint current{0.0f, float(ry)};
    bounds.add(current);
    flowchart::BlinkFloatPoint next{float(w), current.y};
    flowchart::addBlinkSvgArcBounds(
        bounds, current, next, w / 2.0, ry, 0.0, false, false);
    current = next;
    next = {0.0f, current.y};
    flowchart::addBlinkSvgArcBounds(
        bounds, current, next, w / 2.0, ry, 0.0, false, false);
    current = next;
    current.y += float(bodyHeight);
    bounds.add(current);
    next = {float(w), current.y};
    flowchart::addBlinkSvgArcBounds(
        bounds, current, next, w / 2.0, ry, 0.0, false, false);
    current = next;
    current.y -= float(bodyHeight);
    bounds.add(current);
    return bounds.rect().size();
  }
  if (node.type == QLatin1String("lean_right")) {
    const qreal h = bh + pad;
    return {bw + pad + 2.0 * h / 3.0, h};
  }
  if (node.type == QLatin1String("lean_left")) {
    const qreal h = bh + pad;
    return {bw + pad + h / 3.0, h};
  }
  if (node.type == QLatin1String("trapezoid") ||
      node.type == QLatin1String("inv_trapezoid")) {
    const qreal h = bh + pad;
    return {bw + pad + 2.0 * h / 3.0, h};
  }
  if (node.type == QLatin1String("rect_left_inv_arrow")) {
    const qreal h = bh + pad;
    return {bw + pad + h / 2.0, h};
  }
  if (node.type == QLatin1String("block_arrow")) {
    const qreal h = bh + 2.0 * pad;
    return {bw + h + pad, h};
  }
  return {bw + pad, bh + pad};
}

LayoutNode makeLayoutNode(const BlockNode& block, const QString& family,
                          qreal fontSize, bool htmlLabels,
                          const JsScalar& padding,
                          const csscascade::FlowchartProjection* css,
                          const CssLengthContext& rootContext) {
  LayoutNode result;
  result.block = block;
  const JsScalar nodePadding = block.type == QLatin1String("composite")
      ? JsScalar::fromNumber(0.0) : padding;
  QString resolvedFamily = family;
  qreal resolvedFontSize = fontSize;
  QFont::Weight resolvedWeight = QFont::Normal;
  bool labelHasBox = true;
  if (css) {
    const auto label = css->nodeLabels.constFind(block.id);
    if (label != css->nodeLabels.constEnd()) {
      resolvedFamily = label->fontFamily;
      const CssLengthContext context = editor::pieCssLengthContext(
          resolvedFamily, rootContext.emPx);
      resolvedFontSize = editor::cssFontSizePx(label->fontSize, context);
      resolvedWeight = editor::cssFontWeightToQt(
          QJsonValue(label->fontWeight), QFont::Normal);
      labelHasBox = label->hasBox();
    }
  }
  result.labelSize = labelHasBox
      ? labelSize(block, resolvedFamily, resolvedFontSize, resolvedWeight,
                  htmlLabels)
      : QSizeF(0.0, 0.0);
  result.naturalSize = naturalSize(block, result.labelSize, nodePadding);
  setSize(result, JsScalar::fromNumber(result.naturalSize.width()),
          JsScalar::fromNumber(result.naturalSize.height()));
  setCenter(result, JsScalar::fromNumber(0.0), JsScalar::fromNumber(0.0));
  for (const BlockNode& child : block.children)
    result.children.append(makeLayoutNode(child, family, fontSize, htmlLabels,
                                          padding, css, rootContext));
  return result;
}

void setBlockSizes(LayoutNode& node, qreal siblingWidth, qreal siblingHeight,
                   const JsScalar& padding) {
  if (!node.rawWidth.truthy())
    setSize(node, JsScalar::fromNumber(siblingWidth),
            JsScalar::fromNumber(siblingHeight));
  if (node.children.isEmpty()) return;
  for (LayoutNode& child : node.children)
    setBlockSizes(child, 0.0, 0.0, padding);
  qreal maxWidth = 0.0;
  qreal maxHeight = 0.0;
  for (const LayoutNode& child : node.children) {
    if (child.block.type == QLatin1String("space")) continue;
    maxWidth = std::max(maxWidth,
                        child.size.width() / std::max(1, child.block.widthInColumns));
    maxHeight = std::max(maxHeight, child.size.height());
  }
  for (LayoutNode& child : node.children) {
    const int span = std::max(1, child.block.widthInColumns);
    setSize(child,
            jsAdd(JsScalar::fromNumber(maxWidth * span),
                  jsMultiply(padding, span - 1)),
            JsScalar::fromNumber(maxHeight));
  }
  for (LayoutNode& child : node.children)
    setBlockSizes(child, maxWidth, maxHeight, padding);

  int numItems = 0;
  for (const LayoutNode& child : node.children)
    numItems += std::max(1, child.block.widthInColumns);
  const int columns = node.block.hasColumns ? node.block.columns : -1;
  int xSize = node.children.size();
  if (columns > 0 && columns < numItems) xSize = columns;
  if (xSize == 0) return;
  const int ySize = int(std::ceil(qreal(numItems) / qreal(xSize)));
  JsScalar width = jsAdd(
      jsMultiply(jsAdd(JsScalar::fromNumber(maxWidth), padding), xSize),
      padding);
  JsScalar height = jsAdd(
      jsMultiply(jsAdd(JsScalar::fromNumber(maxHeight), padding), ySize),
      padding);
  if (width.toNumber() < siblingWidth) {
    width = JsScalar::fromNumber(siblingWidth);
    height = JsScalar::fromNumber(siblingHeight);
    const qreal pad = padding.toNumber();
    const qreal childWidth = (siblingWidth - xSize * pad - pad) / xSize;
    const qreal childHeight = (siblingHeight - ySize * pad - pad) / ySize;
    for (LayoutNode& child : node.children)
      setSize(child, JsScalar::fromNumber(childWidth),
              JsScalar::fromNumber(childHeight));
  }
  if (width.toNumber() < node.rawWidth.toNumber()) {
    width = node.rawWidth;
    const int count = columns > 0 ? std::min<int>(node.children.size(), columns)
                                  : node.children.size();
    if (count > 0) {
      const qreal pad = padding.toNumber();
      const qreal childWidth =
          (width.toNumber() - count * pad - pad) / count;
      for (LayoutNode& child : node.children)
        setSize(child, JsScalar::fromNumber(childWidth), child.rawHeight);
    }
  }
  setSize(node, width, height);
}

QPair<int, int> blockPosition(int columns, int position) {
  if (columns == 0 || (columns != -1 && columns < 0))
    throw BlockParseError(BlockErrorKind::Runtime, 1, 1, {},
                          QStringLiteral("Columns must be an integer !== 0."));
  if (columns < 0) return {position, 0};
  if (columns == 1) return {0, position};
  return {position % columns, position / columns};
}

void layoutBlocks(LayoutNode& node, const JsScalar& padding) {
  if (node.children.isEmpty()) return;
  const int columns = node.block.hasColumns ? node.block.columns : -1;
  QHash<int, qreal> rowHeights;
  int columnPos = 0;
  for (const LayoutNode& child : node.children) {
    const int row = blockPosition(columns, columnPos).second;
    rowHeights[row] = std::max(rowHeights.value(row), child.size.height());
    int filled = std::max(1, child.block.widthInColumns);
    if (columns > 0) filled = std::min(filled, columns - columnPos % columns);
    columnPos += filled;
  }
  QList<int> rows = rowHeights.keys();
  std::sort(rows.begin(), rows.end());
  QHash<int, qreal> rowOffsets;
  qreal offset = 0.0;
  for (int row : rows) {
    rowOffsets.insert(row, offset);
    offset += jsAdd(JsScalar::fromNumber(rowHeights.value(row)), padding)
                  .toNumber();
  }
  columnPos = 0;
  int currentRow = 0;
  JsScalar startX = node.rawX.truthy()
      ? jsAdd(node.rawX, JsScalar::fromNumber(-node.size.width() / 2.0))
      : JsScalar::fromNumber(-padding.toNumber());
  for (LayoutNode& child : node.children) {
    const auto [column, row] = blockPosition(columns, columnPos);
    (void)column;
    if (row != currentRow) {
      currentRow = row;
      startX = node.rawX.truthy()
          ? jsAdd(node.rawX,
                  JsScalar::fromNumber(-node.size.width() / 2.0))
          : JsScalar::fromNumber(-padding.toNumber());
    }
    const qreal halfWidth = child.size.width() / 2.0;
    const JsScalar childX = jsAdd(
        jsAdd(startX, padding), JsScalar::fromNumber(halfWidth));
    const qreal rowHeight = rowHeights.value(row, child.size.height());
    const JsScalar childY = jsAdd(
        jsAdd(jsAdd(jsSubtract(node.rawY, node.size.height() / 2.0),
                    JsScalar::fromNumber(rowOffsets.value(row))),
              JsScalar::fromNumber(rowHeight / 2.0)),
        padding);
    setCenter(child, childX, childY);
    startX = jsAdd(child.rawX, JsScalar::fromNumber(halfWidth));
    layoutBlocks(child, padding);
    int filled = std::max(1, child.block.widthInColumns);
    if (columns > 0) filled = std::min(filled, columns - columnPos % columns);
    columnPos += filled;
  }
}

QSizeF positionedPaintSize(const LayoutNode& node) {
  if (node.block.type == QLatin1String("square") ||
      node.block.type == QLatin1String("round") ||
      node.block.type == QLatin1String("composite") ||
      node.block.type == QLatin1String("hexagon") ||
      node.block.type == QLatin1String("na"))
    return node.size;
  if (node.block.type == QLatin1String("block_arrow") &&
      node.block.widthInColumns > 1 && node.size.width() > node.naturalSize.width())
    return {node.size.width(), node.naturalSize.height()};
  return node.naturalSize;
}

void flatten(LayoutNode& node, QVector<LayoutNode*>& output) {
  if (node.block.id != QLatin1String("root") && node.block.type != QLatin1String("space"))
    output.append(&node);
  for (LayoutNode& child : node.children) flatten(child, output);
}

QRectF contentBounds(const QVector<LayoutNode*>& nodes) {
  // blockRenderer's findBounds starts at the SVG origin and updates each
  // side with JavaScript comparisons. Invalid transforms therefore do not
  // poison the accumulated bounds: every comparison against NaN is false.
  qreal minX = 0.0;
  qreal minY = 0.0;
  qreal maxX = 0.0;
  qreal maxY = 0.0;
  for (const LayoutNode* node : nodes) {
    const qreal halfWidth = node->rawWidth.toNumber() / 2.0;
    const qreal halfHeight = node->rawHeight.toNumber() / 2.0;
    const qreal left = jsSubtract(node->rawX, halfWidth).toNumber();
    const qreal top = jsSubtract(node->rawY, halfHeight).toNumber();
    // The positive sides use JavaScript `+`. A string-valued x/y therefore
    // concatenates before the relational comparison coerces it to Number.
    const qreal right =
        jsAdd(node->rawX, JsScalar::fromNumber(halfWidth)).toNumber();
    const qreal bottom =
        jsAdd(node->rawY, JsScalar::fromNumber(halfHeight)).toNumber();
    if (left < minX) minX = left;
    if (top < minY) minY = top;
    if (right > maxX) maxX = right;
    if (bottom > maxY) maxY = bottom;
  }
  return QRectF(minX, minY, maxX - minX, maxY - minY);
}

QVector<QPointF> blockArrowPoints(const LayoutNode& node) {
  QStringList directions = node.block.directions;
  if (directions.contains(QStringLiteral("x"))) directions += {QStringLiteral("right"), QStringLiteral("left")};
  if (directions.contains(QStringLiteral("y"))) directions += {QStringLiteral("up"), QStringLiteral("down")};
  directions.removeDuplicates();
  const qreal w = node.paintSize.width();
  const qreal h = node.paintSize.height();
  QVector<QPointF> points;
  if (directions.contains(QStringLiteral("right")) && directions.contains(QStringLiteral("down")))
    points = {{0, 0}, {w, 0}, {0, -h}};
  else if (directions.contains(QStringLiteral("right")) && directions.contains(QStringLiteral("up")))
    points = {{0, 0}, {w, -h / 2.0}, {0, -h}};
  else if (directions.contains(QStringLiteral("left")) && directions.contains(QStringLiteral("down")))
    points = {{w, 0}, {0, 0}, {w, -h}};
  else if (directions.contains(QStringLiteral("left")) && directions.contains(QStringLiteral("up")))
    points = {{w, 0}, {0, -h / 2.0}, {w, -h}};
  else
    points = {{0, 0}, {w, -h / 2.0}, {0, -h}};
  for (QPointF& point : points) point += QPointF(-w / 2.0, h / 2.0);
  return points;
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

}  // namespace

BlockScene buildBlockScene(const BlockData& data, BlockConfig config,
                           const flowtheme::FlowThemeVariables& theme,
                           const csscascade::FlowchartProjection* measurementCss,
                           const csscascade::FlowchartProjection* paintCss) {
  BlockScene scene;
  scene.useMaxWidth = editor::truthyConfigValue(config.useMaxWidth);
  scene.fontFamily = theme.fontFamily;
  const JsScalar padding = JsScalar::fromConfig(config.padding, 8.0);
  const auto rootCtx = editor::pieCssLengthContext(theme.fontFamily, 16.0);
  const qreal fontSize = editor::cssFontSizePx(theme.fontSize, rootCtx);

  LayoutNode root = makeLayoutNode(data.root, theme.fontFamily, fontSize,
                                   config.htmlLabels, padding, measurementCss,
                                   rootCtx);
  setBlockSizes(root, 0.0, 0.0, padding);
  layoutBlocks(root, padding);
  QVector<LayoutNode*> flat;
  flatten(root, flat);
  for (LayoutNode* node : flat) node->paintSize = positionedPaintSize(*node);
  scene.contentBounds = contentBounds(flat);
  const qreal width = scene.contentBounds.width();
  const qreal height = scene.contentBounds.height();
  scene.bounds = QRectF(scene.contentBounds.x() - 5.0,
                        scene.contentBounds.y() - 5.0,
                        width + 10.0, height + 10.0);
  scene.viewBoxAttribute = QStringLiteral("%1 %2 %3 %4")
      .arg(scene.bounds.x(), 0, 'g', 17).arg(scene.bounds.y(), 0, 'g', 17)
      .arg(scene.bounds.width(), 0, 'g', 17).arg(scene.bounds.height(), 0, 'g', 17);
  const qreal magic = height == 0.0 ? 1.0
      : std::max<qreal>(1.0, std::round(0.125 * width / height));
  const qreal rasterHeight = scene.useMaxWidth
      ? scene.bounds.height()
      : height + magic + 10.0;
  // Chromium rasterizes the CSS replaced-element viewport at the nearest
  // device pixel. In max-width mode its aspect ratio comes directly from the
  // viewBox; fixed sizing uses blockRenderer's additional magic-factor row.
  scene.rasterBounds = QRectF(scene.bounds.x(), scene.bounds.y(),
                              qRound(scene.bounds.width()),
                              qRound(rasterHeight));

  flowchart::FlowchartData flowData;
  flowData.direction = QStringLiteral("TB");
  QHash<QString, const LayoutNode*> layoutById;
  for (const LayoutNode* node : flat) {
    flowchart::FlowVertex vertex;
    vertex.id = node->block.id;
    vertex.domId = config.svgId + QLatin1Char('-') + vertex.id;
    vertex.text = node->block.label;
    vertex.type = mappedShape(node->block.type);
    vertex.styles = node->block.styles;
    vertex.classes = node->block.classes;
    flowData.vertices.append(vertex);
    layoutById.insert(vertex.id, node);
  }
  for (const BlockClass& source : data.classes) {
    flowchart::FlowClass value;
    value.id = source.id;
    value.styles = source.styles;
    value.textStyles = source.textStyles;
    flowData.classes.append(value);
  }
  flowchart::FlowLayoutResult flowLayout;
  for (LayoutNode* node : flat) {
    flowchart::FlowLayoutNode value;
    value.id = node->block.id;
    value.x = node->center.x();
    value.y = node->center.y();
    value.width = node->paintSize.width();
    value.height = node->paintSize.height();
    flowLayout.nodes.append(value);
    BlockNodeGeometry geometry;
    geometry.id = node->block.id;
    geometry.type = node->block.type;
    geometry.label = node->block.label;
    geometry.center = node->center;
    geometry.layoutSize = node->size;
    geometry.paintSize = node->paintSize;
    geometry.bounds = QRectF(node->center - QPointF(node->paintSize.width() / 2.0,
                                                    node->paintSize.height() / 2.0),
                             node->paintSize);
    scene.nodes.append(geometry);
  }

  const auto look = flowchart::parseFlowLook(config.look);
  for (const BlockEdge& source : data.edges) {
    if (!layoutById.contains(source.start) || !layoutById.contains(source.end)) continue;
    const LayoutNode* start = layoutById.value(source.start);
    const LayoutNode* end = layoutById.value(source.end);
    QVector<QPointF> points{start->center, (start->center + end->center) / 2.0,
                            end->center};
    flowchart::FlowVertex startVertex;
    startVertex.id = start->block.id;
    startVertex.type = mappedShape(start->block.type);
    flowchart::FlowVertex endVertex;
    endVertex.id = end->block.id;
    endVertex.type = mappedShape(end->block.type);
    points.first() = flowchart::intersectFlowShape(startVertex,
        QRectF(start->center - QPointF(start->paintSize.width()/2.0, start->paintSize.height()/2.0), start->paintSize),
        points.at(1), look);
    points.last() = flowchart::intersectFlowShape(endVertex,
        QRectF(end->center - QPointF(end->paintSize.width()/2.0, end->paintSize.height()/2.0), end->paintSize),
        points.at(1), look);
    if (source.arrowTypeEnd == QLatin1String("arrow_point")) {
      const QPointF delta = points.at(1) - points.last();
      const qreal length = std::hypot(delta.x(), delta.y());
      if (length > 0.0) points.last() += delta * (4.0 / length);
    }
    flowchart::FlowEdge edge;
    edge.id = source.id;
    edge.start = source.start;
    edge.end = source.end;
    edge.text = source.label;
    edge.type = source.arrowTypeEnd == QLatin1String("arrow_point")
        ? QStringLiteral("arrow_point") : QStringLiteral("open");
    edge.stroke = source.pattern == QLatin1String("dotted")
        ? QStringLiteral("dotted") : source.thickness == QLatin1String("thick")
          ? QStringLiteral("thick") : QStringLiteral("normal");
    flowData.edges.append(edge);
    flowchart::FlowLayoutEdge layoutEdge;
    layoutEdge.id = edge.id;
    layoutEdge.points = points;
    layoutEdge.path = flowchart::d3curve::pathForCurve(points, QStringLiteral("basis"));
    if (!edge.text.isEmpty()) {
      flowchart::FlowTextOptions options;
      options.fontFamily = theme.fontFamily;
      options.fontPixelSize = fontSize;
      options.lineHeight = fontSize * 1.5;
      const auto label = flowchart::layoutFlowchartEdgeLabel(edge, options);
      layoutEdge.hasLabelPosition = true;
      layoutEdge.labelX = (start->center.x() + end->center.x()) / 2.0;
      layoutEdge.labelY = (start->center.y() + end->center.y()) / 2.0;
      layoutEdge.labelSize = label.size;
      layoutEdge.labelDocument = label.document;
    }
    flowLayout.edges.append(layoutEdge);
    BlockEdgeGeometry geometry;
    geometry.id = edge.id;
    geometry.start = edge.start;
    geometry.end = edge.end;
    geometry.points = points;
    geometry.path = layoutEdge.path;
    if (layoutEdge.hasLabelPosition)
      geometry.labelBounds = QRectF(QPointF(layoutEdge.labelX - layoutEdge.labelSize.width()/2.0,
                                             layoutEdge.labelY - layoutEdge.labelSize.height()/2.0),
                                     layoutEdge.labelSize);
    scene.edges.append(geometry);
  }

  flowscene::FlowSceneTextOptions textOptions;
  textOptions.nodeHtmlLabels = config.htmlLabels;
  textOptions.auxiliaryHtmlLabels = config.htmlLabels;
  textOptions.css = paintCss;
  scene.flow = flowscene::buildFlowScene(flowData, flowLayout, theme, look,
                                         config.handDrawnSeed, textOptions);
  scene.flow.markerDiagramType = QStringLiteral("block");
  scene.flow.bounds = scene.bounds;
  for (qsizetype i = 0; i < flat.size() && i < scene.flow.nodes.size(); ++i) {
    const LayoutNode& layoutNode = *flat.at(i);
    auto& flowNode = scene.flow.nodes[i];
    if (layoutNode.block.type == QLatin1String("composite")) {
      flowNode.fill = theme.clusterBkg;
      flowNode.stroke = theme.clusterBorder;
      flowNode.label.color = theme.titleColor;
    }
    if (layoutNode.block.type == QLatin1String("block_arrow")) {
      flowNode.shapeKind = QStringLiteral("polygon");
      flowNode.points = blockArrowPoints(layoutNode);
      QPainterPath path;
      if (!flowNode.points.isEmpty()) {
        path.moveTo(flowNode.points.first());
        for (qsizetype p = 1; p < flowNode.points.size(); ++p) path.lineTo(flowNode.points.at(p));
        path.closeSubpath();
      }
      flowNode.shapePaths = {{path, true, true, {}, {}}};
    }
  }
  return scene;
}

void BlockScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  // blockRenderer appends every node recursively, then every edge to the same
  // group. Paint two projections of the immutable FlowScene to preserve that
  // DOM order while reusing the shared shape/marker/text painter.
  flowscene::FlowScene nodeLayer = flow;
  nodeLayer.edges.clear();
  flowscene::paintFlowScene(nodeLayer, painter, fontFamily,
                            flowscene::PaintMode::Color, options);
  flowscene::FlowScene edgeLayer = flow;
  edgeLayer.nodes.clear();
  edgeLayer.clusters.clear();
  flowscene::paintFlowScene(edgeLayer, painter, fontFamily,
                            flowscene::PaintMode::Color, options);
}

QJsonObject BlockScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("rasterBounds")] = rectJson(rasterBounds);
  root[QStringLiteral("contentBounds")] = rectJson(contentBounds);
  root[QStringLiteral("viewBox")] = viewBoxAttribute;
  root[QStringLiteral("useMaxWidth")] = useMaxWidth;
  QJsonArray nodeArray;
  for (const auto& node : nodes) {
    nodeArray.append(QJsonObject{{QStringLiteral("id"), node.id},
                                 {QStringLiteral("type"), node.type},
                                 {QStringLiteral("label"), node.label},
                                 {QStringLiteral("cx"), node.center.x()},
                                 {QStringLiteral("cy"), node.center.y()},
                                 {QStringLiteral("layoutWidth"), node.layoutSize.width()},
                                 {QStringLiteral("layoutHeight"), node.layoutSize.height()},
                                 {QStringLiteral("width"), node.paintSize.width()},
                                 {QStringLiteral("height"), node.paintSize.height()}});
  }
  root[QStringLiteral("nodes")] = nodeArray;
  QJsonArray edgeArray;
  for (const auto& edge : edges)
    edgeArray.append(QJsonObject{{QStringLiteral("id"), edge.id},
                                 {QStringLiteral("start"), edge.start},
                                 {QStringLiteral("end"), edge.end},
                                 {QStringLiteral("path"), edge.path}});
  root[QStringLiteral("edges")] = edgeArray;
  root[QStringLiteral("flow")] = flow.toJsonObject();
  return root;
}

}  // namespace muffin::mermaid::block
