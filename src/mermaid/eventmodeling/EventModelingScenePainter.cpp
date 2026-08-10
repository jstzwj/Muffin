#include "mermaid/eventmodeling/EventModelingScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace muffin::mermaid::eventmodeling {
namespace {

QColor resolved(const QString& value, const QColor& fallback = Qt::black) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Fill, fallback);
  return paint.none ? QColor(Qt::transparent) : paint.color;
}

QFont textFont(const QString& family, qreal size, bool bold) {
  QFont font = flowchart::makeFlowLabelFont(
      family, size, bold ? QFont::Bold : QFont::Normal);
  return font;
}

void drawSwimlaneLabel(const EventModelingScene& scene, QPainter& painter,
                       const EventModelingSwimlaneGeometry& lane) {
  painter.save();
  painter.setFont(textFont(scene.style.fontFamily, scene.style.rootFontSize, true));
  painter.setPen(resolved(scene.style.textColor));
  painter.drawText(lane.labelPosition, lane.label);
  painter.restore();
}

QPen stroke(const QString& value) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Stroke, Qt::black);
  if (paint.none) return QPen(Qt::NoPen);
  QPen pen(paint.color, 1.0);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

QBrush fill(const QString& value) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

void drawArrow(const EventModelingScene& scene, QPainter& painter,
               const QLineF& line) {
  if (line.length() <= 0.0) return;
  const qreal angle = std::atan2(line.dy(), line.dx());
  const QPointF tip = line.p2();
  const QPointF back(tip.x() - 10.0 * std::cos(angle),
                     tip.y() - 10.0 * std::sin(angle));
  const QPointF normal(-std::sin(angle) * 3.5, std::cos(angle) * 3.5);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill(scene.style.arrowhead));
  painter.drawPolygon(QPolygonF{back + normal, tip, back - normal});
  painter.restore();
}

}  // namespace

void paintEventModelingScene(const EventModelingScene& scene,
                             QPainter& painter,
                             const MermaidPaintOptions&) {
  painter.save();
  for (const auto& lane : scene.swimlanes) {
    painter.setPen(stroke(scene.style.swimlaneStroke));
    painter.setBrush(fill(scene.style.swimlaneFill));
    painter.drawRoundedRect(lane.rect, 3.0, 3.0);
    drawSwimlaneLabel(scene, painter, lane);
  }
  for (const auto& box : scene.boxes) {
    painter.setPen(stroke(box.stroke));
    painter.setBrush(fill(box.fill));
    painter.drawRoundedRect(box.rect, 3.0, 3.0);
    flowchart::paintFlowLabel(
        painter, box.label, box.foreignObjectRect, scene.style.fontFamily,
        scene.style.rootFontSize, 1.2, resolved(scene.style.textColor), true,
        flowchart::FlowLabelAlign::Center);
  }
  for (const auto& relation : scene.relations) {
    const QPen pen = stroke(relation.stroke);
    if (pen.style() != Qt::NoPen) {
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(relation.line);
    }
    drawArrow(scene, painter, relation.line);
  }
  painter.restore();
}

}  // namespace muffin::mermaid::eventmodeling
