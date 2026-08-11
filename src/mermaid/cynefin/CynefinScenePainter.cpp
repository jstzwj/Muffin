#include "mermaid/cynefin/CynefinScenePainter.h"

#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::cynefin {
namespace {

color::SvgPaint fillPaint(const QString &value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
}

color::SvgPaint strokePaint(const QString &value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Stroke, Qt::black);
}

QPen pathPen(const QString &value, qreal width, const QVector<qreal> &dash) {
  const auto paint = strokePaint(value);
  if (paint.none || !(width > 0.0)) return Qt::NoPen;
  QPen pen(paint.color, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  if (!dash.isEmpty()) {
    QVector<qreal> scaled;
    scaled.reserve(dash.size());
    for (qreal dashLength : dash) scaled.append(dashLength / width);
    pen.setDashPattern(scaled);
  }
  return pen;
}

void paintPath(QPainter &painter, const CynefinPathGeometry &path) {
  const auto fill = fillPaint(path.fill);
  if (fill.none) painter.setBrush(Qt::NoBrush);
  else {
    QColor color = fill.color;
    color.setAlphaF(color.alphaF() * qBound(0.0, path.fillOpacity, 1.0));
    painter.setBrush(color);
  }
  painter.setPen(pathPen(path.stroke, path.strokeWidth, path.dash));
  painter.drawPath(path.path);
}

void paintRect(QPainter &painter, const CynefinRectGeometry &rect) {
  const auto fill = fillPaint(rect.fill);
  if (fill.none) painter.setBrush(Qt::NoBrush);
  else {
    QColor color = fill.color;
    color.setAlphaF(color.alphaF() * qBound(0.0, rect.fillOpacity, 1.0));
    painter.setBrush(color);
  }
  painter.setPen(pathPen(rect.stroke, rect.strokeWidth, rect.dash));
  if (rect.radius > 0.0)
    painter.drawRoundedRect(rect.rect, rect.radius, rect.radius,
                            Qt::AbsoluteSize);
  else
    painter.drawRect(rect.rect);
}

QString visibleSvgText(QString value) {
  value.replace(QRegularExpression(QStringLiteral(R"([\t\n\r\f ]+)")),
                QStringLiteral(" "));
  while (value.startsWith(QLatin1Char(' '))) value.remove(0, 1);
  while (value.endsWith(QLatin1Char(' '))) value.chop(1);
  return value;
}

QStringList cssFontFamilies(const QString &expression) {
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

void paintText(QPainter &painter, const CynefinScene &scene,
               const CynefinTextGeometry &text) {
  if (text.text.isEmpty() || !(text.fontSize > 0.0)) return;
  const auto fill = color::resolveSvgPaint(text.fill, color::SvgPaintKind::Text,
                                           Qt::black);
  if (fill.none) return;
  const QStringList families = cssFontFamilies(scene.style.fontFamily);
  auto font = editor::makeUnhintedCssPixelFont(families.first(), text.fontSize);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(text.bold ? QFont::Bold : QFont::Normal);
  font.font.setItalic(text.italic);
  const QString visible = visibleSvgText(text.text);
  qreal width = QFontMetricsF(font.font).horizontalAdvance(visible) * font.scale;
  qreal x = std::isfinite(text.position.x()) ? text.position.x() : 0.0;
  if (text.anchor == QLatin1String("middle")) x -= width / 2.0;
  else if (text.anchor == QLatin1String("end")) x -= width;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      scene.style.fontFamily, text.fontSize,
      text.bold ? QFont::Bold : QFont::Normal,
      text.italic ? QFont::StyleItalic : QFont::StyleNormal);
  qreal baseline = std::isfinite(text.position.y()) ? text.position.y() : 0.0;
  if (text.baseline == CynefinTextBaseline::Middle ||
      text.baseline == CynefinTextBaseline::Central)
    baseline += metrics.xHeight / 2.0;
  painter.save();
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

void paintArrowHead(QPainter &painter, const CynefinArrowGeometry &arrow) {
  if (!(arrow.strokeWidth > 0.0)) return;
  const auto fill = fillPaint(arrow.stroke);
  if (fill.none) return;
  const QPointF tangent = arrow.end - arrow.control;
  const qreal angle = std::atan2(tangent.y(), tangent.x());
  const qreal scale = arrow.strokeWidth * 0.6;
  QPolygonF triangle;
  triangle << QPointF(0.6 * scale, 0.0)
           << QPointF(-5.4 * scale, -3.0 * scale)
           << QPointF(-5.4 * scale, 3.0 * scale);
  QTransform transform;
  transform.translate(arrow.end.x(), arrow.end.y());
  transform.rotateRadians(angle);
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill.color);
  painter.drawPolygon(transform.map(triangle));
}

} // namespace

void paintCynefinScene(const CynefinScene &scene, QPainter &painter,
                       const MermaidPaintOptions &) {
  painter.save();
  painter.translate(scene.rootTranslation);
  for (const auto &background : scene.backgrounds)
    paintRect(painter, background);
  for (const auto &boundary : scene.boundaries)
    paintPath(painter, boundary);
  paintPath(painter, scene.confusion);
  for (const auto &label : scene.labels)
    paintText(painter, scene, label);
  for (const auto &subtitle : scene.subtitles)
    paintText(painter, scene, subtitle);
  for (const auto &item : scene.items) {
    painter.save();
    painter.translate(item.translation);
    paintRect(painter, item.rect);
    paintText(painter, scene, item.text);
    painter.restore();
  }
  for (const auto &arrow : scene.arrows) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pathPen(arrow.stroke, arrow.strokeWidth, {}));
    painter.drawPath(arrow.path);
    paintArrowHead(painter, arrow);
    paintText(painter, scene, arrow.label);
  }
  paintText(painter, scene, scene.title);
  painter.restore();
}

} // namespace muffin::mermaid::cynefin
