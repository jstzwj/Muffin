// Native pie painter. See PieScenePainter.h.

#include "mermaid/pie/PieScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::pie {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Point on the pie circle at angle `deg` (oracle convention: deg=-90 is 12
// o'clock, increasing clockwise; point = (r*cos, r*sin) in Qt's y-down coords).
QPointF circlePoint(double r, double deg) {
  const double rad = deg * kPi / 180.0;
  return QPointF(r * std::cos(rad), r * std::sin(rad));
}

// Rebuild a slice's wedge as a QPainterPath (group-local, centered at origin).
// Qt arcTo convention: startAngle = -startDeg, sweepLength = -(endDeg - startDeg)
// (derived from Qt measuring 0deg at 3 o'clock, positive counter-clockwise, with
// the painter's y-down flipping the visual direction to clockwise — matching the
// oracle's clockwise slice sweep).
QPainterPath buildSlicePath(double startDeg, double endDeg, double outerR, double innerR) {
  QPainterPath p;
  const double span = endDeg - startDeg;
  const QRectF outerRect(-outerR, -outerR, 2.0 * outerR, 2.0 * outerR);
  if (span >= 360.0 - 1e-9) {
    // Full circle. Solid -> disk; donut -> annulus (outer minus inner, even-odd).
    p.setFillRule(Qt::OddEvenFill);
    p.addEllipse(outerRect);
    if (innerR > 0.0) p.addEllipse(QRectF(-innerR, -innerR, 2.0 * innerR, 2.0 * innerR));
    return p;
  }
  if (innerR == 0.0) {
    p.moveTo(0.0, 0.0);
    p.arcTo(outerRect, -startDeg, -(endDeg - startDeg));
    p.closeSubpath();
  } else {
    const QRectF innerRect(-innerR, -innerR, 2.0 * innerR, 2.0 * innerR);
    p.moveTo(circlePoint(outerR, startDeg));
    p.arcTo(outerRect, -startDeg, -(endDeg - startDeg));
    p.lineTo(circlePoint(innerR, endDeg));
    p.arcTo(innerRect, -endDeg, endDeg - startDeg);
    p.closeSubpath();
  }
  return p;
}

}  // namespace

// Theme colors arrive as CSS strings (hex / hsl() / rgba() / named / ...). The
// shared color::toQColor parses every notation the way the browser does; an
// EMPTY value returns an invalid QColor so fill call sites paint NoBrush (the
// dark-theme pie12 "no fill attribute" contract). color::toQColor never returns
// invalid for non-empty input (garbage/NaN-hsl -> opaque black, the SVG default).
QColor parsePieColor(const QString& value) {
  return value.trimmed().isEmpty() ? QColor() : color::toQColor(value);
}

void paintPieScene(const PieScene& scene, QPainter& painter,
                   const MermaidPaintOptions& /*options*/) {
  painter.save();
  painter.translate(scene.centerX, scene.centerY);

  // The pie subgroup (outer ring + slices + slice labels) is shifted for the
  // top/left legend positions so the legend block fits beside/above the chart
  // (mermaid's pie-group transform).
  const int n = scene.legends.size();
  const qreal totalLegendHeight = n * scene.legendHeight;
  qreal pieShiftX = 0.0, pieShiftY = 0.0;
  if (scene.legendPosition == QStringLiteral("top"))
    pieShiftY = totalLegendHeight + scene.legendHeight;
  else if (scene.legendPosition == QStringLiteral("left"))
    pieShiftX = scene.longestLegendWidth + scene.legendRectSize + scene.legendSpacing;

  painter.save();
  painter.translate(pieShiftX, pieShiftY);

  // Outer ring (pieOuterCircle: stroke, no fill).
  const QColor outerStroke = parsePieColor(scene.style.outerStrokeColor);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(outerStroke, scene.style.outerStrokeWidth));
  painter.drawEllipse(QPointF(0, 0), scene.outerRingRadius, scene.outerRingRadius);

  // Slices (pieCircle: fill + stroke at pieOpacity). A `highlightSlice=<label>`
  // slice gets the upstream "highlighted" class: scale 1.05 about the chart
  // center + opacity 1. `=hover` (highlightedOnHover) is hover-only upstream —
  // no static paint change.
  painter.setOpacity(scene.style.pieOpacity);
  for (const PieSliceGeometry& s : scene.slices) {
    const bool highlighted = s.className.contains(QStringLiteral("highlighted")) &&
                             !s.className.contains(QStringLiteral("highlightedOnHover"));
    const QColor fill = parsePieColor(s.fill);
    const QPainterPath wedge =
        buildSlicePath(s.startAngleDeg, s.endAngleDeg, s.outerRadius, s.innerRadius);
    painter.setBrush(fill.isValid() ? fill : Qt::NoBrush);
    painter.setPen(QPen(parsePieColor(scene.style.sliceStrokeColor), scene.style.sliceStrokeWidth));
    if (highlighted) {
      painter.save();
      painter.setOpacity(1.0);
      painter.scale(1.05, 1.05);  // about the chart center (origin after translate)
      painter.drawPath(wedge);
      painter.restore();
    } else {
      painter.drawPath(wedge);
    }
  }
  painter.setOpacity(1.0);

  // Percentage labels (slice text), centered at each centroid.
  QFont sliceFont(scene.style.fontFamily, qRound(scene.style.sectionFontSize));
  sliceFont.setPixelSize(qRound(scene.style.sectionFontSize));
  painter.setFont(sliceFont);
  painter.setPen(parsePieColor(scene.style.sectionTextColor));
  for (const PieSliceGeometry& s : scene.slices) {
    painter.drawText(QRectF(s.centroidX - 60, s.centroidY - 30, 120, 60),
                     Qt::AlignCenter, s.percentage);
  }
  painter.restore();  // pie subgroup

  // Title (pieTitleText, in the main group — not shifted), centered at y=-200.
  if (!scene.title.isEmpty()) {
    QFont titleFont(scene.style.fontFamily, qRound(scene.style.titleFontSize));
    titleFont.setPixelSize(qRound(scene.style.titleFontSize));
    painter.setFont(titleFont);
    painter.setPen(parsePieColor(scene.style.titleColor));
    const qreal ty = -(scene.height - 50.0) / 2.0;
    painter.drawText(QRectF(-400.0, ty - 30.0, 800.0, 60.0),
                     Qt::AlignCenter, scene.title);
  }

  // Legend block — position-dependent horizontal/vertical (mermaid draw()).
  if (!scene.legends.isEmpty()) {
    QFont legendFont(scene.style.fontFamily, qRound(scene.style.legendFontSize));
    legendFont.setPixelSize(qRound(scene.style.legendFontSize));
    painter.setFont(legendFont);
    painter.setPen(parsePieColor(scene.style.legendTextColor));
    const QString& pos = scene.legendPosition;
    const qreal rectBlock = scene.legendRectSize + scene.legendSpacing;
    // Upstream's switch default is "right": only left/top/bottom/center are
    // special; every other value (incl. empty/unknown) behaves as right.
    const qreal horizontal = pos == QStringLiteral("left")
                                 ? -scene.radius - rectBlock
                             : (pos == QStringLiteral("top") || pos == QStringLiteral("bottom") ||
                                pos == QStringLiteral("center"))
                                 ? -scene.longestLegendWidth / 2.0 - rectBlock
                                 : 12.0 * scene.legendRectSize;  // right (default)
    const qreal offset = pos == QStringLiteral("top") ? scene.radius
                          : pos == QStringLiteral("bottom") ? -scene.radius - scene.legendHeight
                                                             : scene.legendHeight * n / 2.0;
    for (int i = 0; i < n; ++i) {
      const PieLegendEntry& e = scene.legends.at(i);
      const qreal vertical = i * scene.legendHeight - offset;
      const QColor lc = parsePieColor(e.fill);
      painter.setBrush(lc.isValid() ? lc : Qt::NoBrush);
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(horizontal, vertical, scene.legendRectSize, scene.legendRectSize));
      painter.setPen(parsePieColor(scene.style.legendTextColor));
      painter.drawText(
          QRectF(horizontal + scene.legendRectSize + scene.legendSpacing,
                 vertical - 2.0, 1000.0, scene.legendRectSize + 4.0),
          Qt::AlignLeft | Qt::AlignVCenter, e.text);
    }
  }

  painter.restore();
}

}  // namespace muffin::mermaid::pie
