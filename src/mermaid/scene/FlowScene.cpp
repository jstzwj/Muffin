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

QRectF edgePaintBounds(const flowchart::FlowLayoutEdge& edge) {
  QRectF bounds;
  bool initialized = false;
  auto includePoint = [&](const QPointF& point) {
    const QRectF pixel(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0));
    if (!initialized) {
      bounds = pixel;
      initialized = true;
    } else {
      bounds = bounds.united(pixel);
    }
  };
  for (const QPointF& point : edge.points) includePoint(point);
  for (const QVector<QPointF>& segment : edge.segments)
    for (const QPointF& point : segment) includePoint(point);
  // Point/circle/cross markers extend roughly 10 scene pixels past endpoints.
  return initialized ? bounds.adjusted(-12.0, -12.0, 12.0, 12.0) : QRectF{};
}

QPainterPath polygonPath(const QVector<QPointF>& points) {
  QPainterPath path;
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  path.closeSubpath();
  return path;
}

QPainterPath openPath(const QVector<QPointF>& points, const QPointF& offset = {}) {
  QPainterPath path;
  if (points.isEmpty()) return path;
  path.moveTo(points.first() + offset);
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i) + offset);
  return path;
}

QVector<QPointF> circlePoints(qreal centerX, qreal centerY, qreal radius,
                              int count, qreal startDegrees, qreal endDegrees,
                              bool negate) {
  QVector<QPointF> points;
  points.reserve(count);
  const qreal start = qDegreesToRadians(startDegrees);
  const qreal range = qDegreesToRadians(endDegrees - startDegrees);
  for (int i = 0; i < count; ++i) {
    const qreal angle = start + range * i / (count - 1);
    QPointF point(centerX + radius * std::cos(angle),
                  centerY + radius * std::sin(angle));
    points.append(negate ? -point : point);
  }
  return points;
}

void appendPoints(QVector<QPointF>& target, const QVector<QPointF>& source) {
  target.reserve(target.size() + source.size());
  for (const QPointF& point : source) target.append(point);
}

QPointF bboxCenter(const QVector<QPointF>& points) {
  if (points.isEmpty()) return {};
  qreal left = points.first().x(), right = left;
  qreal top = points.first().y(), bottom = top;
  for (const QPointF& point : points) {
    left = std::min(left, point.x()); right = std::max(right, point.x());
    top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
  }
  return QPointF((left + right) / 2.0, (top + bottom) / 2.0);
}

void appendSine(QPainterPath& path, qreal x1, qreal x2, qreal y,
                qreal amplitude, qreal cycles) {
  constexpr int steps = 50;
  for (int i = 1; i <= steps; ++i) {
    const qreal t = qreal(i) / steps;
    const qreal x = x1 + (x2 - x1) * t;
    path.lineTo(x, y + amplitude * std::sin(2.0 * M_PI * cycles * t));
  }
}

FlowSceneShapePath shapePath(QPainterPath path, bool fill = true, bool stroke = true,
                             QString fillOverride = {}, QString strokeOverride = {}) {
  return {std::move(path), fill, stroke, std::move(fillOverride), std::move(strokeOverride)};
}

QVector<FlowSceneShapePath> buildShapePaths(const FlowSceneNode& node) {
  QVector<FlowSceneShapePath> paths;
  const QRectF r(-node.width / 2.0, -node.height / 2.0, node.width, node.height);
  const QString& type = node.shapeType;
  if (type == QLatin1String("text")) return paths;

  const auto documentPath = [](const QRectF& layer, qreal heightRatio,
                               qreal amplitudeDivisor) {
    const qreal internalH = layer.height() / heightRatio;
    const qreal amplitude = internalH / amplitudeDivisor;
    const qreal waveY = layer.bottom() - amplitude;
    QPainterPath path(QPointF(layer.left(), waveY));
    appendSine(path, layer.left(), layer.right(), waveY, amplitude, 0.8);
    path.lineTo(layer.right(), layer.top());
    path.lineTo(layer.left(), layer.top());
    path.closeSubpath();
    return path;
  };

  if (type == QLatin1String("multi_document")) {
    constexpr qreal o = 10.0;
    for (const QRectF& layer : {r.adjusted(2 * o, 0, 0, -2 * o),
                                r.adjusted(o, o, -o, -o),
                                r.adjusted(0, 2 * o, -2 * o, 0)})
      paths.append(shapePath(documentPath(layer, 1.25, 4.0)));
    return paths;
  }
  if (type == QLatin1String("document") || type == QLatin1String("lined_document") ||
      type == QLatin1String("tagged_document")) {
    const bool tagged = type == QLatin1String("tagged_document");
    paths.append(shapePath(documentPath(r, tagged ? 1.25 : 1.5, tagged ? 8.0 : 4.0)));
    const qreal internalH = r.height() / (tagged ? 1.25 : 1.5);
    const qreal amplitude = internalH / (tagged ? 8.0 : 4.0);
    const qreal waveY = r.bottom() - amplitude;
    if (type == QLatin1String("lined_document")) {
      QPainterPath line(QPointF(r.left() + r.width() / 11.0, r.top()));
      line.lineTo(r.left() + r.width() / 11.0, waveY);
      paths.append(shapePath(line, false, true));
    } else if (tagged) {
      const qreal tag = std::min(r.width(), internalH) * 0.22;
      QPainterPath fold(QPointF(r.right() - tag, r.bottom() - amplitude * 0.2));
      fold.lineTo(r.right() - tag, r.bottom() - tag - amplitude * 0.2);
      fold.lineTo(r.right(), r.bottom() - amplitude * 0.2);
      paths.append(shapePath(fold, false, true));
    }
    return paths;
  }
  if (type == QLatin1String("flag")) {
    const qreal internalH = r.height() / 1.5;
    const qreal amplitude = internalH / 8.0;
    const qreal top = r.top() + amplitude, bottom = r.bottom() - amplitude;
    QPainterPath path(QPointF(r.left(), bottom));
    appendSine(path, r.left(), r.right(), bottom, amplitude, 1.0);
    path.lineTo(r.right(), top);
    appendSine(path, r.right(), r.left(), top, -amplitude, -1.0);
    path.closeSubpath();
    paths.append(shapePath(path));
    return paths;
  }
  if (type == QLatin1String("stacked_rect")) {
    const qreal o = node.width > 80.0 && node.height > 60.0 ? 10.0 : 5.0;
    for (const QRectF& layer : {r.adjusted(2 * o, 0, 0, -2 * o),
                                r.adjusted(o, o, -o, -o),
                                r.adjusted(0, 2 * o, -2 * o, 0)}) {
      QPainterPath path; path.addRect(layer); paths.append(shapePath(path));
    }
    return paths;
  }
  if (type == QLatin1String("tagged_rect")) {
    const qreal tag = r.height() * 0.2;
    QPainterPath outer(QPointF(r.left(), r.top()));
    outer.lineTo(r.right() - tag, r.top()); outer.lineTo(r.right(), r.top() + tag);
    outer.lineTo(r.right(), r.bottom()); outer.lineTo(r.left(), r.bottom()); outer.closeSubpath();
    paths.append(shapePath(outer));
    QPainterPath fold(QPointF(r.right() - tag, r.top()));
    fold.lineTo(r.right() - tag, r.top() + tag); fold.lineTo(r.right(), r.top() + tag);
    paths.append(shapePath(fold, false, true));
    return paths;
  }
  if (type == QLatin1String("brace_left") || type == QLatin1String("brace_right") ||
      type == QLatin1String("braces")) {
    qreal h, radius;
    if (node.height >= 60.0) {
      h = node.height / 1.2;
      radius = h / 10.0;
    } else {
      radius = 5.0;
      h = node.height - 2.0 * radius;
    }
    const qreal widthFactor = type == QLatin1String("braces") ? 2.5 : 2.0;
    const qreal w = node.width - widthFactor * radius;
    QVector<QPointF> bounds;

    if (type == QLatin1String("brace_left")) {
      QVector<QPointF> visible;
      appendPoints(visible, circlePoints(w / 2, -h / 2, radius, 30, -90, 0, true));
      visible.append(QPointF(-w / 2 - radius, radius));
      appendPoints(visible, circlePoints(w / 2 + 2 * radius, -radius, radius, 20, -180, -270, true));
      appendPoints(visible, circlePoints(w / 2 + 2 * radius, radius, radius, 20, -90, -180, true));
      visible.append(QPointF(-w / 2 - radius, -h / 2));
      appendPoints(visible, circlePoints(w / 2, h / 2, radius, 20, 0, 90, true));

      bounds = {QPointF(w / 2, -h / 2 - radius), QPointF(-w / 2, -h / 2 - radius)};
      appendPoints(bounds, circlePoints(w / 2, -h / 2, radius, 20, -90, 0, true));
      bounds.append(QPointF(-w / 2 - radius, -radius));
      appendPoints(bounds, circlePoints(w / 2 + w * 0.1, -radius, radius, 20, -180, -270, true));
      appendPoints(bounds, circlePoints(w / 2 + w * 0.1, radius, radius, 20, -90, -180, true));
      bounds.append(QPointF(-w / 2 - radius, h / 2));
      appendPoints(bounds, circlePoints(w / 2, h / 2, radius, 20, 0, 90, true));
      bounds.append(QPointF(-w / 2, h / 2 + radius));
      bounds.append(QPointF(w / 2, h / 2 + radius));
      const QPointF offset = -bboxCenter(bounds);
      paths.append(shapePath(openPath(visible, offset), false, true));
    } else if (type == QLatin1String("brace_right")) {
      QVector<QPointF> visible;
      appendPoints(visible, circlePoints(w / 2, -h / 2, radius, 20, -90, 0, false));
      visible.append(QPointF(w / 2 + radius, -radius));
      appendPoints(visible, circlePoints(w / 2 + 2 * radius, -radius, radius, 20, -180, -270, false));
      appendPoints(visible, circlePoints(w / 2 + 2 * radius, radius, radius, 20, -90, -180, false));
      visible.append(QPointF(w / 2 + radius, h / 2));
      appendPoints(visible, circlePoints(w / 2, h / 2, radius, 20, 0, 90, false));

      bounds = {QPointF(-w / 2, -h / 2 - radius), QPointF(w / 2, -h / 2 - radius)};
      appendPoints(bounds, visible);
      bounds.append(QPointF(w / 2, h / 2 + radius));
      bounds.append(QPointF(-w / 2, h / 2 + radius));
      const QPointF offset = -bboxCenter(bounds);
      paths.append(shapePath(openPath(visible, offset), false, true));
    } else {
      QVector<QPointF> left, right;
      appendPoints(left, circlePoints(w / 2, -h / 2, radius, 30, -90, 0, true));
      left.append(QPointF(-w / 2 - radius, radius));
      appendPoints(left, circlePoints(w / 2 + 2 * radius, -radius, radius, 20, -180, -270, true));
      appendPoints(left, circlePoints(w / 2 + 2 * radius, radius, radius, 20, -90, -180, true));
      left.append(QPointF(-w / 2 - radius, -h / 2));
      appendPoints(left, circlePoints(w / 2, h / 2, radius, 20, 0, 90, true));
      appendPoints(right, circlePoints(-w / 2 + 1.5 * radius, -h / 2, radius, 20, -90, -180, true));
      right.append(QPointF(w / 2 - radius / 2, radius));
      appendPoints(right, circlePoints(-w / 2 - radius / 2, -radius, radius, 20, 0, 90, true));
      appendPoints(right, circlePoints(-w / 2 - radius / 2, radius, radius, 20, -90, 0, true));
      right.append(QPointF(w / 2 - radius / 2, -radius));
      appendPoints(right, circlePoints(-w / 2 + 1.5 * radius, h / 2, radius, 30, -180, -270, true));
      bounds = left;
      appendPoints(bounds, right);
      bounds.append(QPointF(w / 2, -h / 2 - radius));
      bounds.append(QPointF(-w / 2, -h / 2 - radius));
      bounds.append(QPointF(-w / 2, h / 2 + radius));
      bounds.append(QPointF(w / 2 - 1.5 * radius, h / 2 + radius));
      const QPointF offset = -bboxCenter(bounds);
      paths.append(shapePath(openPath(left, offset), false, true));
      paths.append(shapePath(openPath(right, offset), false, true));
    }
    return paths;
  }

  if (node.shapeKind == QLatin1String("roundedRect") ||
      node.shapeKind == QLatin1String("stadium")) {
    QPainterPath path; path.addRoundedRect(r, node.cornerRadius, node.cornerRadius);
    paths.append(shapePath(path));
  } else if (node.shapeKind == QLatin1String("ellipse")) {
    QPainterPath outer; outer.addEllipse(r);
    paths.append(type == QLatin1String("filled_circle")
                     ? shapePath(outer, true, true, node.stroke)
                     : shapePath(outer));
    if (type == QLatin1String("double_circle") || type == QLatin1String("framed_circle")) {
      const qreal gap = type == QLatin1String("double_circle") ? 12.0 : 2.0;
      QPainterPath inner; inner.addEllipse(r.adjusted(gap, gap, -gap, -gap));
      paths.append(shapePath(inner));
    }
    if (type == QLatin1String("crossed_circle")) {
      QPainterPath cross(r.topLeft()); cross.lineTo(r.bottomRight());
      cross.moveTo(r.topRight()); cross.lineTo(r.bottomLeft());
      paths.append(shapePath(cross, false, true));
    }
  } else if (node.shapeKind == QLatin1String("polygon")) {
    paths.append(shapePath(polygonPath(node.points)));
  } else if (node.shapeKind == QLatin1String("cylinder")) {
    const QRectF top(r.left(), r.top(), r.width(), node.radiusY * 2.0);
    const QRectF bottom(r.left(), r.bottom() - node.radiusY * 2.0,
                        r.width(), node.radiusY * 2.0);
    const qreal topCenter = r.top() + node.radiusY;
    const qreal bottomCenter = r.bottom() - node.radiusY;
    QPainterPath body(QPointF(r.left(), topCenter));
    body.arcTo(top, 180, -180);
    body.lineTo(r.right(), bottomCenter);
    body.arcTo(bottom, 0, -180);
    body.closeSubpath();
    paths.append(shapePath(body));
    QPainterPath rim;
    rim.addEllipse(top);
    paths.append(shapePath(rim, false, true));
    if (type == QLatin1String("lined_cylinder")) {
      const qreal outerOffset = (node.height - 2.0 * node.radiusY) * 0.1;
      const QRectF offsetArc(r.left(), topCenter + outerOffset - node.radiusY,
                             r.width(), 2.0 * node.radiusY);
      QPainterPath line(QPointF(r.left(), topCenter + outerOffset));
      line.arcTo(offsetArc, 180, -180);
      paths.append(shapePath(line, false, true));
    }
  } else if (node.shapeKind == QLatin1String("horizontalCylinder")) {
    QPainterPath body(QPointF(r.left() + node.radiusX, r.top()));
    body.lineTo(r.right() - node.radiusX, r.top());
    body.cubicTo(r.right() + node.radiusX, r.top(), r.right() + node.radiusX, r.bottom(),
                 r.right() - node.radiusX, r.bottom());
    body.lineTo(r.left() + node.radiusX, r.bottom());
    body.cubicTo(r.left() - node.radiusX, r.bottom(), r.left() - node.radiusX, r.top(),
                 r.left() + node.radiusX, r.top()); body.closeSubpath(); paths.append(shapePath(body));
    QPainterPath cap; cap.addEllipse(QRectF(r.left(), r.top(), 2 * node.radiusX, r.height()));
    paths.append(shapePath(cap, false, true));
  } else {
    QPainterPath rect; rect.addRect(r);
    paths.append(shapePath(rect));
    if (type == QLatin1String("subroutine") || type == QLatin1String("lined_process")) {
      QPainterPath frames(QPointF(r.left() + 8.0, r.top()));
      frames.lineTo(r.left() + 8.0, r.bottom());
      if (type == QLatin1String("subroutine")) {
        frames.moveTo(r.right() - 8.0, r.top()); frames.lineTo(r.right() - 8.0, r.bottom());
      }
      paths.append(shapePath(frames, false, true));
    } else if (type == QLatin1String("window_pane")) {
      QPainterPath div(QPointF(r.left() + 10.0, r.top()));
      div.lineTo(r.left() + 10.0, r.bottom());
      div.moveTo(r.left(), r.top() + 10.0); div.lineTo(r.right(), r.top() + 10.0);
      paths.append(shapePath(div, false, true));
    } else if (type == QLatin1String("divided_rect")) {
      QPainterPath div(QPointF(r.left(), r.top() + r.height() / 6.0));
      div.lineTo(r.right(), r.top() + r.height() / 6.0);
      paths.append(shapePath(div, false, true));
    }
  }
  return paths;
}

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

qreal labelFontPixelSize(const FlowSceneLabel& label) {
  bool ok = false;
  const qreal size = label.fontSize.endsWith(QLatin1String("px"))
      ? label.fontSize.chopped(2).toDouble(&ok)
      : 16.0;
  return ok ? size : 16.0;
}

void prepareSceneLabel(FlowSceneLabel& label) {
  if (label.richText.text.isNull())
    label.richText = label.mathEnabled
        ? flowchart::parseFlowLabel(label.text, label.labelType, true)
        : flowchart::parseFlowSvgLabel(label.text, label.labelType);
  flowchart::prepareFlowLabelMath(label.richText,
                                  labelFontPixelSize(label));
}

}  // namespace

FlowScene buildFlowScene(const flowchart::FlowchartData& data,
                         const flowchart::FlowLayoutResult& layout,
                         const flowtheme::FlowThemeVariables& theme,
                         flowchart::FlowLook look,
                         quint32 handDrawnSeed) {
  FlowScene scene;
  scene.background = theme.background;
  scene.look = look;
  scene.handDrawnSeed = handDrawnSeed;
  scene.useGradient = look == flowchart::FlowLook::Neo && theme.useGradient;
  scene.gradientStart = theme.gradientStart;
  scene.gradientStop = theme.gradientStop;
  scene.lineColor = theme.lineColor;
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
    // Neo's cluster CSS shares the node border token; clusterBorder is used by
    // the classic flowchart stylesheet only.
    sc.stroke = look == flowchart::FlowLook::Neo ? theme.nodeBorder
                                                  : theme.clusterBorder;
    sc.strokeWidth = clusterStrokeWidth;
    if (const flowchart::FlowSubgraph* s = subgraphById.value(c.id)) {
      sc.label.text = s->title;
      sc.label.labelType = s->labelType;
      sc.label.color = theme.titleColor;
      sc.label.fontFamily = theme.fontFamily;
      sc.label.fontSize = theme.fontSize;
      // classDef applied to a subgraph (`class <subgraphId> <name>`) overrides
      // the theme cluster fill/stroke — upstream setClass hits subgraphs too.
      for (const QString& decl : flowstyle::compiledClassStyles(s->classes, data.classes)) {
        const int colon = decl.indexOf(QLatin1Char(':'));
        if (colon < 0) continue;
        const QString key = decl.left(colon).trimmed();
        const QString value = decl.mid(colon + 1).trimmed();
        if (key == QLatin1String("fill")) sc.fill = value;
        else if (key == QLatin1String("stroke")) sc.stroke = value;
      }
    }
    prepareSceneLabel(sc.label);
    scene.clusters.append(sc);
  }

  // Edges (drawn second). Markers ride on the path.
  for (const flowchart::FlowLayoutEdge& e : layout.edges) {
    FlowSceneEdge se;
    se.id = e.id;
    se.path = e.path;
    se.pathBounds = edgePaintBounds(e);
    const flowchart::FlowEdge* fe = edgeById.value(e.id);
    if (fe) {
      flowchart::FlowEdge effectiveEdge = *fe;
      // Cascade order (low -> high): classDef (`class <edgeId> <name>`) < the
      // linkStyle default < this edge's own linkStyle entries. resolveEdgeStyle
      // applies declarations in order, last-wins.
      effectiveEdge.style = flowstyle::compiledClassStyles(fe->classes, data.classes)
          + data.defaultEdgeStyles + effectiveEdge.style;
      const flowstyle::ResolvedEdgeStyle rs = flowstyle::resolveEdgeStyle(effectiveEdge, theme);
      se.stroke = rs.stroke;
      se.strokeWidth = rs.strokeWidth;
      se.strokeDasharray = rs.strokeDasharray;
      se.markerEnd = markerEndType(fe->type);
      se.markerStart = markerStartType(fe->type);
      se.animated = fe->animate || !fe->animation.isEmpty();
      if (se.animated) {
        se.animation = fe->animation == QLatin1String("slow")
            ? QStringLiteral("slow") : QStringLiteral("fast");
      }
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
        se.label.richText = e.labelDocument;
        se.labelSize = e.labelSize;
        se.labelBounds = QRectF(
            QPointF(se.label.x - se.labelSize.width() / 2.0,
                    se.label.y - se.labelSize.height() / 2.0),
            se.labelSize);
      }
    } else {
      se.stroke = theme.lineColor;
      se.strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
    }
    prepareSceneLabel(se.label);
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
      sn.shapePaths = buildShapePaths(sn);
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
      } else if (sn.shapeType == QLatin1String("brace_left") &&
                 look == flowchart::FlowLook::Neo) {
        const qreal h = sn.height >= 60.0 ? sn.height / 1.2 : sn.height - 10.0;
        const qreal radius = std::max(5.0, h * 0.1);
        sn.label.x += radius - 9.0;
        sn.label.y += 1.5;
      } else if (sn.shapeType == QLatin1String("brace_left") &&
                 look == flowchart::FlowLook::HandDrawn) {
        sn.label.x += 7.5;
      } else if (look == flowchart::FlowLook::Neo &&
                 (sn.shapeType == QLatin1String("brace_right") ||
                  sn.shapeType == QLatin1String("braces"))) {
        sn.label.x -= 10.5;
        sn.label.y -= 4.5;
      }
      sn.label.color = rs.color;
      sn.label.fontFamily = rs.fontFamily;
      sn.label.fontSize = rs.fontSize;
      sn.label.fontWeight = rs.fontWeight;
      prepareSceneLabel(sn.label);
      flowchart::FlowTextOptions labelOptions;
      labelOptions.fontFamily = rs.fontFamily;
      bool fontSizeOk = false;
      labelOptions.fontPixelSize = rs.fontSize.endsWith(QLatin1String("px"))
          ? rs.fontSize.chopped(2).toDouble(&fontSizeOk) : 16.0;
      if (!fontSizeOk) labelOptions.fontPixelSize = 16.0;
      labelOptions.lineHeight = labelOptions.fontPixelSize * 1.5;
      labelOptions.look = look;
      const QSizeF labelSize = flowchart::measureLabel(v->text, v->labelType, labelOptions);
      if (sn.shapeType == QLatin1String("lined_cylinder")) {
        sn.label.y += sn.radiusY;
      } else if (sn.shapeType == QLatin1String("flipped_triangle")) {
        sn.label.y += -sn.height / 2.0 + 7.5 + labelSize.height() / 2.0;
      } else if (sn.shapeType == QLatin1String("sloped_rect")) {
        const qreal h = sn.height / 1.5;
        sn.label.x += -sn.width / 2.0 + 15.0 + labelSize.width() / 2.0;
        sn.label.y += -h / 4.0 + 15.0 + labelSize.height() / 2.0;
      } else if (sn.shapeType == QLatin1String("window_pane")) {
        sn.label.x += 5.0;
        sn.label.y += 5.0;
      } else if (sn.shapeType == QLatin1String("divided_rect")) {
        const qreal h = sn.height / 1.2;
        const qreal offset = h * 0.2;
        sn.label.x += -sn.width / 2.0 + 7.5 + labelSize.width() / 2.0;
        sn.label.y += -h / 2.0 + offset / 2.0 + 7.5 + labelSize.height() / 2.0;
      }
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
  scene.interactionRegions_.reserve(scene.nodes.size());
  for (const FlowSceneNode& node : scene.nodes) {
    InteractionRegion region;
    region.bounds = QRectF(node.cx - node.width / 2.0, node.cy - node.height / 2.0,
                           node.width, node.height);
    region.href = node.link;
    region.toolTip = node.tooltip;
    region.accessibleLabel = node.tooltip;
    scene.interactionRegions_.append(region);
  }
  return scene;
}

QJsonObject FlowScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("background")] = background;
  root[QStringLiteral("look")] = flowchart::flowLookName(look);
  if (look == flowchart::FlowLook::HandDrawn)
    root[QStringLiteral("handDrawnSeed")] = static_cast<qint64>(handDrawnSeed);
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
    if (e.animated) {
      o[QStringLiteral("animated")] = true;
      o[QStringLiteral("animation")] = e.animation;
    }
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
    if (!n.link.isEmpty()) o[QStringLiteral("link")] = n.link;
    if (!n.tooltip.isEmpty()) o[QStringLiteral("tooltip")] = n.tooltip;
    nodesJson.append(o);
  }
  root[QStringLiteral("nodes")] = nodesJson;

  return root;
}

QString FlowScene::toJson() const {
  return QJsonDocument(toJsonObject()).toJson(QJsonDocument::Compact);
}

}  // namespace muffin::mermaid::flowscene
