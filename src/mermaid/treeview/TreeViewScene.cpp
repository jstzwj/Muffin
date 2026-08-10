#include "mermaid/treeview/TreeViewScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/treeview/TreeViewScenePainter.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace muffin::mermaid::treeview {
namespace {

struct JsPrimitive {
  bool string = false;
  QString text;
  double number = 0.0;
};

JsPrimitive primitive(const QJsonValue& value) {
  if (value.isString()) return {true, value.toString(), 0.0};
  if (value.isBool()) return {false, {}, value.toBool() ? 1.0 : 0.0};
  if (value.isDouble()) return {false, {}, value.toDouble()};
  return {false, {}, 0.0};
}

QString jsString(const JsPrimitive& value) {
  return value.string ? value.text : editor::jsNumberToString(value.number);
}

double jsNumber(const JsPrimitive& value) {
  return value.string ? editor::jsNumberValue(QJsonValue(value.text))
                      : value.number;
}

JsPrimitive jsAdd(const JsPrimitive& left, const JsPrimitive& right) {
  if (left.string || right.string)
    return {true, jsString(left) + jsString(right), 0.0};
  return {false, {}, left.number + right.number};
}

JsPrimitive jsMultiply(const JsPrimitive& left, double right) {
  return {false, {}, jsNumber(left) * right};
}

qreal svgNumber(const QString& attribute) {
  static const QRegularExpression number(
      QStringLiteral(R"(^[\t ]*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[\t ]*$)"));
  if (!number.match(attribute).hasMatch()) return 0.0;
  bool ok = false;
  const qreal result = attribute.trimmed().toDouble(&ok);
  return ok && std::isfinite(result) ? result : 0.0;
}

QString scalarString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return editor::jsNumberToString(value.toDouble());
  if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                            : QStringLiteral("false");
  return {};
}

bool hasHighlightClass(const QString& cssClass) {
  return cssClass.split(QRegularExpression(QStringLiteral(R"(\s+)")),
                        Qt::SkipEmptyParts)
      .contains(QStringLiteral("highlight"));
}

flowchart::FlowLabelDocument textDocument(const QString& text, bool bold,
                                          bool italic) {
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = bold ? QFont::Bold : QFont::Normal;
  document.baseStyle = italic ? QFont::StyleItalic : QFont::StyleNormal;
  return document;
}

qreal textInkWidth(const QString& text, const TreeViewSceneStyle& style,
                    qreal fontSize, bool bold, bool italic) {
  if (!(fontSize > 0.0) || text.isEmpty()) return 0.0;
  const QFont font = flowchart::makeFlowLabelFont(
      style.fontFamily, fontSize, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  const QFontMetricsF metrics(font);
  const flowchart::FlowLabelDocument document = textDocument(text, bold, italic);
  const std::optional<qreal> designAdvance =
      flowchart::measureOpenTypeDesignAdvance(document, style.fontFamily,
                                              fontSize);
  const qreal advance = designAdvance.has_value()
      ? std::ceil(*designAdvance * 64.0) / 64.0
      : metrics.horizontalAdvance(text);
  if (bold) {
    // The bundled Noto face is Regular. Chromium synthesizes bold for the
    // directory class; getBBox includes the emboldened outline rather than
    // only ShapeResult's advance. DirectWrite reports the same outline one
    // small CSS-pixel fringe narrower. The slash has both negative and
    // positive bearings, hence its stable three-pixel expansion.
    const qreal outline = metrics.boundingRect(text).width();
    return std::max(advance, outline) +
           (text == QLatin1String("/") ? 3.0 : fontSize * 0.0390625);
  }
  if (italic) {
    // Synthetic oblique extends the final glyph past its advance. Chromium's
    // SVG text run keeps the inter-word cell boundary in the ink union, while
    // a single uninterrupted run only contributes the terminal fringe.
    const bool multipleRuns = text.contains(QRegularExpression(
        QStringLiteral(R"([\t\n\f\r ])")));
    return advance + fontSize * (multipleRuns ? 0.15625 : 0.0625);
  }
  // Skia's SVG glyph cell retains the terminal `t` outline fringe beyond the
  // HarfBuzz advance (about 0.22 CSS px for bundled Noto Sans).
  return advance + (text.endsWith(QLatin1Char('t')) ? 0.25 : 0.0);
}

qreal textHeight(const TreeViewSceneStyle& style, qreal fontSize, bool bold,
                 bool italic) {
  if (!(fontSize > 0.0)) return 0.0;
  return flowchart::flowLabelFontBoundingMetrics(
             style.fontFamily, fontSize, bold ? QFont::Bold : QFont::Normal,
             italic ? QFont::StyleItalic : QFont::StyleNormal)
      .height();
}

QRectF textInkBounds(const QString& text, const QPointF& position,
                     const TreeViewSceneStyle& style, qreal fontSize,
                     bool bold, bool italic) {
  if (!(fontSize > 0.0) || text.isEmpty()) return {};
  const QFont font = flowchart::makeFlowLabelFont(
      style.fontFamily, fontSize, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  const QFontMetricsF metrics(font);
  const qreal baseline =
      position.y() + (metrics.ascent() - metrics.descent()) / 2.0;
  return metrics.boundingRect(text).translated(position.x(), baseline);
}

QString detectedIcon(const TreeViewNode& node, const TreeViewConfig& config) {
  if (node.hasIcon) return node.icon == QLatin1String("none") ? QString() : node.icon;
  if (!editor::truthyConfigValue(config.showIcons)) return {};
  if (!node.directory) {
    const QJsonValue filename = config.filenameIcons.value(node.name);
    if (filename.isString() && !filename.toString().isEmpty()) {
      return filename.toString() == QLatin1String("none") ? QString()
                                                           : filename.toString();
    }
    const int dot = node.name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
      const QString extension = node.name.mid(dot).toLower();
      QJsonValue mapped = config.extensionIcons.value(extension);
      if (mapped.isUndefined())
        mapped = config.extensionIcons.value(extension.mid(1));
      if (mapped.isString())
        return mapped.toString() == QLatin1String("none") ? QString()
                                                           : mapped.toString();
    }
  }
  return node.directory ? QStringLiteral("folder") : QStringLiteral("file");
}

QString qualifiedIcon(const QString& icon, const TreeViewConfig& config) {
  if (icon.isEmpty() || icon.contains(QLatin1Char(':'))) return icon;
  if (icon == QLatin1String("folder") || icon == QLatin1String("file") ||
      config.defaultIconPack.isEmpty())
    return QStringLiteral("mermaid-treeview:") + icon;
  return config.defaultIconPack + QLatin1Char(':') + icon;
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

QJsonObject pointJson(const QPointF& point) {
  return {{QStringLiteral("x"), point.x()},
          {QStringLiteral("y"), point.y()}};
}

QJsonObject textJson(const TreeViewTextGeometry& text) {
  return {{QStringLiteral("class"), text.cssClass},
          {QStringLiteral("text"), text.text},
          {QStringLiteral("position"), pointJson(text.position)},
          {QStringLiteral("inkBounds"), rectJson(text.inkBounds)},
          {QStringLiteral("layoutWidth"), text.layoutWidth},
          {QStringLiteral("fontSize"), text.fontSize},
          {QStringLiteral("bold"), text.bold},
          {QStringLiteral("italic"), text.italic},
          {QStringLiteral("fill"), text.fill}};
}

}  // namespace

void TreeViewScene::paint(QPainter& painter,
                          const MermaidPaintOptions& options) const {
  paintTreeViewScene(painter, *this, options);
}

QJsonObject TreeViewScene::toJsonObject() const {
  QJsonArray nodeArray;
  for (const TreeViewNodeGeometry& node : nodes) {
    nodeArray.append(
        QJsonObject{{QStringLiteral("id"), node.id},
                    {QStringLiteral("depth"), node.depth},
                    {QStringLiteral("bbox"), rectJson(node.bbox)},
                    {QStringLiteral("xAttribute"), node.xAttribute},
                    {QStringLiteral("iconReserved"), node.iconReserved},
                    {QStringLiteral("iconName"), node.iconName},
                    {QStringLiteral("label"), textJson(node.label)},
                    {QStringLiteral("hasDescription"), node.hasDescription},
                    {QStringLiteral("description"), textJson(node.description)},
                    {QStringLiteral("highlighted"), node.highlighted},
                    {QStringLiteral("highlightRect"), rectJson(node.highlightRect)},
                    {QStringLiteral("paintOrder"), node.groupPaintOrder}});
  }
  QJsonArray lineArray;
  for (const TreeViewLineGeometry& line : lines) {
    lineArray.append(
        QJsonObject{{QStringLiteral("start"), pointJson(line.start)},
                    {QStringLiteral("end"), pointJson(line.end)},
                    {QStringLiteral("x1"), line.x1Attribute},
                    {QStringLiteral("y1"), line.y1Attribute},
                    {QStringLiteral("x2"), line.x2Attribute},
                    {QStringLiteral("y2"), line.y2Attribute},
                    {QStringLiteral("stroke"), line.stroke},
                    {QStringLiteral("strokeWidth"), line.strokeWidthAttribute},
                    {QStringLiteral("paintOrder"), line.paintOrder}});
  }
  QJsonArray defs;
  for (const QString& icon : iconDefs) defs.append(icon);
  return {{QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("totalWidth"), totalWidth},
          {QStringLiteral("totalHeight"), totalHeight},
          {QStringLiteral("useMaxWidth"), useMaxWidth},
          {QStringLiteral("nodes"), nodeArray},
          {QStringLiteral("lines"), lineArray},
          {QStringLiteral("iconDefs"), defs}};
}

TreeViewScene buildTreeViewScene(const TreeViewData& data,
                                 TreeViewConfig config,
                                 TreeViewSceneStyle style) {
  TreeViewScene scene;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.useMaxWidth = editor::truthyConfigValue(scene.config.useMaxWidth);

  const JsPrimitive rowIndent = primitive(scene.config.rowIndent);
  const JsPrimitive paddingX = primitive(scene.config.paddingX);
  const JsPrimitive paddingY = primitive(scene.config.paddingY);
  const JsPrimitive lineThickness = primitive(scene.config.lineThickness);
  const double paddingXNumber = jsNumber(paddingX);
  const double paddingYNumber = jsNumber(paddingY);
  const double lineThicknessNumber = jsNumber(lineThickness);
  const QString lineThicknessAttribute = jsString(lineThickness);
  const qreal usedLineThickness = svgNumber(lineThicknessAttribute);
  const CssLengthContext rootContext =
      editor::pieCssLengthContext(scene.style.fontFamily,
                                  scene.style.rootFontSize);
  const qreal labelFontSize =
      editor::cssFontSizePx(scene.style.labelFontSize, rootContext);
  const qreal regularHeight =
      textHeight(scene.style, labelFontSize, false, false);
  const qreal boldHeight = textHeight(scene.style, labelFontSize, true, false);

  int paintOrder = 0;
  QVector<JsPrimitive> labelRightEdges;
  QStringList usedIcons;

  auto appendLine = [&](JsPrimitive x1, JsPrimitive y1, JsPrimitive x2,
                        JsPrimitive y2) {
    TreeViewLineGeometry line;
    line.x1Attribute = jsString(x1);
    line.y1Attribute = jsString(y1);
    line.x2Attribute = jsString(x2);
    line.y2Attribute = jsString(y2);
    line.start = QPointF(svgNumber(line.x1Attribute),
                         svgNumber(line.y1Attribute));
    line.end =
        QPointF(svgNumber(line.x2Attribute), svgNumber(line.y2Attribute));
    line.stroke = scene.style.lineColor;
    line.strokeWidthAttribute = lineThicknessAttribute;
    line.strokeWidth = usedLineThickness;
    line.paintOrder = paintOrder++;
    scene.lines.append(std::move(line));
  };

  std::function<void(int, int)> processNode = [&](int nodeIndex, int depth) {
    const TreeViewNode& source = data.nodes.at(nodeIndex);
    const JsPrimitive combinedIndent = jsAdd(rowIndent, paddingX);
    const JsPrimitive indent = jsMultiply(combinedIndent, depth);
    const double x = jsNumber(indent);
    const double y = scene.totalHeight;
    const QString icon = qualifiedIcon(detectedIcon(source, scene.config),
                                       scene.config);
    const bool showIcon = !icon.isEmpty();
    if (showIcon && !usedIcons.contains(icon)) usedIcons.append(icon);

    TreeViewNodeGeometry node;
    node.id = source.id;
    node.depth = depth;
    node.xAttribute = jsString(indent);
    node.iconReserved = showIcon;
    node.iconName = icon;
    node.groupPaintOrder = paintOrder++;
    node.label.cssClass = QStringLiteral("treeView-node-label");
    if (source.directory)
      node.label.cssClass += QStringLiteral(" treeView-node-dir");
    if (!source.cssClass.isEmpty())
      node.label.cssClass += QLatin1Char(' ') + source.cssClass;
    node.label.text = source.name;
    node.label.bold = source.directory;
    node.label.fontSize = labelFontSize;
    node.label.fill = scene.style.labelColor;
    node.label.layoutWidth = textInkWidth(
        source.name, scene.style, labelFontSize, source.directory, false);
    const qreal labelHeight = source.directory ? boldHeight : regularHeight;
    const qreal height = labelHeight + paddingYNumber * 2.0;
    JsPrimitive labelX = jsAdd({false, {}, x}, paddingX);
    labelX = jsAdd(labelX, {false, {}, showIcon ? 18.0 : 0.0});
    const qreal labelXUsed = svgNumber(jsString(labelX));
    const qreal centerY = y + height / 2.0;
    node.label.position = QPointF(labelXUsed, centerY);
    node.label.inkBounds = textInkBounds(
        source.name, node.label.position, scene.style, labelFontSize,
        source.directory, false);
    const qreal width = node.label.layoutWidth + paddingXNumber * 2.0 +
                        (showIcon ? 18.0 : 0.0);
    node.bbox = QRectF(x, y, width, height);
    node.highlighted = hasHighlightClass(source.cssClass);
    if (node.highlighted)
      node.highlightRect = QRectF(x, y + 1.0, 0.0, height - 2.0);
    node.hasDescription = !source.description.isEmpty();
    node.description.text = source.description;
    node.description.cssClass = QStringLiteral("treeView-node-description");
    node.description.fontSize = labelFontSize;
    node.description.italic = true;
    node.description.fill = scene.style.descriptionColor;
    node.description.layoutWidth = textInkWidth(
        source.description, scene.style, labelFontSize, false, true);

    scene.nodes.append(std::move(node));
    labelRightEdges.append(jsAdd(labelX,
                                 {false, {}, scene.nodes.back().label.layoutWidth}));
    scene.totalWidth = std::max(scene.totalWidth, x + width);
    scene.totalHeight += height;

    appendLine({false, {}, x - jsNumber(rowIndent)},
               {false, {}, centerY}, {false, {}, x},
               {false, {}, centerY});

    for (const int child : source.children) processNode(child, depth + 1);
    if (!source.children.isEmpty()) {
      const int lastChildId = source.children.back();
      const auto lastIt = std::find_if(
          scene.nodes.crbegin(), scene.nodes.crend(),
          [lastChildId](const TreeViewNodeGeometry& value) {
            return value.id == lastChildId;
          });
      if (lastIt != scene.nodes.crend()) {
        const JsPrimitive verticalX = jsAdd({false, {}, x}, paddingX);
        appendLine(verticalX, {false, {}, y + height}, verticalX,
                   {false, {}, lastIt->bbox.y() + lastIt->bbox.height() / 2.0 +
                                      lineThicknessNumber / 2.0});
      }
    }
  };

  processNode(data.rootIndex, 0);

  bool anyDescription = false;
  for (const TreeViewNodeGeometry& node : scene.nodes)
    anyDescription = anyDescription || node.hasDescription;
  if (anyDescription) {
    qreal maxLabelRight = -std::numeric_limits<qreal>::infinity();
    for (const JsPrimitive& edge : labelRightEdges)
      maxLabelRight = std::max(maxLabelRight, jsNumber(edge));
    const qreal descriptionX = maxLabelRight + 16.0;
    for (TreeViewNodeGeometry& node : scene.nodes) {
      if (!node.hasDescription) continue;
      node.description.position =
          QPointF(descriptionX, node.bbox.y() + node.bbox.height() / 2.0);
      node.description.inkBounds = textInkBounds(
          node.description.text, node.description.position, scene.style,
          labelFontSize, false, true);
      const JsPrimitive descriptionRight = jsAdd(
          {false, {}, descriptionX + node.description.layoutWidth}, paddingX);
      scene.totalWidth =
          std::max(scene.totalWidth, jsNumber(descriptionRight));
    }
  }

  for (TreeViewNodeGeometry& node : scene.nodes) {
    if (!node.highlighted) continue;
    const qreal rectWidth = scene.totalWidth - node.bbox.x() + 8.0;
    node.highlightRect.setWidth(rectWidth);
    scene.totalWidth =
        std::max(scene.totalWidth, node.bbox.x() + rectWidth + 2.0);
  }

  scene.iconDefs = std::move(usedIcons);
  scene.bounds = QRectF(-lineThicknessNumber / 2.0, 0.0, scene.totalWidth,
                        scene.totalHeight);
  return scene;
}

}  // namespace muffin::mermaid::treeview
