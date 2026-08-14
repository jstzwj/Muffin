#include "mermaid/state/StateScene.h"
#include "mermaid/state/StateRough.h"
#include "mermaid/scene/SvgPathParse.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::state {
namespace {
flowchart::FlowLabelDocument prepareLabel(const QString& text, qreal fontSize) {
  auto document = flowchart::parseFlowLabel(text, QStringLiteral("markdown"), true);
  document.formattingContext =
      flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
  flowchart::prepareFlowLabelMath(document, fontSize);
  return document;
}
QStringList descriptions(const QJsonValue& value) {
  QStringList result;
  if (value.isArray())
    for (const QJsonValue& item : value.toArray()) result.append(item.toString());
  return result;
}
// Resolve the merged style cascade (node.styles) into paint values, defaulting
// to the caller-supplied theme strings. Mirrors classdiagram::applyNodeStyles;
// mermaid splits inline `style`/classDef by key (fill/stroke/color/stroke-width)
// with last-wins, stripping `!important`.
void applyStateNodeStyles(StateSceneNode& node, const QString& defaultFill,
                          const QString& defaultStroke, const QString& defaultText,
                          qreal defaultStrokeWidth) {
  node.fill = defaultFill;
  node.stroke = defaultStroke;
  node.textColor = defaultText;
  node.strokeWidth = defaultStrokeWidth;
  const auto apply = [&](const QString& declaration) {
    const int colon = declaration.indexOf(QLatin1Char(':'));
    if (colon < 0) return;
    const QString key = declaration.left(colon).trimmed();
    QString value = declaration.mid(colon + 1).trimmed();
    value.remove(QStringLiteral("!important"), Qt::CaseInsensitive);
    value = value.trimmed();
    if (key == QLatin1String("fill")) node.fill = value;
    else if (key == QLatin1String("stroke")) node.stroke = value;
    else if (key == QLatin1String("color")) node.textColor = value;
    else if (key == QLatin1String("stroke-width")) {
      if (value.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) value.chop(2);
      bool ok = false;
      const qreal width = value.toDouble(&ok);
      if (ok && width >= 0.0) node.strokeWidth = width;
    }
  };
  for (const QString& declaration : node.styles) apply(declaration);
}
const StateLayoutNodeInput* inputNode(const StateLayoutInput& input, const QString& id) {
  const auto it = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
      [&](const StateLayoutNodeInput& node) { return node.id == id; });
  return it == input.nodes.cend() ? nullptr : &*it;
}
void include(QRectF& bounds, bool& initialized, const QRectF& value) {
  if (!value.isValid()) return;
  if (!initialized) { bounds = value; initialized = true; }
  else bounds = bounds.united(value);
}
QRectF edgePaintBounds(const QVector<QPointF>& points,
                       const QVector<QVector<QPointF>>& segments) {
  QRectF bounds;
  bool initialized = false;
  for (const QPointF& point : points)
    include(bounds, initialized,
            QRectF(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0)));
  for (const QVector<QPointF>& segment : segments)
    for (const QPointF& point : segment)
      include(bounds, initialized,
              QRectF(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0)));
  return initialized ? bounds.adjusted(-12.0, -12.0, 12.0, 12.0) : QRectF{};
}
}

StateScene buildStateScene(const StateLayoutInput& input,
                           const StatePlacementResult& placement,
                           StateSceneStyle style,
                           const QVector<style::ClassDef>& classDefs,
                           const style::ThemeDefaults& themeDefaults,
                           bool handDrawn, quint32 handDrawnSeed) {
  StateScene scene;
  scene.style = std::move(style);
  scene.handDrawn = handDrawn;
  scene.handDrawnSeed = handDrawnSeed;
  bool initialized = false;
  for (const StatePlacementCluster& placed : placement.clusters) {
    const StateLayoutNodeInput* source = inputNode(input, placed.id);
    if (!source) continue;
    StateSceneNode node;
    node.id = source->id;
    node.shape = source->shape;
    node.label = source->label.toString();
    node.descriptions = descriptions(source->description);
    node.cssClasses = source->cssClasses;
    node.styles = source->styles;
    const QSizeF logicalSize = placed.logicalSize.isValid() &&
            !placed.logicalSize.isEmpty()
        ? placed.logicalSize : placed.size;
    node.bounds = QRectF(placed.center - QPointF(logicalSize.width() / 2.0,
                                                 logicalSize.height() / 2.0),
                         logicalSize);
    node.group = true;
    applyStateNodeStyles(node, scene.style.compositeTitleFill,
                         scene.style.compositeStroke,
                         scene.style.textColor, scene.style.strokeWidth);
    node.labelDocument = prepareLabel(node.label, scene.style.fontSize);
    const qreal titleHeight = flowchart::measureFlowLabel(
        node.labelDocument, scene.style.fontFamily, scene.style.fontSize,
        scene.style.lineHeight).height();
    const qreal innerY = node.bounds.top() + titleHeight + 2.0;
    node.innerBounds = QRectF(node.bounds.left(), innerY, node.bounds.width(),
                             std::max<qreal>(
                                 0.0, node.bounds.height() - titleHeight - 6.0));
    node.innerFill = source->cssClasses.contains(
                         QStringLiteral("statediagram-cluster-alt"))
        ? scene.style.compositeAltFill : scene.style.compositeFill;
    if (handDrawn) {
      node.roughDrawables = stateRoughClusterDrawables(
          node.bounds, titleHeight, handDrawnSeed,
          source->cssClasses.contains(
              QStringLiteral("statediagram-cluster-alt")),
          node.fill, node.innerFill, node.stroke, node.strokeWidth);
      node.paintedBounds = stateRoughBounds(node.roughDrawables);
    }
    scene.clusters.append(node);
    include(scene.bounds, initialized,
            handDrawn && node.paintedBounds.isValid()
                ? node.paintedBounds : node.bounds);
  }
  for (const StatePlacementNode& placed : placement.nodes) {
    const StateLayoutNodeInput* source = inputNode(input, placed.id);
    if (!source) continue;
    StateSceneNode node;
    node.id = source->id;
    node.shape = source->shape.isEmpty() ? QStringLiteral("rect") : source->shape;
    node.label = source->label.toString();
    node.descriptions = descriptions(source->description);
    node.cssClasses = source->cssClasses;
    node.styles = source->styles;
    node.labelDocument = prepareLabel(node.label, scene.style.fontSize);
    for (const QString& description : node.descriptions)
      node.descriptionDocuments.append(prepareLabel(description, scene.style.fontSize));
    node.bounds = QRectF(placed.center - QPointF(placed.paintedSize.width() / 2.0,
                                                 placed.paintedSize.height() / 2.0),
                         placed.paintedSize);
    const bool note = node.shape == QLatin1String("note");
    node.shapeVisible = scene.style.shapeVisible;
    applyStateNodeStyles(node,
        note ? scene.style.noteFill : scene.style.stateFill,
        note ? scene.style.noteStroke : scene.style.stateStroke,
        note ? scene.style.noteTextColor : scene.style.textColor,
        scene.style.strokeWidth);
    if (handDrawn) {
      const QRectF localBounds(-node.bounds.width() / 2.0,
                               -node.bounds.height() / 2.0,
                               node.bounds.width(), node.bounds.height());
      node.roughDrawables = stateRoughNodeDrawables(
          node.shape, localBounds, handDrawnSeed, node.strokeWidth);
      if (!node.descriptions.isEmpty()) {
        const qreal dividerY = localBounds.top() +
            scene.style.lineHeight + 16.0;
        node.roughDrawables.append(rough::roughNodeLineDrawable(
            QPointF(localBounds.left(), dividerY),
            QPointF(localBounds.right(), dividerY), handDrawnSeed,
            qMax<qreal>(1.3, node.strokeWidth)));
      }
      for (rough::Drawable& drawable : node.roughDrawables)
        drawable = rough::translatedDrawable(std::move(drawable),
                                              node.bounds.center());
      node.paintedBounds = stateRoughBounds(node.roughDrawables);
    }
    scene.nodes.append(node);
    include(scene.bounds, initialized,
            handDrawn && node.paintedBounds.isValid()
                ? node.paintedBounds : node.bounds);
  }
  for (const StatePlacementEdge& placed : placement.edges) {
    const auto it = std::find_if(input.edges.cbegin(), input.edges.cend(),
        [&](const StateLayoutEdgeInput& edge) { return edge.id == placed.id; });
    if (it == input.edges.cend()) continue;
    StateSceneEdge edge;
    edge.id = it->id; edge.start = it->start; edge.end = it->end;
    edge.label = it->label.toString(); edge.markerEnd = it->arrowTypeEnd;
    edge.classes = it->classes; edge.points = placed.points;
    edge.segments = placed.segments; edge.labelPosition = placed.labelPosition;
    edge.path = placed.path;
    // Resolve linkStyle / edge classDef via the shared cascade. State thickness
    // tokens (normal/dotted/thick/invisible) match MermaidStyleResolve's pattern.
    QStringList edgeLinkStyles = style::compiledClassStyles(
        it->classes.split(QLatin1Char(' '), Qt::SkipEmptyParts), classDefs);
    edgeLinkStyles += it->linkStyles;
    const style::ResolvedEdgeStyle resolvedEdge = style::resolveEdgeStyle(
        it->thickness, edgeLinkStyles, false, QString(), themeDefaults);
    edge.stroke = resolvedEdge.stroke;
    edge.strokeWidth = resolvedEdge.strokeWidth;
    edge.strokeDasharray = resolvedEdge.strokeDasharray;
    edge.labelDocument = prepareLabel(edge.label, scene.style.fontSize);
    edge.pathBounds = edgePaintBounds(edge.points, edge.segments);
    if (handDrawn) {
      edge.roughDrawable = rough::roughEdgeDrawable(
          scene::parseSvgPath(edge.path), handDrawnSeed);
      edge.pathBounds = rough::tightBounds(edge.roughDrawable);
    }
    if (!edge.label.isEmpty() && edge.labelPosition) {
      edge.labelSize = flowchart::measureFlowLabel(
          edge.labelDocument, scene.style.fontFamily,
          scene.style.fontSize, scene.style.lineHeight);
      edge.labelBounds = QRectF(
          *edge.labelPosition -
              QPointF(edge.labelSize.width() / 2.0,
                      edge.labelSize.height() / 2.0),
          edge.labelSize);
    }
    scene.edges.append(std::move(edge));
    if (handDrawn && edge.pathBounds.isValid())
      include(scene.bounds, initialized, edge.pathBounds);
    else
      for (const QPointF& point : placed.points)
        include(scene.bounds, initialized, QRectF(
            point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0)));
  }
  if (initialized) scene.bounds.adjust(-8.0, -8.0, 8.0, 8.0);
  return scene;
}

namespace {

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r3(r.x())},
          {QStringLiteral("y"), r3(r.y())},
          {QStringLiteral("width"), r3(r.width())},
          {QStringLiteral("height"), r3(r.height())}};
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"), r3(p.x())}, {QStringLiteral("y"), r3(p.y())}};
}

QJsonArray pointsJson(const QVector<QPointF>& pts) {
  QJsonArray a;
  for (const QPointF& p : pts) {
    QJsonArray pair;
    pair.append(r3(p.x()));
    pair.append(r3(p.y()));
    a.append(pair);
  }
  return a;
}

}  // namespace

QJsonObject StateScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("role")] = role;
  o[QStringLiteral("ariaRoleDescription")] = ariaRoleDescription;
  o[QStringLiteral("bounds")] = rectJson(bounds);
  if (handDrawn) {
    o[QStringLiteral("handDrawn")] = true;
    o[QStringLiteral("handDrawnSeed")] = static_cast<int>(handDrawnSeed);
  }

  QJsonArray nodesArray;
  for (const StateSceneNode& node : nodes) {
    QJsonObject n;
    n[QStringLiteral("id")] = node.id;
    if (!node.shape.isEmpty())
      n[QStringLiteral("shape")] = node.shape;
    n[QStringLiteral("label")] = node.label;
    n[QStringLiteral("bounds")] = rectJson(node.bounds);
    n[QStringLiteral("group")] = node.group;
    nodesArray.append(n);
  }
  o[QStringLiteral("nodes")] = nodesArray;

  QJsonArray clustersArray;
  for (const StateSceneNode& cluster : clusters) {
    QJsonObject c;
    c[QStringLiteral("id")] = cluster.id;
    if (!cluster.shape.isEmpty())
      c[QStringLiteral("shape")] = cluster.shape;
    c[QStringLiteral("label")] = cluster.label;
    c[QStringLiteral("bounds")] = rectJson(cluster.bounds);
    c[QStringLiteral("group")] = cluster.group;
    clustersArray.append(c);
  }
  o[QStringLiteral("clusters")] = clustersArray;

  QJsonArray edgesArray;
  for (const StateSceneEdge& edge : edges) {
    QJsonObject e;
    e[QStringLiteral("id")] = edge.id;
    e[QStringLiteral("start")] = edge.start;
    e[QStringLiteral("end")] = edge.end;
    if (!edge.label.isEmpty())
      e[QStringLiteral("label")] = edge.label;
    if (!edge.markerEnd.isEmpty())
      e[QStringLiteral("markerEnd")] = edge.markerEnd;
    e[QStringLiteral("path")] = edge.path;
    e[QStringLiteral("points")] = pointsJson(edge.points);
    if (edge.labelPosition.has_value())
      e[QStringLiteral("labelPosition")] = pointJson(*edge.labelPosition);
    edgesArray.append(e);
  }
  o[QStringLiteral("edges")] = edgesArray;

  return o;
}

SvgMarkerProjection StateScene::svgMarkerProjection() const {
  SvgMarkerProjection projection;
  SvgMarkerDefinition definition;
  definition.key = arrowMarkerId;
  definition.idSuffix = QStringLiteral("_stateDiagram-") + arrowMarkerId;
  definition.refX = arrowMarkerRef.x(); definition.refY = arrowMarkerRef.y();
  definition.markerWidth = arrowMarkerSize.width();
  definition.markerHeight = arrowMarkerSize.height();
  definition.markerUnits = QStringLiteral("userSpaceOnUse");
  definition.orient = arrowMarkerOrient;
  SvgMarkerChild child;
  child.tag = QStringLiteral("path");
  child.path = QStringLiteral("M 19,7 L9,13 L14,7 L9,1 Z");
  child.fill = style.transitionColor;
  child.stroke = style.transitionColor;
  definition.children.append(child);
  projection.definitions.append(definition);
  for (const StateSceneEdge& source : edges) {
    if (source.markerEnd.isEmpty() || source.markerEnd == QLatin1String("none"))
      continue;
    SvgMarkerEdge edge;
    edge.id = source.id;
    edge.cssClass = QStringLiteral("transition");
    edge.path = source.path;
    edge.markerEnd = arrowMarkerId;
    edge.stroke = source.stroke.isEmpty() ? style.transitionColor : source.stroke;
    edge.strokeWidth = source.strokeWidth;
    edge.strokeDasharray = source.strokeDasharray;
    projection.edges.append(edge);
  }
  return projection;
}

}  // namespace muffin::mermaid::state
