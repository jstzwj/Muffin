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
// shared color::toQColor parses every notation the way the browser does. Paint
// values that are none / currentColor / inherit / garbage are resolved per
// property by color::resolveSvgPaint (fill/text -> color or NoBrush/hide;
// stroke -> color or NoPen).
void paintPieScene(const PieScene& scene, QPainter& painter,
                   const MermaidPaintOptions& /*options*/) {
  const QColor inherited = color::toQColor(scene.style.inheritedColor);
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

  // Outer ring (pieOuterCircle: stroke, no fill). stroke-width:0 / stroke:none /
  // invalid stroke -> NoPen (SVG draws nothing); a negative/invalid width also
  // resolved to NoPen via cssStrokeWidthPx returning 0 only for an explicit 0.
  const auto outerStroke =
      color::resolveSvgPaint(scene.style.outerStrokeColor, color::SvgPaintKind::Stroke, inherited);
  painter.setBrush(Qt::NoBrush);
  if (outerStroke.none || scene.style.outerStrokeWidth <= 0.0)
    painter.setPen(Qt::NoPen);
  else
    painter.setPen(QPen(outerStroke.color, scene.style.outerStrokeWidth));
  painter.drawEllipse(QPointF(0, 0), scene.outerRingRadius, scene.outerRingRadius);

  // Slices (pieCircle: fill + stroke at pieOpacity). A `highlightSlice=<label>`
  // slice gets the upstream "highlighted" class: scale 1.05 about the chart
  // center + opacity 1. `=hover` (highlightedOnHover) is hover-only upstream —
  // no static paint change.
  const auto sliceStroke =
      color::resolveSvgPaint(scene.style.sliceStrokeColor, color::SvgPaintKind::Stroke, inherited);
  const bool sliceNoPen = sliceStroke.none || scene.style.sliceStrokeWidth <= 0.0;
  painter.setOpacity(scene.style.pieOpacity);
  for (const PieSliceGeometry& s : scene.slices) {
    const bool highlighted = s.className.contains(QStringLiteral("highlighted")) &&
                             !s.className.contains(QStringLiteral("highlightedOnHover"));
    const auto fill =
        color::resolveSvgPaint(s.fill, color::SvgPaintKind::Fill, inherited);
    const QPainterPath wedge =
        buildSlicePath(s.startAngleDeg, s.endAngleDeg, s.outerRadius, s.innerRadius);
    painter.setBrush(fill.none ? Qt::NoBrush : QBrush(fill.color));
    if (sliceNoPen)
      painter.setPen(Qt::NoPen);
    else
      painter.setPen(QPen(sliceStroke.color, scene.style.sliceStrokeWidth));
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

  // Percentage labels (slice text), centered at each centroid. font-size:0 -> no
  // text; a none/invalid text fill hides the text too.
  if (scene.style.sectionFontSize > 0.0) {
    const auto sectionPaint =
        color::resolveSvgPaint(scene.style.sectionTextColor, color::SvgPaintKind::Text, inherited);
    if (!sectionPaint.none) {
      QFont sliceFont(scene.style.fontFamily, qRound(scene.style.sectionFontSize));
      sliceFont.setPixelSize(qRound(scene.style.sectionFontSize));
      painter.setFont(sliceFont);
      painter.setPen(sectionPaint.color);
      for (const PieSliceGeometry& s : scene.slices) {
        painter.drawText(QRectF(s.centroidX - 60, s.centroidY - 30, 120, 60),
                         Qt::AlignCenter, s.percentage);
      }
    }
  }
  painter.restore();  // pie subgroup

  // Title (pieTitleText, in the main group — not shifted), centered at y=-200.
  if (!scene.title.isEmpty() && scene.style.titleFontSize > 0.0) {
    const auto titlePaint =
        color::resolveSvgPaint(scene.style.titleColor, color::SvgPaintKind::Text, inherited);
    if (!titlePaint.none) {
      QFont titleFont(scene.style.fontFamily, qRound(scene.style.titleFontSize));
      titleFont.setPixelSize(qRound(scene.style.titleFontSize));
      painter.setFont(titleFont);
      painter.setPen(titlePaint.color);
      const qreal ty = -(scene.height - 50.0) / 2.0;
      // Size the rect to the measured title width (scene.titleWidth, set by the
      // adapter) so a super-long title is NOT clipped to the old fixed 800px --
      // an SVG <text> is never clipped. TextDontClip is a safety net for any
      // sub-pixel drift between QFontMetrics advance and drawText's own metrics.
      const qreal tw = scene.titleWidth;
      painter.drawText(QRectF(-tw / 2.0 - 2.0, ty - 30.0, tw + 4.0, 60.0),
                       Qt::AlignCenter | Qt::TextDontClip, scene.title);
    }
  }

  // Legend block — position-dependent horizontal/vertical (mermaid draw()).
  // Rects (color swatches) always draw; the text draws only when the legend
  // font-size > 0 and the text fill is not none/hidden.
  if (!scene.legends.isEmpty()) {
    const QString& pos = scene.legendPosition;
    const qreal rectBlock = scene.legendRectSize + scene.legendSpacing;
    const qreal horizontal = pos == QStringLiteral("left")
                                 ? -scene.radius - rectBlock
                             : (pos == QStringLiteral("top") || pos == QStringLiteral("bottom") ||
                                pos == QStringLiteral("center"))
                                 ? -scene.longestLegendWidth / 2.0 - rectBlock
                                 : 12.0 * scene.legendRectSize;  // right (default)
    const qreal offset = pos == QStringLiteral("top") ? scene.radius
                          : pos == QStringLiteral("bottom") ? -scene.radius - scene.legendHeight
                                                             : scene.legendHeight * n / 2.0;
    const auto legendTextPaint =
        color::resolveSvgPaint(scene.style.legendTextColor, color::SvgPaintKind::Text, inherited);
    const bool drawLegendText = scene.style.legendFontSize > 0.0 && !legendTextPaint.none;
    if (drawLegendText) {
      QFont legendFont(scene.style.fontFamily, qRound(scene.style.legendFontSize));
      legendFont.setPixelSize(qRound(scene.style.legendFontSize));
      painter.setFont(legendFont);
    }
    for (int i = 0; i < n; ++i) {
      const PieLegendEntry& e = scene.legends.at(i);
      const qreal vertical = i * scene.legendHeight - offset;
      const auto lc = color::resolveSvgPaint(e.fill, color::SvgPaintKind::Fill, inherited);
      painter.setBrush(lc.none ? Qt::NoBrush : QBrush(lc.color));
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(horizontal, vertical, scene.legendRectSize, scene.legendRectSize));
      if (drawLegendText) {
        painter.setPen(legendTextPaint.color);
        painter.drawText(
            QRectF(horizontal + scene.legendRectSize + scene.legendSpacing,
                   vertical - 2.0, 1000.0, scene.legendRectSize + 4.0),
            Qt::AlignLeft | Qt::AlignVCenter, e.text);
      }
    }
  }

  painter.restore();
}

}  // namespace muffin::mermaid::pie
