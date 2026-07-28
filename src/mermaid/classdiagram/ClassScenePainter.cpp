#include "mermaid/classdiagram/ClassScenePainter.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::classdiagram {
namespace {

QColor color(const QString& value) {
  return mermaid::color::toQColor(value);
}

void drawLabel(QPainter& painter, const ClassSceneLabel& label,
               const QPointF& nodeCenter, const ClassSceneStyle& style,
               bool centered = true) {
  if (label.text.isEmpty()) return;
  const QPointF center = nodeCenter + label.center;
  const QRectF rect(center.x() - label.size.width() / 2.0,
                    center.y() - label.size.height() / 2.0,
                    label.size.width(), label.size.height());
  painter.save();
  painter.setClipRect(rect);
  flowchart::paintFlowLabel(painter, label.document, rect, style.fontFamily,
                            style.fontSize, style.lineHeight,
                            color(style.textColor), centered);
  painter.restore();
}

const ClassMarkerDefinition* markerDefinition(
    const ClassScene& scene, const QString& type, bool start) {
  if (type.isEmpty() || type == QLatin1String("none")) return nullptr;
  const QString suffix = type + (start ? QStringLiteral("Start")
                                      : QStringLiteral("End"));
  for (const auto& marker : scene.markers)
    if (marker.suffix == suffix) return &marker;
  return nullptr;
}

void drawMarker(QPainter& painter, const ClassScene& scene, const QString& type,
                bool start, const QVector<QPointF>& points,
                ClassPaintMode mode) {
  const ClassMarkerDefinition* definition = markerDefinition(scene, type, start);
  if (!definition || points.size() < 2) return;
  const QPointF endpoint = start ? points.first() : points.last();
  const QPointF neighbor = start ? points.at(1) : points.at(points.size() - 2);
  const QPointF tangent = start ? neighbor - endpoint : endpoint - neighbor;
  const qreal angle = std::atan2(tangent.y(), tangent.x()) * 180.0 / 3.14159265358979323846;
  painter.save();
  painter.translate(endpoint);
  painter.rotate(angle);
  painter.translate(-definition->refX, -definition->refY);
  const QColor markerColor = mode == ClassPaintMode::SemanticMask
      ? QColor::fromRgba(kClassMaskMarker) : color(scene.style.lineColor);
  QPen pen(markerColor, 1.0);
  painter.setPen(pen);
  if (type == QLatin1String("composition") || type == QLatin1String("dependency"))
    painter.setBrush(markerColor);
  else if (type == QLatin1String("lollipop"))
    painter.setBrush(mode == ClassPaintMode::SemanticMask
                         ? Qt::NoBrush : QBrush(color(scene.style.classFill)));
  else
    painter.setBrush(Qt::NoBrush);
  if (definition->child.tag == QLatin1String("circle"))
    painter.drawEllipse(QPointF(definition->child.cx, definition->child.cy),
                        definition->child.radius, definition->child.radius);
  else
    painter.drawPath(scene::parseSvgPath(definition->child.path));
  painter.restore();
}

}  // namespace

void paintClassScene(const ClassScene& scene, QPainter& painter,
                     ClassPaintMode mode,
                     const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  for (const auto& cluster : scene.clusters) {
    if (!mermaidPrimitiveIsVisible(cluster.bounds, options)) continue;
    const QColor clusterColor = mode == ClassPaintMode::SemanticMask
        ? QColor::fromRgba(kClassMaskCluster) : color(scene.style.clusterStroke);
    if (mode != ClassPaintMode::TextMask) {
      painter.setPen(QPen(clusterColor, 1.0));
      painter.setBrush(mode == ClassPaintMode::SemanticMask
                           ? clusterColor : color(scene.style.clusterFill));
      if (mode == ClassPaintMode::Color && scene.handDrawn)
        rough::roughRect(painter, cluster.bounds, scene.handDrawnSeed,
                         color(scene.style.clusterFill),
                         color(scene.style.clusterStroke), 1.0);
      else
        painter.drawRect(cluster.bounds);
    }
    if (!cluster.titleLabel.text.isEmpty()) {
      const QRectF labelRect(
          cluster.titleLabel.center.x() - cluster.titleLabel.size.width() / 2.0,
          cluster.titleLabel.center.y() - cluster.titleLabel.size.height() / 2.0,
          cluster.titleLabel.size.width(), cluster.titleLabel.size.height());
      painter.save();
      painter.setClipRect(labelRect);
      flowchart::paintFlowLabel(painter, cluster.titleLabel.document, labelRect,
          scene.style.fontFamily, scene.style.fontSize, scene.style.lineHeight,
          mode != ClassPaintMode::Color
              ? QColor::fromRgba(kClassMaskText)
              : color(scene.style.titleColor), true);
      painter.restore();
    }
  }

  for (const auto& edge : scene.edges) {
    if (mode == ClassPaintMode::TextMask) continue;
    if (!mermaidPrimitiveIsVisible(
            edge.pathBounds.isValid() ? edge.pathBounds : scene.bounds,
            options))
      continue;
    QPen pen(mode == ClassPaintMode::SemanticMask
                 ? QColor::fromRgba(kClassMaskEdge)
                 : color(scene.style.lineColor), scene.style.strokeWidth);
    if (edge.pattern == QLatin1String("dashed")) pen.setDashPattern({3.0, 3.0});
    else if (edge.pattern == QLatin1String("dotted")) pen.setDashPattern({2.0, 2.0});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (mode == ClassPaintMode::Color && scene.handDrawn) {
      for (const QString& path : edge.paths)
        rough::roughPath(painter, scene::parseSvgPath(path), scene.handDrawnSeed,
                         color(scene.style.lineColor), scene.style.strokeWidth);
    } else {
      for (const QString& path : edge.paths) painter.drawPath(scene::parseSvgPath(path));
    }
    const QVector<QPointF>& startPoints = edge.renderedSegments.isEmpty()
        ? edge.renderedPoints : edge.renderedSegments.first();
    const QVector<QPointF>& endPoints = edge.renderedSegments.isEmpty()
        ? edge.renderedPoints : edge.renderedSegments.last();
    drawMarker(painter, scene, edge.markerStart, true, startPoints, mode);
    drawMarker(painter, scene, edge.markerEnd, false, endPoints, mode);
  }

  for (const auto& edge : scene.edges) {
    if (!edge.label.isEmpty() && edge.labelPosition) {
      flowchart::FlowLabelDocument document = edge.labelDocument;
      QSizeF size = edge.labelSize;
      if (document.text.isEmpty()) {
        document = flowchart::parseFlowLabel(
            edge.label, QStringLiteral("markdown"), true);
        document.formattingContext =
            flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
        flowchart::prepareFlowLabelMath(document, scene.style.fontSize);
        size = flowchart::measureFlowLabel(
            document, scene.style.fontFamily, scene.style.fontSize,
            scene.style.lineHeight);
      }
      const QRectF rect(edge.labelPosition->x() - size.width() / 2.0,
                        edge.labelPosition->y() - size.height() / 2.0,
                        size.width(), size.height());
      if (mermaidPrimitiveIsVisible(
              edge.labelBounds.isValid() ? edge.labelBounds : rect, options)) {
        if (mode != ClassPaintMode::TextMask)
          painter.fillRect(rect, mode == ClassPaintMode::SemanticMask
                                     ? QColor::fromRgba(kClassMaskEdgeLabel)
                                     : color(scene.style.edgeLabelFill));
        painter.save();
        painter.setClipRect(rect);
        flowchart::paintFlowLabel(painter, document, rect, scene.style.fontFamily,
            scene.style.fontSize, scene.style.lineHeight,
            mode != ClassPaintMode::Color
                ? QColor::fromRgba(kClassMaskText)
                : color(scene.style.textColor), true);
        painter.restore();
      }
    }
    const auto terminal = [&](const std::optional<ClassSceneTerminalLabel>& label) {
      if (!label) return;
      const QSizeF size = label->size.isValid()
          ? label->size
          : flowchart::measureFlowLabel(
                label->document, scene.style.fontFamily, 11.0, 12.0);
      const QRectF rect(label->center.x() - size.width() / 2.0,
                        label->center.y() - size.height() / 2.0,
                        size.width(), size.height());
      if (!mermaidPrimitiveIsVisible(rect, options)) return;
      painter.save();
      painter.setClipRect(rect);
      flowchart::paintFlowLabel(painter, label->document, rect,
          scene.style.fontFamily, 11.0, 12.0,
          mode != ClassPaintMode::Color
              ? QColor::fromRgba(kClassMaskText)
              : color(scene.style.textColor), true);
      painter.restore();
    };
    terminal(edge.startLabelRight);
    terminal(edge.endLabelLeft);
  }

  for (const auto& node : scene.nodes) {
    const QRectF nodeBounds = node.localOuter.isValid()
        ? node.localOuter.translated(node.center)
        : QRectF(node.center - QPointF(node.size.width() / 2.0,
                                       node.size.height() / 2.0),
                 node.size);
    if (!mermaidPrimitiveIsVisible(nodeBounds, options)) continue;
    const bool transparentOuter = std::any_of(node.cssStyles.cbegin(), node.cssStyles.cend(),
        [](const QString& style) { return style.contains(QStringLiteral("opacity: 0")); });
    if (!transparentOuter && mode != ClassPaintMode::TextMask) {
      const QRectF outer = node.localOuter.translated(node.center);
      const QColor nodeColor = mode == ClassPaintMode::SemanticMask
          ? QColor::fromRgba(kClassMaskNode) : color(node.stroke);
      painter.setPen(QPen(nodeColor, node.strokeWidth));
      painter.setBrush(mode == ClassPaintMode::SemanticMask
                           ? nodeColor : color(node.fill));
      if (mode == ClassPaintMode::Color && scene.handDrawn) {
        rough::roughRect(painter, outer, scene.handDrawnSeed,
                         color(node.fill), color(node.stroke), node.strokeWidth);
        for (const QRectF& divider : node.localDividers)
          rough::roughRect(painter, divider.translated(node.center), scene.handDrawnSeed,
                           color(node.fill), color(node.stroke), node.strokeWidth);
      } else {
        painter.drawRect(outer);
        for (const QRectF& divider : node.localDividers)
          painter.drawRect(divider.translated(node.center));
      }
    }
    ClassSceneStyle nodeStyle = scene.style;
    nodeStyle.textColor = mode != ClassPaintMode::Color
        ? QStringLiteral("#ff00ff") : node.textColor;
    for (const auto& label : node.annotationLabels) drawLabel(painter, label, node.center, nodeStyle);
    for (const auto& label : node.nameLabels) drawLabel(painter, label, node.center, nodeStyle);
    for (const auto& label : node.memberLabels) drawLabel(painter, label, node.center, nodeStyle, false);
    for (const auto& label : node.methodLabels) drawLabel(painter, label, node.center, nodeStyle, false);
  }
}

QImage renderClassSceneToImage(const ClassScene& scene, qreal dpr, qreal padding,
                               ClassPaintMode mode) {
  const qreal width = std::max(1.0, scene.bounds.width() + 2.0 * padding);
  const qreal height = std::max(1.0, scene.bounds.height() + 2.0 * padding);
  QImage image(qCeil(width * dpr), qCeil(height * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintClassScene(scene, painter, mode);
  return image;
}

void ClassScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintClassScene(*this, painter, ClassPaintMode::Color, options);
}

}  // namespace muffin::mermaid::classdiagram
