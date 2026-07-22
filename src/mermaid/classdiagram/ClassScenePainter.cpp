#include "mermaid/classdiagram/ClassScenePainter.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::classdiagram {
namespace {

QColor color(const QString& value) {
  return mermaid::color::toQColor(value);
}

QPainterPath painterPath(const QString& source) {
  static const QRegularExpression token(
      QStringLiteral("[A-Za-z]|[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
  QStringList tokens;
  auto matches = token.globalMatch(source);
  while (matches.hasNext()) tokens.append(matches.next().captured());
  QPainterPath path;
  qsizetype i = 0;
  QChar command;
  QPointF current;
  QPointF subpathStart;
  const auto isCommand = [](const QString& value) {
    return value.size() == 1 && value.front().isLetter();
  };
  const auto number = [&]() { return tokens.value(i++).toDouble(); };
  const auto point = [&](bool relative) {
    const qreal x = number();
    const qreal y = number();
    QPointF value(x, y);
    if (relative) value += current;
    return value;
  };
  while (i < tokens.size()) {
    if (isCommand(tokens.at(i))) command = tokens.at(i++).front();
    if (command.isNull()) break;
    const bool relative = command.isLower();
    const QChar upper = command.toUpper();
    if (upper == QLatin1Char('Z')) {
      path.closeSubpath();
      current = subpathStart;
      command = {};
    } else if (upper == QLatin1Char('M')) {
      current = point(relative);
      path.moveTo(current);
      subpathStart = current;
      command = relative ? QLatin1Char('l') : QLatin1Char('L');
    } else if (upper == QLatin1Char('L')) {
      current = point(relative);
      path.lineTo(current);
    } else if (upper == QLatin1Char('H')) {
      current.setX(number() + (relative ? current.x() : 0.0));
      path.lineTo(current);
    } else if (upper == QLatin1Char('V')) {
      current.setY(number() + (relative ? current.y() : 0.0));
      path.lineTo(current);
    } else if (upper == QLatin1Char('C')) {
      const QPointF first = point(relative);
      const QPointF second = point(relative);
      current = point(relative);
      path.cubicTo(first, second, current);
    } else {
      command = {};
    }
  }
  return path;
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
    painter.drawPath(painterPath(definition->child.path));
  painter.restore();
}

}  // namespace

void paintClassScene(const ClassScene& scene, QPainter& painter,
                     ClassPaintMode mode) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  for (const auto& cluster : scene.clusters) {
    const QColor clusterColor = mode == ClassPaintMode::SemanticMask
        ? QColor::fromRgba(kClassMaskCluster) : color(scene.style.clusterStroke);
    if (mode != ClassPaintMode::TextMask) {
      painter.setPen(QPen(clusterColor, 1.0));
      painter.setBrush(mode == ClassPaintMode::SemanticMask
                           ? clusterColor : color(scene.style.clusterFill));
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
    QPen pen(mode == ClassPaintMode::SemanticMask
                 ? QColor::fromRgba(kClassMaskEdge)
                 : color(scene.style.lineColor), scene.style.strokeWidth);
    if (edge.pattern == QLatin1String("dashed")) pen.setDashPattern({3.0, 3.0});
    else if (edge.pattern == QLatin1String("dotted")) pen.setDashPattern({2.0, 2.0});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(painterPath(edge.path));
    drawMarker(painter, scene, edge.markerStart, true, edge.renderedPoints, mode);
    drawMarker(painter, scene, edge.markerEnd, false, edge.renderedPoints, mode);
  }

  for (const auto& edge : scene.edges) {
    if (!edge.label.isEmpty() && edge.labelPosition) {
      auto document = flowchart::parseFlowLabel(edge.label, QStringLiteral("markdown"), true);
      document.formattingContext =
          flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
      flowchart::prepareFlowLabelMath(document, scene.style.fontSize);
      const QSizeF size = flowchart::measureFlowLabel(
          document, scene.style.fontFamily, scene.style.fontSize, scene.style.lineHeight);
      const QRectF rect(edge.labelPosition->x() - size.width() / 2.0,
                        edge.labelPosition->y() - size.height() / 2.0,
                        size.width(), size.height());
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
    const auto terminal = [&](const std::optional<ClassSceneTerminalLabel>& label) {
      if (!label) return;
      const QSizeF size = flowchart::measureFlowLabel(
          label->document, scene.style.fontFamily, 11.0, 12.0);
      const QRectF rect(label->center.x() - size.width() / 2.0,
                        label->center.y() - size.height() / 2.0,
                        size.width(), size.height());
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
    const bool transparentOuter = std::any_of(node.cssStyles.cbegin(), node.cssStyles.cend(),
        [](const QString& style) { return style.contains(QStringLiteral("opacity: 0")); });
    if (!transparentOuter && mode != ClassPaintMode::TextMask) {
      const QRectF outer = node.localOuter.translated(node.center);
      const QColor nodeColor = mode == ClassPaintMode::SemanticMask
          ? QColor::fromRgba(kClassMaskNode) : color(node.stroke);
      painter.setPen(QPen(nodeColor, node.strokeWidth));
      painter.setBrush(mode == ClassPaintMode::SemanticMask
                           ? nodeColor : color(node.fill));
      painter.drawRect(outer);
      for (const QRectF& divider : node.localDividers)
        painter.drawRect(divider.translated(node.center));
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

}  // namespace muffin::mermaid::classdiagram
