#include "mermaid/state/StateScenePainter.h"

#include "mermaid/rough/RoughPaint.h"
#include "mermaid/theme/MermaidColor.h"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::state {
namespace {
QColor color(const QString& value) { return mermaid::color::toQColor(value); }

QPainterPath svgPath(const QString& source) {
  static const QRegularExpression token(
      QStringLiteral("[A-Za-z]|[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
  QStringList tokens;
  auto matches = token.globalMatch(source);
  while (matches.hasNext()) tokens.append(matches.next().captured());
  QPainterPath path;
  qsizetype i = 0;
  QChar command;
  QPointF current, start;
  const auto isCommand = [](const QString& value) {
    return value.size() == 1 && value.front().isLetter();
  };
  const auto number = [&]() { return tokens.value(i++).toDouble(); };
  const auto point = [&](bool relative) {
    QPointF value(number(), number());
    return relative ? value + current : value;
  };
  while (i < tokens.size()) {
    if (isCommand(tokens.at(i))) command = tokens.at(i++).front();
    if (command.isNull()) break;
    const bool relative = command.isLower();
    const QChar upper = command.toUpper();
    if (upper == QLatin1Char('M')) {
      current = point(relative); path.moveTo(current); start = current;
      command = relative ? QLatin1Char('l') : QLatin1Char('L');
    } else if (upper == QLatin1Char('L')) {
      current = point(relative); path.lineTo(current);
    } else if (upper == QLatin1Char('C')) {
      const QPointF first = point(relative), second = point(relative);
      current = point(relative); path.cubicTo(first, second, current);
    } else if (upper == QLatin1Char('Z')) {
      path.closeSubpath(); current = start; command = {};
    } else command = {};
  }
  return path;
}

QPainterPath edgePath(const StateSceneEdge& edge) {
  if (!edge.path.isEmpty()) return svgPath(edge.path);
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
    if (!mermaidPrimitiveIsVisible(cluster.bounds, options)) continue;
    if (cluster.shape == QLatin1String("noteGroup")) continue;
    painter.setPen(QPen(color(cluster.stroke), cluster.strokeWidth));
    painter.setBrush(color(cluster.fill));
    if (scene.handDrawn)
      rough::roughRect(painter, cluster.bounds, scene.handDrawnSeed,
                       color(cluster.fill), color(cluster.stroke), cluster.strokeWidth);
    else
      painter.drawRoundedRect(cluster.bounds, 5.0, 5.0);
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
    painter.setPen(QPen(transition, scene.style.strokeWidth));
    painter.setBrush(Qt::NoBrush);
    if (scene.handDrawn)
      rough::roughPath(painter, edgePath(edge), scene.handDrawnSeed,
                       transition, scene.style.strokeWidth);
    else
      painter.drawPath(edgePath(edge));
    paintArrow(painter, edge, transition);
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
    painter.fillRect(bounds, color(scene.style.edgeLabelFill));
    paintLabel(painter, edge.labelDocument, bounds, scene.style,
               color(scene.style.textColor));
  }
  for (const StateSceneNode& node : scene.nodes) {
    if (!mermaidPrimitiveIsVisible(node.bounds, options)) continue;
    const QString shape = node.shape;
    const QPointF center = node.bounds.center();
    painter.setPen(QPen(color(node.stroke), node.strokeWidth));
    painter.setBrush(color(node.fill));
    if (shape == QLatin1String("stateStart")) {
      painter.setPen(Qt::NoPen); painter.setBrush(transition);
      painter.drawEllipse(center, 7.0, 7.0); continue;
    }
    if (shape == QLatin1String("stateEnd")) {
      painter.setPen(QPen(transition, 2.0)); painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(center, 7.0, 7.0);
      painter.setPen(Qt::NoPen); painter.setBrush(transition);
      painter.drawEllipse(center, 2.5, 2.5); continue;
    }
    if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
      painter.setPen(Qt::NoPen); painter.setBrush(transition);
      painter.drawRoundedRect(node.bounds, 1.0, 1.0); continue;
    }
    if (shape == QLatin1String("choice")) {
      QPolygonF diamond{QPointF(center.x(), node.bounds.top()),
          QPointF(node.bounds.right(), center.y()),
          QPointF(center.x(), node.bounds.bottom()),
          QPointF(node.bounds.left(), center.y())};
      painter.drawPolygon(diamond); continue;
    }
    if (shape == QLatin1String("note")) {
      if (scene.handDrawn)
        rough::roughRect(painter, node.bounds, scene.handDrawnSeed,
                         color(node.fill), color(node.stroke), node.strokeWidth);
      else
        painter.drawRect(node.bounds);
      paintNodeLabel(painter, node, scene.style, color(node.textColor));
      continue;
    }
    if (scene.handDrawn)
      rough::roughRect(painter, node.bounds, scene.handDrawnSeed,
                       color(node.fill), color(node.stroke), node.strokeWidth);
    else
      painter.drawRoundedRect(node.bounds, 5.0, 5.0);
    if (!node.descriptions.isEmpty()) {
      const qreal dividerY = node.bounds.top() + scene.style.lineHeight + 16.0;
      if (scene.handDrawn)
        rough::roughLine(painter, QPointF(node.bounds.left(), dividerY),
                         QPointF(node.bounds.right(), dividerY),
                         scene.handDrawnSeed, color(node.stroke), node.strokeWidth);
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

}  // namespace muffin::mermaid::state
