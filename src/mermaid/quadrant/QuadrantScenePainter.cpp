// Native quadrantChart painter. See QuadrantScenePainter.h.

#include "mermaid/quadrant/QuadrantScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::quadrant {

// Theme colors arrive as CSS strings (hex / hsl() / rgba() / named / ...). The
// shared color::toQColor parses every notation the way the browser does. Paint
// values that are none / currentColor / inherit / garbage are resolved per
// property by color::resolveSvgPaint (fill/text -> color or NoBrush/hide;
// stroke -> color or NoPen).
void paintQuadrantScene(const QuadrantScene& scene, QPainter& painter,
                        const MermaidPaintOptions& /*options*/) {
  const bool pointsEmpty = scene.points.isEmpty();
  const QColor inherited = color::toQColor(scene.style.inheritedColor);

  // Quadrant rects + labels.
  for (const QuadrantRect& q : scene.quadrants) {
    const QRectF rect(q.x, q.y, q.width, q.height);
    painter.setPen(Qt::NoPen);
    const auto fill = color::resolveSvgPaint(q.fill, color::SvgPaintKind::Fill, inherited);
    painter.setBrush(fill.none ? Qt::NoBrush : QBrush(fill.color));
    painter.drawRect(rect);
  }

  // Borders. stroke:none/invalid or width<=0 -> NoPen.
  for (const QuadrantBorder& b : scene.borders) {
    const auto sc = color::resolveSvgPaint(b.strokeFill, color::SvgPaintKind::Stroke, inherited);
    if (sc.none || b.strokeWidth <= 0.0)
      painter.setPen(Qt::NoPen);
    else
      painter.setPen(QPen(sc.color, b.strokeWidth));
    painter.drawLine(QPointF(b.x1, b.y1), QPointF(b.x2, b.y2));
  }

  // Quadrant label text. A none/invalid text fill hides the text.
  QFont qFont(scene.style.fontFamily);
  qFont.setPixelSize(qRound(scene.quadrantLabelFontSize));
  painter.setFont(qFont);
  for (const QuadrantRect& q : scene.quadrants) {
    if (q.text.isEmpty()) continue;
    const auto tf = color::resolveSvgPaint(q.textFill, color::SvgPaintKind::Text, inherited);
    if (tf.none) continue;
    painter.setPen(tf.color);
    const qreal cx = q.x + q.width / 2.0;
    const qreal cy = pointsEmpty ? q.y + q.height / 2.0 : q.y + scene.quadrantTextTopPadding;
    // pointsEmpty => centered (middle baseline); else top (hanging baseline).
    const QRectF box(cx - q.width / 2.0, pointsEmpty ? cy - 30.0 : cy,
                     q.width, pointsEmpty ? 60.0 : 40.0);
    painter.drawText(box, pointsEmpty ? Qt::AlignCenter : (Qt::AlignHCenter | Qt::AlignTop), q.text);
  }

  // Data points (circles + labels). Fill falls back to black for the invalid
  // upstream hsl(NaN). A classDef stroke-width > 0 draws the stroke (its color,
  // or none if the stroke value is none/invalid).
  for (const QuadrantPointG& p : scene.points) {
    double sw = 0.0;
    if (p.strokeWidth.endsWith(QLatin1String("px")))
      sw = p.strokeWidth.left(p.strokeWidth.size() - 2).toDouble();
    QPen pen(Qt::NoPen);
    if (sw > 0.0) {
      const auto ps = color::resolveSvgPaint(p.stroke, color::SvgPaintKind::Stroke, inherited);
      if (!ps.none) pen = QPen(ps.color, sw);
    }
    painter.setPen(pen);
    const auto pfill = color::resolveSvgPaint(p.fill, color::SvgPaintKind::Fill, inherited);
    painter.setBrush(pfill.none ? Qt::NoBrush : QBrush(pfill.color));
    painter.drawEllipse(QPointF(p.x, p.y), p.radius, p.radius);
  }
  QFont pFont(scene.style.fontFamily);
  pFont.setPixelSize(qRound(scene.pointLabelFontSize));
  painter.setFont(pFont);
  const auto ptText =
      color::resolveSvgPaint(scene.style.quadrantPointTextFill, color::SvgPaintKind::Text, inherited);
  if (!ptText.none) {
    painter.setPen(ptText.color);
    for (const QuadrantPointG& p : scene.points) {
      if (p.text.isEmpty()) continue;
      painter.drawText(QRectF(p.x - 200.0, p.y + scene.pointTextPadding, 400.0, 40.0),
                       Qt::AlignHCenter | Qt::AlignTop, p.text);
    }
  }

  // Axis labels (rotated -90 for the y-axis).
  QFont aFont(scene.style.fontFamily);
  aFont.setPixelSize(qRound(scene.xAxisLabelFontSize));
  painter.setFont(aFont);
  for (const QuadrantAxisLabel& a : scene.axisLabels) {
    if (a.text.isEmpty()) continue;
    const auto af = color::resolveSvgPaint(a.fill, color::SvgPaintKind::Text, inherited);
    if (af.none) continue;
    painter.setPen(af.color);
    painter.save();
    painter.translate(a.x, a.y);
    painter.rotate(a.rotation);
    painter.drawText(QRectF(-400.0, -20.0, 800.0, 40.0),
                     Qt::AlignVCenter | Qt::AlignLeft, a.text);
    painter.restore();
  }

  // Title.
  if (!scene.titleText.isEmpty()) {
    QFont tFont(scene.style.fontFamily);
    tFont.setPixelSize(qRound(scene.titleFontSizeCfg));
    painter.setFont(tFont);
    const auto tf =
        color::resolveSvgPaint(scene.style.quadrantTitleFill, color::SvgPaintKind::Text, inherited);
    if (!tf.none) {
      painter.setPen(tf.color);
      painter.drawText(QRectF(scene.titleX - 400.0, scene.titleY, 800.0, 40.0),
                       Qt::AlignHCenter | Qt::AlignTop, scene.titleText);
    }
  }
}

}  // namespace muffin::mermaid::quadrant
