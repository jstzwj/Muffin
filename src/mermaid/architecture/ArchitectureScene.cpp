#include "mermaid/architecture/ArchitectureScene.h"

#include "mermaid/architecture/ArchitectureFcoseLayout.h"
#include "mermaid/architecture/ArchitectureScenePainter.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::architecture {
namespace {

qreal number(const QJsonValue& value, qreal fallback) {
  const double result = editor::jsNumberValue(value);
  return std::isfinite(result) ? qreal(result) : fallback;
}

bool truthy(const QJsonValue& value) {
  return editor::truthyConfigValue(value);
}

qreal layoutUnit(qreal value) {
  return std::round(value * 64.0) / 64.0;
}

QRectF textBounds(const QString& text, const QString& family, qreal fontSize,
                  const QPointF& anchor, Qt::Alignment horizontal) {
  if (text.isEmpty() || !(fontSize > 0.0)) return {};
  const auto cssFont = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(family), fontSize);
  QStringList families;
  for (QString item : family.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    item = item.trimmed();
    if (item.size() >= 2 &&
        ((item.front() == QLatin1Char('"') && item.back() == QLatin1Char('"')) ||
         (item.front() == QLatin1Char('\'') && item.back() == QLatin1Char('\''))))
      item = item.mid(1, item.size() - 2);
    if (!item.isEmpty()) families.append(item);
  }
  QFont font = cssFont.font;
  if (!families.isEmpty()) font.setFamilies(families);
  const qreal width = layoutUnit(
      QFontMetricsF(font).horizontalAdvance(text) * cssFont.scale);
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      family, fontSize, QFont::Normal, QFont::StyleNormal);
  qreal x = anchor.x();
  if (horizontal.testFlag(Qt::AlignHCenter)) x -= width / 2.0;
  else if (horizontal.testFlag(Qt::AlignRight)) x -= width;
  // createText's SVG text uses dy=1em and y=-10.1 before its group
  // translation. With Noto 16 this places the ink top at 3.28 and gives the
  // browser's hhea 22px box. Scale the same offsets with the CSS font size.
  const qreal top = layoutUnit(anchor.y() + fontSize * 0.205);
  return QRectF(x, top, width, metrics.ascent + metrics.descent);
}

QPointF port(const ArchitectureNodeGeometry& node, QChar direction) {
  const QRectF rect(node.topLeft,
                    QSizeF(node.localBounds.width(), node.localBounds.height()));
  if (direction == QLatin1Char('L')) return {rect.left(), rect.center().y()};
  if (direction == QLatin1Char('R')) return {rect.right(), rect.center().y()};
  if (direction == QLatin1Char('T')) return {rect.center().x(), rect.top()};
  return {rect.center().x(), rect.bottom()};
}

QPolygonF arrowPolygon(QChar direction, qreal size) {
  if (direction == QLatin1Char('L'))
    return {{size, size / 2.0}, {0, size}, {0, 0}};
  if (direction == QLatin1Char('R'))
    return {{0, size / 2.0}, {size, 0}, {size, size}};
  if (direction == QLatin1Char('T'))
    return {{0, 0}, {size, 0}, {size / 2.0, size}};
  return {{size / 2.0, 0}, {size, size}, {0, size}};
}

QPointF arrowPosition(QChar direction, const QPointF& endpoint, qreal size) {
  if (direction == QLatin1Char('L')) return {endpoint.x() - size + 2, endpoint.y() - size / 2.0};
  if (direction == QLatin1Char('R')) return {endpoint.x() - 2, endpoint.y() - size / 2.0};
  if (direction == QLatin1Char('T')) return {endpoint.x() - size / 2.0, endpoint.y() - size + 2};
  return {endpoint.x() - size / 2.0, endpoint.y() - 2};
}

QRectF edgeLabelBounds(const QString& text, const QString& fontFamily,
                       qreal fontSize, const QPointF& middle,
                       QChar sourceDirection, QChar targetDirection) {
  if (text.isEmpty() || !(fontSize > 0.0)) return {};
  flowchart::FlowLabelDocument document;
  document.text = text;
  QRectF autoBaseline = flowchart::measureFlowSvgTextBounds(
      document, fontFamily, fontSize);
  autoBaseline.setWidth(
      std::ceil(autoBaseline.width() * 64.0 - 1e-9) / 64.0);
  autoBaseline.setHeight(
      std::ceil(autoBaseline.height() * 64.0 - 1e-9) / 64.0);
  autoBaseline.moveLeft(-autoBaseline.width() / 2.0);
  const bool sourceX = sourceDirection == QLatin1Char('L') ||
                       sourceDirection == QLatin1Char('R');
  const bool targetX = targetDirection == QLatin1Char('L') ||
                       targetDirection == QLatin1Char('R');
  if (sourceX == targetX) {
    QRectF middleBaseline = autoBaseline;
    middleBaseline.moveTop(fontSize * 0.205);
    QTransform transform;
    transform.translate(middle.x(), middle.y());
    if (!sourceX) transform.rotate(-90.0);
    return transform.mapRect(middleBaseline);
  }

  int xFactor = -1;
  int yFactor = 1;
  const QString pair = QString(sourceDirection) + targetDirection;
  if (pair == QLatin1String("LT") || pair == QLatin1String("TL")) {
    xFactor = 1;
    yFactor = 1;
  } else if (pair == QLatin1String("BL") || pair == QLatin1String("LB")) {
    xFactor = 1;
    yFactor = -1;
  } else if (pair == QLatin1String("BR") || pair == QLatin1String("RB")) {
    xFactor = -1;
    yFactor = -1;
  }
  const qreal rotation = -qreal(xFactor * yFactor) * 45.0;
  QTransform unpivoted;
  unpivoted.rotate(rotation);
  const QRectF rotatedBox = unpivoted.mapRect(autoBaseline);
  QTransform rotated;
  rotated.translate(0.0, autoBaseline.height() / 2.0);
  rotated.rotate(rotation);
  rotated.translate(0.0, -autoBaseline.height() / 2.0);
  QRectF result = rotated.mapRect(autoBaseline);
  result.translate(middle.x() + xFactor * rotatedBox.width() / 2.0,
                   middle.y() - autoBaseline.height() / 2.0 +
                       yFactor * rotatedBox.height() / 2.0);
  return result;
}

QString point(qreal value) { return QString::number(value, 'g', 17); }

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

// Blink stores SVG object bounds as FloatRects. UniteEvenIfEmpty computes the
// far edges from x + size, then stores a newly rounded size. Repeating that at
// each nested <g> is observable in setupGraphViewbox, so preserve both the
// Float32 fields and the DOM union order.
struct SvgFloatRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  QRectF exposedRect() const {
    return QRectF(qreal(x), qreal(y), qreal(width), qreal(height));
  }
};

SvgFloatRect svgFloatBounds(const QRectF& rect) {
  return {float(rect.x()), float(rect.y()), float(rect.width()),
          float(rect.height())};
}

SvgFloatRect svgTranslateFloatBounds(const QRectF& rect,
                                     const QPointF& offset) {
  const SvgFloatRect value = svgFloatBounds(rect);
  const float dx = float(offset.x());
  const float dy = float(offset.y());
  return {float(value.x + dx), float(value.y + dy), value.width,
          value.height};
}

SvgFloatRect svgTranslateFloatBounds(const SvgFloatRect& value,
                                     const QPointF& offset) {
  const float dx = float(offset.x());
  const float dy = float(offset.y());
  return {float(value.x + dx), float(value.y + dy), value.width,
          value.height};
}

SvgFloatRect svgUniteFloatBounds(const SvgFloatRect& lhs,
                                 const SvgFloatRect& rhs) {
  const float left = std::min(lhs.x, rhs.x);
  const float top = std::min(lhs.y, rhs.y);
  const float right = std::max(float(lhs.x + lhs.width),
                               float(rhs.x + rhs.width));
  const float bottom = std::max(float(lhs.y + lhs.height),
                                float(rhs.y + rhs.height));
  return {left, top, float(right - left), float(bottom - top)};
}

SvgFloatRect svgPolylineFloatBounds(const QVector<QPointF>& points) {
  if (points.isEmpty()) return {};
  float minX = float(points.first().x());
  float maxX = minX;
  float minY = float(points.first().y());
  float maxY = minY;
  for (const QPointF& point : points) {
    const float x = float(point.x());
    const float y = float(point.y());
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }
  return {minX, minY, float(maxX - minX), float(maxY - minY)};
}

}  // namespace

ArchitectureScene buildArchitectureScene(const ArchitectureData& data,
                                           ArchitectureConfig config,
                                           ArchitectureSceneStyle style) {
  ArchitectureScene scene;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.useMaxWidth = truthy(scene.config.useMaxWidth);
  const qreal padding = number(scene.config.padding, 40.0);
  const qreal iconSize = number(scene.config.iconSize, 80.0);
  const qreal fontSize = number(scene.config.fontSize, 16.0);

  QHash<QString, qreal> renderedHeights;
  for (const ArchitectureService& service : data.services) {
    QRectF painted(0, 0, iconSize, iconSize);
    if (service.hasTitle) {
      const QRectF label = textBounds(service.title, scene.style.fontFamily,
                                      fontSize, QPointF(iconSize / 2.0, iconSize),
                                      Qt::AlignHCenter);
      painted = painted.united(label);
    }
    renderedHeights.insert(service.id, painted.height());
  }
  for (const ArchitectureJunction& junction : data.junctions)
    renderedHeights.insert(junction.id, iconSize);

  ArchitectureFcoseOptions layoutOptions;
  layoutOptions.iconSize = iconSize;
  layoutOptions.padding = padding;
  layoutOptions.fontSize = fontSize;
  layoutOptions.nodeSeparation = number(scene.config.nodeSeparation, 75.0);
  layoutOptions.idealEdgeLengthMultiplier =
      number(scene.config.idealEdgeLengthMultiplier, 1.5);
  layoutOptions.edgeElasticity = number(scene.config.edgeElasticity, 0.45);
  layoutOptions.numIter = std::max(0, int(number(scene.config.numIter, 2500.0)));
  layoutOptions.randomize = truthy(scene.config.randomize);
  layoutOptions.seed = quint32(std::max(0.0, number(scene.config.seed, 1.0)));
  const ArchitectureFcoseResult layout =
      layoutArchitectureFcose(data, layoutOptions, renderedHeights);

  QHash<QString, int> nodeIndex;
  for (const ArchitectureService& service : data.services) {
    ArchitectureNodeGeometry node;
    node.kind = ArchitectureNodeKind::Service;
    node.id = service.id;
    node.icon = service.icon;
    node.iconText = service.iconText;
    node.title = service.title;
    node.parent = service.parent;
    node.topLeft = layout.topLeft.value(service.id);
    node.localBounds = QRectF(0, 0, iconSize, iconSize);
    node.paintedBounds = QRectF(0, 0, iconSize,
                                renderedHeights.value(service.id, iconSize));
    nodeIndex.insert(node.id, scene.nodes.size());
    scene.nodes.append(std::move(node));
  }
  for (const ArchitectureJunction& junction : data.junctions) {
    ArchitectureNodeGeometry node;
    node.kind = ArchitectureNodeKind::Junction;
    node.id = junction.id;
    node.parent = junction.parent;
    node.topLeft = layout.topLeft.value(junction.id);
    node.localBounds = node.paintedBounds = QRectF(0, 0, iconSize, iconSize);
    nodeIndex.insert(node.id, scene.nodes.size());
    scene.nodes.append(std::move(node));
  }
  for (const ArchitectureGroup& source : data.groups) {
    ArchitectureGroupGeometry group;
    group.id = source.id;
    group.icon = source.icon;
    group.title = source.title;
    group.parent = source.parent;
    const QRectF layoutRect = layout.groups.value(source.id);
    const bool hasChildGroup = std::any_of(
        data.groups.cbegin(), data.groups.cend(), [&](const ArchitectureGroup& candidate) {
          return candidate.hasParent && candidate.parent == source.id;
        });
    const qreal nestedExpansion = hasChildGroup ? 1.5 : 0.0;
    // drawGroups() uses Cytoscape's rendered compound bounding box (2.5px
    // expansion per side), then offsets the rectangle by half the icon size.
    group.rect = QRectF(layoutRect.x() + iconSize / 2.0 - 2.5 - nestedExpansion,
                        layoutRect.y() + iconSize / 2.0 - 2.5 - nestedExpansion,
                        layoutRect.width() + 5.0 + nestedExpansion,
                        layoutRect.height() + 5.0 + 2.0 * nestedExpansion +
                            (source.title.isEmpty() ? 0.0 : fontSize + 1.0));
    scene.groups.append(std::move(group));
  }

  const qreal arrowSize = iconSize / 6.0;
  for (qsizetype i = 0; i < data.edges.size(); ++i) {
    const ArchitectureEdge& source = data.edges.at(i);
    if (!nodeIndex.contains(source.lhsId) || !nodeIndex.contains(source.rhsId))
      continue;
    const ArchitectureNodeGeometry& lhs = scene.nodes.at(nodeIndex.value(source.lhsId));
    const ArchitectureNodeGeometry& rhs = scene.nodes.at(nodeIndex.value(source.rhsId));
    QPointF start = port(lhs, source.lhsDir);
    QPointF end = port(rhs, source.rhsDir);
    const QPointF routeStart = start;
    const QPointF routeEnd = end;
    const qreal groupShift = padding + 4.0;
    const auto shiftGroup = [&](QPointF& value, QChar direction) {
      if (direction == QLatin1Char('L')) value.rx() -= groupShift;
      else if (direction == QLatin1Char('R')) value.rx() += groupShift;
      else if (direction == QLatin1Char('T')) value.ry() -= groupShift;
      else value.ry() += groupShift + 18.0;
    };
    if (source.lhsGroup) shiftGroup(start, source.lhsDir);
    if (source.rhsGroup) shiftGroup(end, source.rhsDir);
    if (!source.lhsGroup && lhs.kind == ArchitectureNodeKind::Junction)
      start = lhs.topLeft + QPointF(iconSize / 2.0, iconSize / 2.0);
    if (!source.rhsGroup && rhs.kind == ArchitectureNodeKind::Junction)
      end = rhs.topLeft + QPointF(iconSize / 2.0, iconSize / 2.0);

    QPointF middle = (routeStart + routeEnd) / 2.0;
    const bool lhsX = source.lhsDir == QLatin1Char('L') || source.lhsDir == QLatin1Char('R');
    const bool rhsX = source.rhsDir == QLatin1Char('L') || source.rhsDir == QLatin1Char('R');
    if (lhsX != rhsX)
      middle = lhsX ? QPointF(routeEnd.x(), routeStart.y())
                    : QPointF(routeStart.x(), routeEnd.y());

    ArchitectureEdgeGeometry edge;
    edge.id = QStringLiteral("L_%1_%2_%3").arg(source.lhsId, source.rhsId).arg(i);
    edge.lhsId = source.lhsId;
    edge.rhsId = source.rhsId;
    edge.title = source.title;
    edge.points = {start, middle, end};
    edge.pathData = QStringLiteral("M %1,%2 L %3,%4 L%5,%6")
        .arg(point(start.x()), point(start.y()), point(middle.x()),
             point(middle.y()), point(end.x()), point(end.y()));
    const qreal edgeLeft = std::min({start.x(), middle.x(), end.x()});
    const qreal edgeTop = std::min({start.y(), middle.y(), end.y()});
    const qreal edgeRight = std::max({start.x(), middle.x(), end.x()});
    const qreal edgeBottom = std::max({start.y(), middle.y(), end.y()});
    edge.bounds = QRectF(QPointF(edgeLeft, edgeTop),
                         QPointF(edgeRight, edgeBottom));
    edge.labelBounds = edgeLabelBounds(
        source.title, scene.style.fontFamily, fontSize, middle,
        source.lhsDir, source.rhsDir);
    if (source.lhsInto)
      edge.arrows.append({source.lhsDir, arrowPosition(source.lhsDir, start, arrowSize),
                          arrowPolygon(source.lhsDir, arrowSize)});
    if (source.rhsInto)
      edge.arrows.append({source.rhsDir, arrowPosition(source.rhsDir, end, arrowSize),
                          arrowPolygon(source.rhsDir, arrowSize)});
    scene.edges.append(std::move(edge));
  }

  const auto includeFloat = [](SvgFloatRect& target, bool& hasTarget,
                               const SvgFloatRect& rect) {
    if (!hasTarget) {
      target = rect;
      hasTarget = true;
    } else {
      target = svgUniteFloatBounds(target, rect);
    }
  };

  SvgFloatRect edgeLayer;
  bool hasEdgeLayer = false;
  for (const ArchitectureEdgeGeometry& edge : scene.edges) {
    SvgFloatRect edgeGroup;
    bool hasEdgeGroup = false;
    includeFloat(edgeGroup, hasEdgeGroup,
                 svgPolylineFloatBounds(edge.points));
    if (!edge.labelBounds.isEmpty()) {
      includeFloat(edgeGroup, hasEdgeGroup,
                   svgFloatBounds(edge.labelBounds));
    }
    for (const ArchitectureArrowGeometry& arrow : edge.arrows) {
      const QRectF localArrowBounds = arrow.polygon.boundingRect();
      includeFloat(edgeGroup, hasEdgeGroup,
                   svgTranslateFloatBounds(localArrowBounds,
                                           arrow.position));
    }
    if (hasEdgeGroup)
      includeFloat(edgeLayer, hasEdgeLayer, edgeGroup);
  }
  SvgFloatRect serviceLayer;
  bool hasServiceLayer = false;
  for (const ArchitectureNodeGeometry& node : scene.nodes) {
    SvgFloatRect serviceGroup;
    bool hasServiceGroup = false;
    if (!node.title.isEmpty()) {
      const QRectF label = textBounds(
          node.title, scene.style.fontFamily, fontSize,
          QPointF(iconSize / 2.0, iconSize), Qt::AlignHCenter);
      includeFloat(serviceGroup, hasServiceGroup, svgFloatBounds(label));
    }
    includeFloat(serviceGroup, hasServiceGroup,
                 svgFloatBounds(node.localBounds));
    includeFloat(serviceLayer, hasServiceLayer,
                 svgTranslateFloatBounds(serviceGroup, node.topLeft));
  }

  SvgFloatRect groupLayer;
  bool hasGroupLayer = false;
  for (const ArchitectureGroupGeometry& group : scene.groups) {
    includeFloat(groupLayer, hasGroupLayer, svgFloatBounds(group.rect));
    if (!group.icon.isEmpty() || !group.title.isEmpty()) {
      // drawGroups appends a label-container sibling after the rectangle. Its
      // contents sit inside the background, but the sibling union still
      // reconstructs the FloatRect size before the architecture-groups layer
      // is returned to the SVG root.
      const QRectF innerLabel(group.rect.x() + 1.0,
                              group.rect.y() + 1.0, 1.0, 1.0);
      includeFloat(groupLayer, hasGroupLayer,
                   svgFloatBounds(innerLabel));
    }
  }

  SvgFloatRect rootBounds;
  bool hasContent = false;
  if (hasEdgeLayer) includeFloat(rootBounds, hasContent, edgeLayer);
  if (hasServiceLayer) includeFloat(rootBounds, hasContent, serviceLayer);
  if (hasGroupLayer) includeFloat(rootBounds, hasContent, groupLayer);
  const QRectF content = hasContent ? rootBounds.exposedRect() : QRectF();
  scene.contentBounds = content;
  scene.bounds = QRectF(content.x() - padding, content.y() - padding,
                        content.width() + 2.0 * padding,
                        content.height() + 2.0 * padding);
  scene.rasterBounds = QRectF(
      scene.bounds.topLeft(),
      QSizeF(qreal(qRound(scene.bounds.width())),
             qreal(qRound(scene.bounds.height()))));
  scene.viewBoxAttribute = QStringLiteral("%1 %2 %3 %4")
      .arg(point(scene.bounds.x()), point(scene.bounds.y()),
           point(scene.bounds.width()), point(scene.bounds.height()));
  return scene;
}

void ArchitectureScene::paint(QPainter& painter,
                              const MermaidPaintOptions& options) const {
  paintArchitectureScene(*this, painter, options);
}

QJsonObject ArchitectureScene::toJsonObject() const {
  QJsonObject root;
  root.insert(QStringLiteral("bounds"), rectJson(bounds));
  root.insert(QStringLiteral("contentBounds"), rectJson(contentBounds));
  root.insert(QStringLiteral("viewBox"), viewBoxAttribute);
  root.insert(QStringLiteral("useMaxWidth"), useMaxWidth);
  QJsonArray nodeArray;
  for (const ArchitectureNodeGeometry& node : nodes) {
    nodeArray.append(QJsonObject{
        {QStringLiteral("id"), node.id},
        {QStringLiteral("kind"), node.kind == ArchitectureNodeKind::Service
                                     ? QStringLiteral("service")
                                     : QStringLiteral("junction")},
        {QStringLiteral("icon"), node.icon},
        {QStringLiteral("iconText"), node.iconText},
        {QStringLiteral("title"), node.title},
        {QStringLiteral("x"), node.topLeft.x()},
        {QStringLiteral("y"), node.topLeft.y()},
        {QStringLiteral("bounds"), rectJson(node.paintedBounds)}});
  }
  root.insert(QStringLiteral("nodes"), nodeArray);
  QJsonArray groupArray;
  for (const ArchitectureGroupGeometry& group : groups)
    groupArray.append(QJsonObject{{QStringLiteral("id"), group.id},
                                  {QStringLiteral("rect"), rectJson(group.rect)},
                                  {QStringLiteral("icon"), group.icon},
                                  {QStringLiteral("title"), group.title}});
  root.insert(QStringLiteral("groups"), groupArray);
  QJsonArray edgeArray;
  for (const ArchitectureEdgeGeometry& edge : edges)
    edgeArray.append(QJsonObject{{QStringLiteral("id"), edge.id},
                                 {QStringLiteral("path"), edge.pathData},
                                 {QStringLiteral("title"), edge.title}});
  root.insert(QStringLiteral("edges"), edgeArray);
  return root;
}

}  // namespace muffin::mermaid::architecture
