#include "mermaid/wardley/WardleyScenePainter.h"

#include "mermaid/wardley/WardleyScene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::wardley {
namespace {

QString visibleSvgText(QString value) {
  value.replace(QRegularExpression(QStringLiteral(R"([\t\n\r\f ]+)")),
                QStringLiteral(" "));
  return value.trimmed();
}

QStringList families(const QString &expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

color::SvgPaint fillPaint(const QString &value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
}

QPen pen(const WardleyPrimitive &primitive) {
  const auto stroke = color::resolveSvgPaint(
      primitive.stroke, color::SvgPaintKind::Stroke, Qt::black);
  if (stroke.none || !(primitive.strokeWidth > 0.0)) return Qt::NoPen;
  QColor c = stroke.color;
  c.setAlphaF(c.alphaF() * qBound(0.0, primitive.opacity, 1.0));
  QPen result(c, primitive.strokeWidth);
  result.setCapStyle(Qt::FlatCap);
  result.setJoinStyle(Qt::MiterJoin);
  if (!primitive.dash.isEmpty()) {
    QVector<qreal> scaled;
    for (qreal value : primitive.dash)
      scaled.append(value / primitive.strokeWidth);
    result.setDashPattern(scaled);
  }
  return result;
}

void arrowHead(QPainter &painter, const QPointF &at, const QPointF &direction,
               const QString &colorValue, qreal size) {
  const auto paint = fillPaint(colorValue);
  if (paint.none) return;
  const qreal angle = std::atan2(direction.y(), direction.x());
  QPolygonF triangle;
  triangle << QPointF(0.5, 0.0) << QPointF(-4.5, -2.5)
           << QPointF(-4.5, 2.5);
  QTransform transform;
  transform.translate(at.x(), at.y());
  transform.rotateRadians(angle);
  transform.scale(size / 5.0, size / 5.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(paint.color);
  painter.drawPolygon(transform.map(triangle));
}

void paintText(QPainter &painter, const WardleyScene &scene,
               const WardleyPrimitive &primitive) {
  if (primitive.text.isEmpty() || !(primitive.fontSize > 0.0)) return;
  const auto fill = color::resolveSvgPaint(
      primitive.fill, color::SvgPaintKind::Text, Qt::black);
  if (fill.none) return;
  const QStringList stack = families(scene.style.fontFamily);
  auto font = editor::makeUnhintedCssPixelFont(stack.first(), primitive.fontSize);
  if (stack.size() > 1) font.font.setFamilies(stack);
  font.font.setWeight(primitive.bold ? QFont::Bold : QFont::Normal);
  const QString text = visibleSvgText(primitive.text);
  const qreal advance = QFontMetricsF(font.font).horizontalAdvance(text) * font.scale;
  qreal x = 0.0;
  if (primitive.anchor == QLatin1String("middle")) x -= advance / 2.0;
  else if (primitive.anchor == QLatin1String("end")) x -= advance;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      scene.style.fontFamily, primitive.fontSize,
      primitive.bold ? QFont::Bold : QFont::Normal, QFont::StyleNormal);
  qreal baseline = 0.0;
  if (primitive.baseline == WardleyTextBaseline::Middle ||
      primitive.baseline == WardleyTextBaseline::Central)
    baseline += metrics.xHeight / 2.0;
  painter.save();
  painter.translate(primitive.position);
  painter.rotate(primitive.rotation);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.drawText(QPointF(x / font.scale, baseline / font.scale), text);
  painter.restore();
}

} // namespace

void paintWardleyScene(const WardleyScene &scene, QPainter &painter,
                       const MermaidPaintOptions &) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const WardleyPrimitive &primitive : scene.primitives) {
    if (primitive.type == WardleyPrimitiveType::Text) {
      paintText(painter, scene, primitive);
      continue;
    }
    const auto fill = fillPaint(primitive.fill);
    painter.setBrush(fill.none ? Qt::NoBrush : QBrush(fill.color));
    painter.setPen(pen(primitive));
    switch (primitive.type) {
    case WardleyPrimitiveType::Rect:
      if (primitive.rx > 0.0)
        painter.drawRoundedRect(primitive.rect, primitive.rx, primitive.rx,
                                Qt::AbsoluteSize);
      else painter.drawRect(primitive.rect);
      break;
    case WardleyPrimitiveType::Line:
      painter.drawLine(primitive.line);
      if (primitive.markerEnd)
        arrowHead(painter, primitive.line.p2(),
                  primitive.line.p2() - primitive.line.p1(),
                  primitive.markerColor, primitive.markerSize);
      if (primitive.markerStart)
        arrowHead(painter, primitive.line.p1(),
                  primitive.line.p1() - primitive.line.p2(),
                  primitive.markerColor, primitive.markerSize);
      break;
    case WardleyPrimitiveType::Circle:
      painter.drawEllipse(primitive.center, primitive.radius, primitive.radius);
      break;
    case WardleyPrimitiveType::Path:
      painter.drawPath(primitive.path);
      break;
    case WardleyPrimitiveType::Text: break;
    }
  }
  painter.restore();
}

} // namespace muffin::mermaid::wardley
