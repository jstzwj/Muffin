#include "mermaid/radar/RadarScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::radar {
namespace {

struct RootFill {
  bool none = false;
  bool currentColor = false;
  QColor color = Qt::black;
};

RootFill rootFill(const RadarScene& scene) {
  const QString value = scene.style.textColor.trimmed();
  if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return {.none = true};
  if (value.compare(QLatin1String("currentColor"), Qt::CaseInsensitive) == 0)
    return {.currentColor = true};
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, QColor(Qt::black));
  return paint.none ? RootFill{.none = true} : RootFill{.color = paint.color};
}

QColor cssColor(const QString& value) {
  return color::isParsableColor(value) ? color::toQColor(value)
                                        : QColor(Qt::black);
}

QColor withOpacity(QColor value, qreal opacity) {
  if (!std::isfinite(opacity)) opacity = 1.0;
  value.setAlphaF(std::clamp(value.alphaF() * opacity, 0.0, 1.0));
  return value;
}

QPen strokePen(const QString& value, qreal width, const QColor& inherited) {
  const color::SvgPaint stroke =
      color::resolveSvgPaint(value, color::SvgPaintKind::Stroke, inherited);
  if (stroke.none || !(width > 0.0) || !std::isfinite(width)) return QPen(Qt::NoPen);
  QPen pen(stroke.color, width);
  // SVG line/path defaults are butt caps with miter joins. QPen defaults to a
  // square cap, which visibly extends radar spokes and curve endpoints.
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  pen.setMiterLimit(4.0);
  return pen;
}

QBrush inheritedFillBrush(const RootFill& root, const QString& elementColor,
                          qreal opacity) {
  if (root.none) return QBrush(Qt::NoBrush);
  const QColor value = root.currentColor ? cssColor(elementColor) : root.color;
  return QBrush(withOpacity(value, opacity));
}

QBrush fillBrush(const QString& raw, qreal opacity, const RootFill& root,
                 const QString& elementColor) {
  const QString value = raw.trimmed();
  if (value.isEmpty() ||
      value.compare(QLatin1String("inherit"), Qt::CaseInsensitive) == 0 ||
      value.compare(QLatin1String("unset"), Qt::CaseInsensitive) == 0 ||
      value.compare(QLatin1String("revert"), Qt::CaseInsensitive) == 0 ||
      value.compare(QLatin1String("revert-layer"), Qt::CaseInsensitive) == 0)
    return inheritedFillBrush(root, elementColor, opacity);
  if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return QBrush(Qt::NoBrush);
  if (value.compare(QLatin1String("currentColor"), Qt::CaseInsensitive) == 0)
    return QBrush(withOpacity(cssColor(elementColor), opacity));
  if (value.compare(QLatin1String("initial"), Qt::CaseInsensitive) == 0)
    return QBrush(withOpacity(QColor(Qt::black), opacity));
  if (!color::isParsableColor(value))
    return inheritedFillBrush(root, elementColor, opacity);
  return QBrush(withOpacity(color::toQColor(value), opacity));
}

bool finitePoint(const QPointF& point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool finiteCurve(const RadarCurveGeometry& curve) {
  for (const QPointF& point : curve.points)
    if (!finitePoint(point)) return false;
  for (const RadarCubicSegment& segment : curve.cubics)
    if (!finitePoint(segment.control1) || !finitePoint(segment.control2) ||
        !finitePoint(segment.end))
      return false;
  return true;
}

QPainterPath polygonPath(const QVector<QPointF>& points) {
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  if (points.isEmpty()) return path;
  path.moveTo(points.front());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  path.closeSubpath();
  return path;
}

QPainterPath curvePath(const RadarCurveGeometry& curve) {
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  if (curve.points.isEmpty()) return path;
  path.moveTo(curve.points.front());
  for (const RadarCubicSegment& segment : curve.cubics)
    path.cubicTo(segment.control1, segment.control2, segment.end);
  path.closeSubpath();
  return path;
}

void drawAnchoredText(QPainter& painter, const QString& family, qreal pixelSize,
                      const QPointF& anchor, const QString& text,
                      RadarTextAnchor horizontal, RadarBaseline vertical,
                      const QColor& value,
                      QFont::Weight weight = QFont::Normal) {
  static const QRegularExpression collapsibleWhitespace(
      QStringLiteral(R"([\t\n\r\f ]+)"));
  QString visibleText = text;
  visibleText.replace(collapsibleWhitespace, QStringLiteral(" "));
  while (visibleText.startsWith(QLatin1Char(' '))) visibleText.remove(0, 1);
  while (visibleText.endsWith(QLatin1Char(' '))) visibleText.chop(1);
  if (visibleText.isEmpty() || !(pixelSize > 0.0) || !std::isfinite(pixelSize) ||
      !finitePoint(anchor))
    return;
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(family, pixelSize);
  font.font.setWeight(weight);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const qreal advance = metrics.horizontalAdvance(visibleText);
  qreal x = anchor.x();
  if (horizontal == RadarTextAnchor::Middle) x -= advance * font.scale / 2.0;
  else if (horizontal == RadarTextAnchor::End) x -= advance * font.scale;
  qreal baseline = anchor.y();
  if (vertical == RadarBaseline::Central)
    baseline += (metrics.ascent() - metrics.descent()) * font.scale / 2.0;
  else if (vertical == RadarBaseline::Hanging)
    baseline += metrics.ascent() * font.scale;
  painter.save();
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(value);
  painter.drawText(QPointF(0.0, 0.0), visibleText);
  painter.restore();
}

}  // namespace

void paintRadarScene(const RadarScene& scene, QPainter& painter,
                     const MermaidPaintOptions&) {
  if (!finitePoint(scene.center)) return;
  const RootFill root = rootFill(scene);
  const QColor inherited = root.none ? QColor(Qt::black) : root.color;
  painter.save();
  painter.translate(scene.center);

  // Upstream DOM order is graticules, axes (line+label), curves, legend, title.
  for (const RadarGraticuleGeometry& ring : scene.graticules) {
    if (!ring.visible || !std::isfinite(ring.radius)) continue;
    painter.setPen(strokePen(ring.stroke, ring.strokeWidth, inherited));
    painter.setBrush(fillBrush(ring.fill, ring.fillOpacity, root, ring.color));
    if (ring.circle) {
      if (ring.radius >= 0.0)
        painter.drawEllipse(QPointF(), ring.radius, ring.radius);
    } else if (std::all_of(ring.points.cbegin(), ring.points.cend(), finitePoint)) {
      painter.drawPath(polygonPath(ring.points));
    }
  }

  for (const RadarAxisGeometry& axis : scene.axes) {
    const QPen axisPen = strokePen(axis.lineStroke,
                                   axis.lineStrokeWidth, inherited);
    if (axis.lineVisible && axis.lineOpacity > 0.0 &&
        finitePoint(axis.end) && axisPen.style() != Qt::NoPen) {
      painter.save();
      painter.setOpacity(painter.opacity() * axis.lineOpacity);
      painter.setPen(axisPen);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(QPointF(), axis.end);
      painter.restore();
    }
    const QBrush axisText = fillBrush(axis.labelFill, axis.labelOpacity, root,
                                      axis.labelColor);
    if (axis.labelVisible && axisText.style() != Qt::NoBrush)
      drawAnchoredText(painter, axis.labelFontFamily,
                       axis.labelFontSize, axis.labelPosition,
                       axis.label, axis.textAnchor, axis.baseline,
                       axisText.color(), axis.labelFontWeight);
  }

  for (const RadarCurveGeometry& curve : scene.curves) {
    if (!finiteCurve(curve) || curve.points.isEmpty()) continue;
    if (!curve.visible) continue;
    painter.setBrush(fillBrush(curve.fill, curve.fillOpacity, root,
                               curve.elementColor));
    QPen curvePen = strokePen(curve.stroke, curve.strokeWidth, inherited);
    if (curvePen.style() != Qt::NoPen)
      curvePen.setColor(withOpacity(curvePen.color(), curve.strokeOpacity));
    painter.setPen(curvePen);
    painter.drawPath(curve.polygon ? polygonPath(curve.points) : curvePath(curve));
  }

  for (const RadarLegendGeometry& legend : scene.legends) {
    if (!finitePoint(legend.position)) continue;
    painter.save();
    painter.translate(legend.position);
    painter.setBrush(fillBrush(legend.boxFill, legend.boxFillOpacity, root,
                               legend.boxColor));
    QPen boxPen = strokePen(legend.boxStroke, legend.boxStrokeWidth, inherited);
    if (boxPen.style() != Qt::NoPen)
      boxPen.setColor(withOpacity(boxPen.color(), legend.boxStrokeOpacity));
    painter.setPen(boxPen);
    // legendBoxSize is dead upstream; renderer hard-codes 12 and x=16.
    if (legend.boxVisible) painter.drawRect(QRectF(0.0, 0.0, 12.0, 12.0));
    painter.restore();
    const QBrush legendText = fillBrush(legend.textFill, legend.textOpacity,
                                        root, legend.textColor);
    if (legend.textVisible && legendText.style() != Qt::NoBrush)
      drawAnchoredText(painter, legend.textFontFamily,
                       legend.textFontSize,
                       legend.position + QPointF(16.0, 0.0), legend.text,
                       RadarTextAnchor::Start, RadarBaseline::Hanging,
                       legendText.color(), legend.textFontWeight);
  }

  const QBrush titleText = fillBrush(scene.titleFill, scene.titleOpacity,
                                     root, scene.titleColor);
  if (scene.titleVisible && titleText.style() != Qt::NoBrush)
    drawAnchoredText(painter, scene.titleFontFamily,
                     scene.titleFontSize,
                     QPointF(0.0, -scene.config.height / 2.0 -
                                      scene.config.marginTop),
                     scene.title, RadarTextAnchor::Middle,
                     RadarBaseline::Hanging, titleText.color(),
                     scene.titleFontWeight);
  painter.restore();
}

}  // namespace muffin::mermaid::radar
