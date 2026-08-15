#include "mermaid/state/StateScenePainter.h"

#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::state {
namespace {
QColor color(const QString& value) { return mermaid::color::toQColor(value); }

QPainterPath edgePath(const StateSceneEdge& edge) {
  if (!edge.path.isEmpty()) return scene::parseSvgPath(edge.path);
  QPainterPath path;
  const QVector<QPointF> points = !edge.points.isEmpty() ? edge.points
      : edge.segments.isEmpty() ? QVector<QPointF>{} : edge.segments.first();
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  return path;
}

void paintLabel(QPainter& painter, const flowchart::FlowLabelDocument& document,
                const QRectF& bounds, const StateSceneStyle& style,
                const QColor& textColor) {
  if (document.text.isEmpty()) return;
  painter.save();
  painter.setClipRect(bounds);
  flowchart::paintFlowLabel(painter, document, bounds, style.fontFamily,
      style.fontSize, style.lineHeight, textColor, true);
  painter.restore();
}

void paintNodeLabel(QPainter& painter, const StateSceneNode& node,
                    const StateSceneStyle& style, const QColor& textColor) {
  if (node.label.isEmpty()) return;
  if (node.descriptions.isEmpty()) {
    paintLabel(painter, node.labelDocument, node.bounds, style, textColor);
    return;
  }
  QRectF title = node.bounds;
  title.setHeight(style.lineHeight + 16.0);
  paintLabel(painter, node.labelDocument, title, style, textColor);
  qreal y = title.bottom();
  for (const auto& document : node.descriptionDocuments) {
    QRectF line(node.bounds.left() + 8.0, y,
                node.bounds.width() - 16.0, style.lineHeight);
    paintLabel(painter, document, line, style, textColor);
    y += style.lineHeight;
  }
}

void paintArrow(QPainter& painter, const StateSceneEdge& edge,
                const QColor& stroke) {
  if (edge.markerEnd.isEmpty() || edge.markerEnd == QLatin1String("none") ||
      edge.points.size() < 2) return;
  const QPointF end = edge.points.last();
  const QPointF before = edge.points.at(edge.points.size() - 2);
  const qreal angle = std::atan2(end.y() - before.y(), end.x() - before.x());
  constexpr qreal length = 10.0;
  constexpr qreal wing = 4.0;
  const QPointF back(std::cos(angle) * length, std::sin(angle) * length);
  const QPointF normal(-std::sin(angle) * wing, std::cos(angle) * wing);
  QPainterPath marker;
  marker.moveTo(end); marker.lineTo(end - back + normal);
  marker.lineTo(end - back - normal); marker.closeSubpath();
  painter.setPen(Qt::NoPen); painter.setBrush(stroke); painter.drawPath(marker);
}
}

void paintStateScene(const StateScene& scene, QPainter& painter,
                     const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const QColor transition = color(scene.style.transitionColor);
  for (const StateSceneNode& cluster : scene.clusters) {
    if (!mermaidPrimitiveIsVisible(
            scene.handDrawn && cluster.paintedBounds.isValid()
                ? cluster.paintedBounds : cluster.bounds,
            options)) continue;
    if (cluster.shape == QLatin1String("noteGroup")) continue;
    painter.setPen(QPen(color(cluster.stroke), cluster.strokeWidth));
    painter.setBrush(color(cluster.fill));
    if (scene.handDrawn) {
      for (qsizetype i = 0; i < cluster.roughDrawables.size(); ++i) {
        const rough::Drawable& drawable = cluster.roughDrawables.at(i);
        const QColor fill = i == 0 ? color(cluster.fill)
                                   : color(cluster.innerFill);
        rough::drawRoughDrawable(
            painter, drawable, fill,
            QPen(color(cluster.stroke), cluster.strokeWidth),
            QPen(fill, cluster.strokeWidth));
      }
    } else {
      painter.drawRoundedRect(cluster.bounds, 5.0, 5.0);
      if (cluster.innerBounds.isValid()) {
        painter.setBrush(color(cluster.innerFill));
        painter.drawRect(cluster.innerBounds);
      }
    }
    if (cluster.shape == QLatin1String("divider")) {
      painter.drawLine(cluster.bounds.left(), cluster.bounds.top(),
                       cluster.bounds.left(), cluster.bounds.bottom());
    } else if (!cluster.label.isEmpty()) {
      QRectF title = cluster.bounds.adjusted(8.0, 0.0, -8.0, 0.0);
      title.setHeight(scene.style.lineHeight + 16.0);
      paintLabel(painter, cluster.labelDocument, title, scene.style,
                 color(cluster.textColor));
    }
  }
  for (const StateSceneEdge& edge : scene.edges) {
    if (!mermaidPrimitiveIsVisible(
            edge.pathBounds.isValid() ? edge.pathBounds : scene.bounds,
            options))
      continue;
    // Resolved edge paint (linkStyle / edge classDef via MermaidStyleResolve);
    // empty resolved values fall back to the transition colour/width.
    const QColor edgeColor = edge.stroke.isEmpty() ? transition : color(edge.stroke);
    qreal edgeWidth = scene.style.strokeWidth;
    if (!edge.strokeWidth.isEmpty()) {
      QString widthToken = edge.strokeWidth;
      if (widthToken.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) widthToken.chop(2);
      bool ok = false;
      const qreal resolved = widthToken.trimmed().toDouble(&ok);
      if (ok && resolved >= 0.0) edgeWidth = resolved;
    }
    QPen pen(edgeColor, edgeWidth);
    if (!edge.strokeDasharray.isEmpty()) {
      // CSS stroke-dasharray is in user units; QPen dash entries are
      // multiples of the pen width, so normalize by edgeWidth. SVG lines are
      // butt-capped by default — SquareCap would extend each dash by half the
      // width on both ends.
      QVector<qreal> dash;
      for (const QString& token : edge.strokeDasharray.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const qreal d = token.trimmed().toDouble(&ok);
        if (ok && d > 0.0) dash.append(d);
      }
      if (!dash.isEmpty() && edgeWidth > 0.0) {
        for (qreal& entry : dash) entry /= edgeWidth;
        pen.setDashPattern(dash);
        pen.setCapStyle(Qt::FlatCap);
      }
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (scene.handDrawn)
      rough::drawRoughDrawable(painter, edge.roughDrawable, Qt::NoBrush,
                               QPen(edgeColor, edgeWidth), Qt::NoPen);
    else
      painter.drawPath(edgePath(edge));
    if (options.paintEdgeMarkers) paintArrow(painter, edge, edgeColor);
  }
  for (const StateSceneEdge& edge : scene.edges) {
    if (edge.label.isEmpty() || !edge.labelPosition) continue;
    const QSizeF size = edge.labelSize.isValid()
        ? edge.labelSize
        : flowchart::measureFlowLabel(edge.labelDocument,
              scene.style.fontFamily, scene.style.fontSize,
              scene.style.lineHeight);
    const QRectF bounds(*edge.labelPosition - QPointF(size.width() / 2.0,
                                                       size.height() / 2.0), size);
    if (!mermaidPrimitiveIsVisible(
            edge.labelBounds.isValid() ? edge.labelBounds : bounds, options))
      continue;
    // `.edgeLabel .label rect { fill: labelBackgroundColor; opacity: 0.5 }` —
    // element opacity MULTIPLIES the fill color's own alpha channel (an
    // rgba(…,0.2) override renders at 0.1); the text takes
    // transitionLabelColor (`|| tertiaryTextColor`).
    QColor labelBackground = color(scene.style.edgeLabelFill);
    labelBackground.setAlphaF(labelBackground.alphaF() * 0.5);
    painter.fillRect(bounds, labelBackground);
    paintLabel(painter, edge.labelDocument, bounds, scene.style,
               color(scene.style.transitionLabelColor.isEmpty()
                         ? scene.style.textColor
                         : scene.style.transitionLabelColor));
  }
  for (const StateSceneNode& node : scene.nodes) {
    if (!mermaidPrimitiveIsVisible(node.bounds, options)) continue;
    const QString shape = node.shape;
    const QPointF center = node.bounds.center();
    if (!node.shapeVisible && (shape.isEmpty() || shape == QLatin1String("rect"))) {
      // `.node rect { display:none }`: text-only node, no shape box.
      continue;
    }
    painter.setPen(QPen(color(node.stroke), node.strokeWidth));
    painter.setBrush(color(node.fill));
    // 11.16 rendering-util shapes: `.node circle.state-start` paints
    // fill+stroke specialStateColor; stateEnd is a rough pair whose ring takes
    // the node fill (userNodeOverrides' mainBkg) + lineColor stroke and whose
    // inner dot is fill+stroke `stateBorder ?? nodeBorder`. handDrawn start
    // keeps solidStateFill(lineColor).
    const QColor special = color(scene.style.specialStateColor.isEmpty()
                                     ? scene.style.transitionColor
                                     : scene.style.specialStateColor);
    const QColor endInner = color(scene.style.endInnerFill.isEmpty()
                                      ? scene.style.transitionColor
                                      : scene.style.endInnerFill);
    if (shape == QLatin1String("stateStart")) {
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable, transition, QPen(transition, 1.0),
              QPen(transition, 1.0));
      } else {
        painter.setPen(QPen(special, 1.0)); painter.setBrush(special);
        painter.drawEllipse(center, 7.0, 7.0);
      }
      continue;
    }
    if (shape == QLatin1String("stateEnd")) {
      if (scene.handDrawn) {
        for (qsizetype i = 0; i < node.roughDrawables.size(); ++i)
          rough::drawRoughDrawable(
              painter, node.roughDrawables.at(i),
              i == 0 ? QBrush(color(node.fill)) : QBrush(endInner),
              QPen(transition, 2.0),
              i == 0 ? QPen(transition, 2.0) : QPen(endInner, 2.0));
      } else {
        painter.setPen(QPen(transition, 2.0));
        painter.setBrush(color(node.fill));
        painter.drawEllipse(center, 7.0, 7.0);
        painter.setPen(QPen(endInner, 2.0)); painter.setBrush(endInner);
        painter.drawEllipse(center, 2.5, 2.5);
      }
      continue;
    }
    if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable, transition, QPen(transition, 1.0),
              QPen(transition, 1.0));
      } else {
        painter.setPen(Qt::NoPen); painter.setBrush(transition);
        painter.drawRoundedRect(node.bounds, 1.0, 1.0);
      }
      continue;
    }
    if (shape == QLatin1String("choice")) {
      QPolygonF diamond{QPointF(center.x(), node.bounds.top()),
          QPointF(node.bounds.right(), center.y()),
          QPointF(center.x(), node.bounds.bottom()),
          QPointF(node.bounds.left(), center.y())};
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable, color(node.fill),
              QPen(color(node.stroke), node.strokeWidth),
              QPen(color(node.fill), node.strokeWidth));
      } else {
        painter.drawPolygon(diamond);
      }
      continue;
    }
    if (shape == QLatin1String("note")) {
      if (scene.handDrawn)
        rough::drawRoughDrawable(
            painter, node.roughDrawables.value(0), color(node.fill),
            QPen(color(node.stroke), node.strokeWidth),
            QPen(color(node.fill), node.strokeWidth));
      else
        painter.drawRect(node.bounds);
      paintNodeLabel(painter, node, scene.style, color(node.textColor));
      continue;
    }
    if (scene.handDrawn)
      rough::drawRoughDrawable(
          painter, node.roughDrawables.value(0), color(node.fill),
          QPen(color(node.stroke), node.strokeWidth),
          QPen(color(node.fill), node.strokeWidth));
    else
      painter.drawRoundedRect(node.bounds, 5.0, 5.0);
    if (!node.descriptions.isEmpty()) {
      const qreal dividerY = node.bounds.top() + scene.style.lineHeight + 16.0;
      if (scene.handDrawn)
        rough::drawRoughDrawable(
            painter, node.roughDrawables.value(1), Qt::NoBrush,
            QPen(color(node.stroke), node.strokeWidth), Qt::NoPen);
      else
        painter.drawLine(node.bounds.left(), dividerY, node.bounds.right(), dividerY);
    }
    paintNodeLabel(painter, node, scene.style, color(node.textColor));
  }
}

QImage renderStateSceneToImage(const StateScene& scene, qreal dpr, qreal padding) {
  const qreal width = std::max(1.0, scene.bounds.width() + 2.0 * padding);
  const qreal height = std::max(1.0, scene.bounds.height() + 2.0 * padding);
  QImage image(qCeil(width * dpr), qCeil(height * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintStateScene(scene, painter);
  painter.end();
  image.setDevicePixelRatio(dpr);
  return image;
}

void StateScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintStateScene(*this, painter, options);
}

}  // namespace muffin::mermaid::state
