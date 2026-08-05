// Native quadrantChart painter. See QuadrantScenePainter.h.

#include "mermaid/quadrant/QuadrantScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/quadrant/QuadrantScene.h"

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

QColor parseColorImpl(const QString& value) {
  const QString s = value.trimmed();
  if (s.isEmpty()) return QColor();
  if (s.startsWith(QStringLiteral("hsl(")) && s.endsWith(QLatin1Char(')'))) {
    const QStringList parts = s.mid(4, s.size() - 5).split(QLatin1Char(','));
    if (parts.size() >= 3) {
      const double h = parts[0].trimmed().toDouble();
      const double sa = parts[1].trimmed().remove(QLatin1Char('%')).toDouble();
      const double la = parts[2].trimmed().remove(QLatin1Char('%')).toDouble();
      if (std::isnan(la) || std::isinf(la)) return QColor();  // upstream NaN trap
      return QColor::fromHslF(h / 360.0, sa / 100.0, la / 100.0);
    }
    return QColor();
  }
  return QColor(s);
}

// Resolve a fill that may be the upstream-invalid hsl(NaN) to the SVG default
// (black) so points render as mermaid renders them in the browser.
QColor resolveFill(const QString& value) {
  QColor c = parseColorImpl(value);
  return c.isValid() ? c : Qt::black;
}

}  // namespace

QColor parseQuadrantColor(const QString& value) { return parseColorImpl(value); }

void paintQuadrantScene(const QuadrantScene& scene, QPainter& painter,
                        const MermaidPaintOptions& /*options*/) {
  const bool pointsEmpty = scene.points.isEmpty();

  // Quadrant rects + labels.
  for (const QuadrantRect& q : scene.quadrants) {
    const QRectF rect(q.x, q.y, q.width, q.height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(resolveFill(q.fill));
    painter.drawRect(rect);
  }

  // Borders.
  for (const QuadrantBorder& b : scene.borders) {
    QColor sc(b.strokeFill);
    if (!sc.isValid()) sc = QColor(scene.style.quadrantExternalBorderStrokeFill);
    painter.setPen(QPen(sc, b.strokeWidth));
    painter.drawLine(QPointF(b.x1, b.y1), QPointF(b.x2, b.y2));
  }

  // Quadrant label text.
  QFont qFont(scene.style.fontFamily);
  qFont.setPixelSize(16);
  painter.setFont(qFont);
  for (const QuadrantRect& q : scene.quadrants) {
    if (q.text.isEmpty()) continue;
    painter.setPen(QColor(q.textFill));
    const qreal cx = q.x + q.width / 2.0;
    const qreal cy = pointsEmpty ? q.y + q.height / 2.0 : q.y + 5.0;
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
      QColor sc = parseColorImpl(p.stroke);
      if (!sc.isValid()) sc = Qt::black;
      pen = QPen(sc, sw);
    }
    painter.setPen(pen);
    painter.setBrush(resolveFill(p.fill));
    painter.drawEllipse(QPointF(p.x, p.y), p.radius, p.radius);
  }
  QFont pFont(scene.style.fontFamily);
  pFont.setPixelSize(12);
  painter.setFont(pFont);
  painter.setPen(QColor(scene.style.quadrantPointTextFill));
  for (const QuadrantPointG& p : scene.points) {
    if (p.text.isEmpty()) continue;
    painter.drawText(QRectF(p.x - 200.0, p.y + 5.0, 400.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, p.text);
  }

  // Axis labels (rotated -90 for the y-axis).
  QFont aFont(scene.style.fontFamily);
  aFont.setPixelSize(16);
  painter.setFont(aFont);
  for (const QuadrantAxisLabel& a : scene.axisLabels) {
    if (a.text.isEmpty()) continue;
    painter.setPen(QColor(a.fill));
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
    tFont.setPixelSize(20);
    painter.setFont(tFont);
    painter.setPen(QColor(scene.style.quadrantTitleFill));
    painter.drawText(QRectF(scene.titleX - 400.0, scene.titleY, 800.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, scene.titleText);
  }
}

}  // namespace muffin::mermaid::quadrant
