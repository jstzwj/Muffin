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

namespace {
}  // namespace

// Theme colors arrive as CSS strings (hex / hsl() / rgba() / named / ...). The
// shared color::toQColor parses every notation the way the browser does, and
// yields opaque black for the upstream-invalid quadrantPointFill hsl(...,NaN%)
// (the SVG default) -- matching the prior file-local NaN trap via one path.
QColor parseQuadrantColor(const QString& value) { return color::toQColor(value); }

void paintQuadrantScene(const QuadrantScene& scene, QPainter& painter,
                        const MermaidPaintOptions& /*options*/) {
  const bool pointsEmpty = scene.points.isEmpty();

  // Quadrant rects + labels.
  for (const QuadrantRect& q : scene.quadrants) {
    const QRectF rect(q.x, q.y, q.width, q.height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(parseQuadrantColor(q.fill));
    painter.drawRect(rect);
  }

  // Borders.
  for (const QuadrantBorder& b : scene.borders) {
    // b.strokeFill is a theme color (often hsl() for default/base/forest/...);
    // parseQuadrantColor parses it instead of QColor(QString), which cannot.
    painter.setPen(QPen(parseQuadrantColor(b.strokeFill), b.strokeWidth));
    painter.drawLine(QPointF(b.x1, b.y1), QPointF(b.x2, b.y2));
  }

  // Quadrant label text.
  QFont qFont(scene.style.fontFamily);
  qFont.setPixelSize(qRound(scene.quadrantLabelFontSize));
  painter.setFont(qFont);
  for (const QuadrantRect& q : scene.quadrants) {
    if (q.text.isEmpty()) continue;
    painter.setPen(parseQuadrantColor(q.textFill));
    const qreal cx = q.x + q.width / 2.0;
    const qreal cy = pointsEmpty ? q.y + q.height / 2.0 : q.y + scene.quadrantTextTopPadding;
    // pointsEmpty => centered (middle baseline); else top (hanging baseline).
    const QRectF box(cx - q.width / 2.0, pointsEmpty ? cy - 30.0 : cy,
                     q.width, pointsEmpty ? 60.0 : 40.0);
    painter.drawText(box, pointsEmpty ? Qt::AlignCenter : (Qt::AlignHCenter | Qt::AlignTop), q.text);
  }

  // Data points (circles + labels). Fill falls back to black for the invalid
  // upstream hsl(NaN). A classDef stroke-width > 0 draws the stroke (its color,
  // or black if the stroke value is also the invalid hsl).
  for (const QuadrantPointG& p : scene.points) {
    double sw = 0.0;
    if (p.strokeWidth.endsWith(QLatin1String("px")))
      sw = p.strokeWidth.left(p.strokeWidth.size() - 2).toDouble();
    QPen pen(Qt::NoPen);
    if (sw > 0.0) {
      pen = QPen(parseQuadrantColor(p.stroke), sw);
    }
    painter.setPen(pen);
    painter.setBrush(parseQuadrantColor(p.fill));
    painter.drawEllipse(QPointF(p.x, p.y), p.radius, p.radius);
  }
  QFont pFont(scene.style.fontFamily);
  pFont.setPixelSize(qRound(scene.pointLabelFontSize));
  painter.setFont(pFont);
  painter.setPen(parseQuadrantColor(scene.style.quadrantPointTextFill));
  for (const QuadrantPointG& p : scene.points) {
    if (p.text.isEmpty()) continue;
    painter.drawText(QRectF(p.x - 200.0, p.y + scene.pointTextPadding, 400.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, p.text);
  }

  // Axis labels (rotated -90 for the y-axis).
  QFont aFont(scene.style.fontFamily);
  aFont.setPixelSize(qRound(scene.xAxisLabelFontSize));
  painter.setFont(aFont);
  for (const QuadrantAxisLabel& a : scene.axisLabels) {
    if (a.text.isEmpty()) continue;
    painter.setPen(parseQuadrantColor(a.fill));
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
    painter.setPen(parseQuadrantColor(scene.style.quadrantTitleFill));
    painter.drawText(QRectF(scene.titleX - 400.0, scene.titleY, 800.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, scene.titleText);
  }
}

}  // namespace muffin::mermaid::quadrant
