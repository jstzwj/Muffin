#include "mermaid/scene/FlowScene.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"

#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/scene/FlowMarkers.h"
#include "mermaid/theme/FlowStyleResolve.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace muffin::mermaid::flowscene {
namespace {

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QJsonObject labelJson(const FlowSceneLabel& l) {
  QJsonObject o;
  o[QStringLiteral("text")] = l.text;
  o[QStringLiteral("labelType")] = l.labelType;
  o[QStringLiteral("x")] = r3(l.x);
  o[QStringLiteral("y")] = r3(l.y);
  if (!l.color.isEmpty()) o[QStringLiteral("color")] = l.color;
  if (!l.fontFamily.isEmpty()) o[QStringLiteral("fontFamily")] = l.fontFamily;
  if (!l.fontSize.isEmpty()) o[QStringLiteral("fontSize")] = l.fontSize;
  if (!l.fontWeight.isEmpty()) o[QStringLiteral("fontWeight")] = l.fontWeight;
  if (!l.background.isEmpty()) o[QStringLiteral("background")] = l.background;
  return o;
}

}  // namespace

FlowScene buildFlowScene(const flowchart::FlowchartData& data,
                         const flowchart::FlowLayoutResult& layout,
                         const flowtheme::FlowThemeVariables& theme,
                         flowchart::FlowLook look) {
  FlowScene scene;
  scene.background = theme.background;
  scene.look = look;
  scene.useGradient = look == flowchart::FlowLook::Neo && theme.useGradient;
  scene.gradientStart = theme.gradientStart;
  scene.gradientStop = theme.gradientStop;
  scene.shadowColor = theme.shadowColor;
  scene.shadowOpacity = theme.shadowOpacity;
  scene.shadowOffsetX = theme.shadowOffsetX;
  scene.shadowOffsetY = theme.shadowOffsetY;

  // Index vertices/subgraphs by id for O(1) lookup.
  QHash<QString, const flowchart::FlowVertex*> vertexById;
  for (const flowchart::FlowVertex& v : data.vertices) vertexById.insert(v.id, &v);
  QHash<QString, const flowchart::FlowSubgraph*> subgraphById;
  for (const flowchart::FlowSubgraph& s : data.subgraphs) subgraphById.insert(s.id, &s);
  QHash<QString, const flowchart::FlowEdge*> edgeById;
  for (const flowchart::FlowEdge& e : data.edges) edgeById.insert(e.id, &e);

  const QString clusterStrokeWidth = QStringLiteral("1px");

  // Clusters (drawn first).
  for (const flowchart::FlowLayoutCluster& c : layout.clusters) {
    FlowSceneCluster sc;
    sc.id = c.id;
    sc.cx = c.x; sc.cy = c.y; sc.width = c.width; sc.height = c.height;
    sc.fill = theme.clusterBkg;
    sc.stroke = theme.clusterBorder;
    sc.strokeWidth = clusterStrokeWidth;
    if (const flowchart::FlowSubgraph* s = subgraphById.value(c.id)) {
      sc.label.text = s->title;
      sc.label.labelType = s->labelType;
      sc.label.color = theme.titleColor;
      sc.label.fontFamily = theme.fontFamily;
      sc.label.fontSize = theme.fontSize;
    }
    scene.clusters.append(sc);
  }

  // Edges (drawn second). Markers ride on the path.
  for (const flowchart::FlowLayoutEdge& e : layout.edges) {
    FlowSceneEdge se;
    se.id = e.id;
    se.path = e.path;
    const flowchart::FlowEdge* fe = edgeById.value(e.id);
    if (fe) {
      flowchart::FlowEdge effectiveEdge = *fe;
      effectiveEdge.style = data.defaultEdgeStyles + effectiveEdge.style;
      const flowstyle::ResolvedEdgeStyle rs = flowstyle::resolveEdgeStyle(effectiveEdge, theme);
      se.stroke = rs.stroke;
      se.strokeWidth = rs.strokeWidth;
      se.strokeDasharray = rs.strokeDasharray;
      se.markerEnd = markerEndType(fe->type);
      se.markerStart = markerStartType(fe->type);
      se.animated = fe->animate || !fe->animation.isEmpty();
      // Path endpoints + tangents (for marker orientation in the painter).
      if (e.points.size() >= 2) {
        se.startPoint = e.points.first();
        se.endPoint = e.points.last();
        se.startTangent = e.points.at(1) - e.points.at(0);
        se.endTangent = e.points.last() - e.points.at(e.points.size() - 2);
      } else if (e.points.size() == 1) {
        se.startPoint = se.endPoint = e.points.first();
      }
      if (!fe->text.isEmpty() && e.hasLabelPosition) {
        se.label.text = fe->text;
        se.label.labelType = fe->labelType;
        se.label.x = e.labelX; se.label.y = e.labelY;
        se.label.background = theme.edgeLabelBackground;
        se.label.color = theme.textColor;
        se.label.fontFamily = theme.fontFamily;
        se.label.fontSize = theme.fontSize;
      }
    } else {
      se.stroke = theme.lineColor;
      se.strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
    }
    scene.edges.append(se);
  }

  // Nodes (drawn last).
  qreal minX = 0, minY = 0, maxX = 0, maxY = 0;
  bool first = true;
  for (const flowchart::FlowLayoutNode& n : layout.nodes) {
    FlowSceneNode sn;
    sn.id = n.id;
    sn.cx = n.x; sn.cy = n.y; sn.width = n.width; sn.height = n.height;
    if (const flowchart::FlowVertex* v = vertexById.value(n.id)) {
      sn.shapeType = flowchart::canonicalShape(v->type);
      const flowstyle::ResolvedNodeStyle rs = flowstyle::resolveNodeStyle(*v, data.classes, theme);
      sn.fill = rs.fill; sn.stroke = rs.stroke; sn.strokeWidth = rs.strokeWidth;
      const flowchart::FlowShapeGeometry geom =
          flowchart::flowShapeGeometry(*v, QSizeF(n.width, n.height), look);
      sn.shapeKind = geom.kind;
      sn.cornerRadius = geom.cornerRadius;
      sn.radiusX = geom.radiusX;
      sn.radiusY = geom.radiusY;
      sn.points = geom.points;
      sn.label.text = v->text;
      if (sn.shapeType == QLatin1String("framed_circle") ||
          sn.shapeType == QLatin1String("small_circle") ||
          sn.shapeType == QLatin1String("filled_circle") ||
          sn.shapeType == QLatin1String("hourglass") ||
          sn.shapeType == QLatin1String("fork") ||
          sn.shapeType == QLatin1String("lightning_bolt"))
        sn.label.text.clear();
      sn.label.labelType = v->labelType;
      sn.label.mathEnabled = true;
      sn.label.x = n.x; sn.label.y = n.y;  // mermaid centers the label at the node centre
      if (sn.shapeType == QLatin1String("multi_document") ||
          sn.shapeType == QLatin1String("stacked_rect")) {
        sn.label.x -= 10.0;
        sn.label.y += 10.0;
      }
      sn.label.color = rs.color;
      sn.label.fontFamily = rs.fontFamily;
      sn.label.fontSize = rs.fontSize;
      sn.label.fontWeight = rs.fontWeight;
      sn.link = v->link;
      sn.tooltip = data.tooltips.value(v->id);
    }
    scene.nodes.append(sn);
    const qreal left = n.x - n.width / 2.0, right = n.x + n.width / 2.0;
    const qreal top = n.y - n.height / 2.0, bottom = n.y + n.height / 2.0;
    if (first) { minX = left; maxX = right; minY = top; maxY = bottom; first = false; }
    else { minX = std::min(minX, left); maxX = std::max(maxX, right); minY = std::min(minY, top); maxY = std::max(maxY, bottom); }
  }
  for (const FlowSceneCluster& c : scene.clusters) {
    const qreal left = c.cx - c.width / 2.0, right = c.cx + c.width / 2.0;
    const qreal top = c.cy - c.height / 2.0, bottom = c.cy + c.height / 2.0;
    if (first) { minX = left; maxX = right; minY = top; maxY = bottom; first = false; }
    else { minX = std::min(minX, left); maxX = std::max(maxX, right); minY = std::min(minY, top); maxY = std::max(maxY, bottom); }
  }
  scene.bounds = QRectF(minX, minY, maxX - minX, maxY - minY);
  return scene;
}

QString FlowScene::toJson() const {
  QJsonObject root;
  root[QStringLiteral("background")] = background;
  root[QStringLiteral("look")] = flowchart::flowLookName(look);
  root[QStringLiteral("useGradient")] = useGradient;
  QJsonObject b;
  b[QStringLiteral("x")] = r3(bounds.x()); b[QStringLiteral("y")] = r3(bounds.y());
  b[QStringLiteral("width")] = r3(bounds.width()); b[QStringLiteral("height")] = r3(bounds.height());
  root[QStringLiteral("bounds")] = b;

  QJsonArray clustersJson;
  for (const FlowSceneCluster& c : clusters) {
    QJsonObject o;
    o[QStringLiteral("id")] = c.id;
    o[QStringLiteral("cx")] = r3(c.cx); o[QStringLiteral("cy")] = r3(c.cy);
    o[QStringLiteral("width")] = r3(c.width); o[QStringLiteral("height")] = r3(c.height);
    o[QStringLiteral("fill")] = c.fill; o[QStringLiteral("stroke")] = c.stroke;
    o[QStringLiteral("strokeWidth")] = c.strokeWidth;
    o[QStringLiteral("label")] = labelJson(c.label);
    clustersJson.append(o);
  }
  root[QStringLiteral("clusters")] = clustersJson;

  QJsonArray edgesJson;
  for (const FlowSceneEdge& e : edges) {
    QJsonObject o;
    o[QStringLiteral("id")] = e.id;
    o[QStringLiteral("path")] = e.path;
    o[QStringLiteral("stroke")] = e.stroke; o[QStringLiteral("strokeWidth")] = e.strokeWidth;
    if (!e.strokeDasharray.isEmpty()) o[QStringLiteral("strokeDasharray")] = e.strokeDasharray;
    o[QStringLiteral("markerEnd")] = e.markerEnd;
    o[QStringLiteral("markerStart")] = e.markerStart;
    o[QStringLiteral("label")] = labelJson(e.label);
    edgesJson.append(o);
  }
  root[QStringLiteral("edges")] = edgesJson;

  QJsonArray nodesJson;
  for (const FlowSceneNode& n : nodes) {
    QJsonObject o;
    o[QStringLiteral("id")] = n.id;
    o[QStringLiteral("shapeKind")] = n.shapeKind;
    o[QStringLiteral("cx")] = r3(n.cx); o[QStringLiteral("cy")] = r3(n.cy);
    o[QStringLiteral("width")] = r3(n.width); o[QStringLiteral("height")] = r3(n.height);
    o[QStringLiteral("fill")] = n.fill; o[QStringLiteral("stroke")] = n.stroke;
    o[QStringLiteral("strokeWidth")] = n.strokeWidth;
    o[QStringLiteral("label")] = labelJson(n.label);
    nodesJson.append(o);
  }
  root[QStringLiteral("nodes")] = nodesJson;

  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}  // namespace muffin::mermaid::flowscene
