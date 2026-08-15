// Native pie painter. See PieScenePainter.h.

#include "mermaid/pie/PieScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
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

void drawCenteredBaseline(QPainter& painter, const editor::CssPixelFont& font,
                          const QPointF& anchor, const QString& text) {
  painter.save();
  painter.translate(anchor);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  const qreal advance = QFontMetricsF(font.font).horizontalAdvance(text);
  painter.drawText(QPointF(-advance / 2.0, 0.0), text);
  painter.restore();
}

void drawLeftBaseline(QPainter& painter, const editor::CssPixelFont& font,
                      const QPointF& anchor, const QString& text) {
  painter.save();
  painter.translate(anchor);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.drawText(QPointF(0.0, 0.0), text);
  painter.restore();
}

editor::CssPixelFont textFont(const QString& family, qreal size,
                              const QString& weight) {
  editor::CssPixelFont font = editor::makeCssPixelFont(family, size);
  font.font.setWeight(editor::cssFontWeightToQt(QJsonValue(weight), QFont::Normal));
  return font;
}

color::SvgPaint resolvePieFill(const QString& value, const QColor& inherited) {
  // D3 omits the fill attribute when a palette slot is undefined. SVG then
  // inherits the root fill (dark pie12 -> #ccc); an explicit empty CSS paint is
  // not the same thing as `fill:none` here.
  return value.isEmpty() ? color::SvgPaint{false, inherited}
                         : color::resolveSvgPaint(value, color::SvgPaintKind::Fill, inherited);
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
    if (!scene.style.sliceVisible) break;  // display:none removes all slices
    const bool highlighted = s.className.contains(QStringLiteral("highlighted")) &&
                             !s.className.contains(QStringLiteral("highlightedOnHover"));
    const auto fill = resolvePieFill(s.fill, inherited);
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
  if (scene.style.sectionTextVisible && scene.style.sectionFontSize > 0.0) {
    const auto sectionPaint =
        color::resolveSvgPaint(scene.style.sectionTextColor, color::SvgPaintKind::Text, inherited);
    if (!sectionPaint.none) {
      const editor::CssPixelFont sliceFont = textFont(
          scene.style.sectionFontFamily, scene.style.sectionFontSize,
          scene.style.sectionFontWeight);
      painter.setPen(sectionPaint.color);
      for (const PieSliceGeometry& s : scene.slices)
        drawCenteredBaseline(painter, sliceFont, QPointF(s.centroidX, s.centroidY),
                             s.percentage);
    }
  }
  painter.restore();  // pie subgroup

  // Title (pieTitleText, in the main group — not shifted), centered at y=-200.
  if (!scene.title.isEmpty() && scene.style.titleVisible &&
      scene.style.titleFontSize > 0.0) {
    const auto titlePaint =
        color::resolveSvgPaint(scene.style.titleColor, color::SvgPaintKind::Text, inherited);
    if (!titlePaint.none) {
      const editor::CssPixelFont titleFont = textFont(
          scene.style.titleFontFamily, scene.style.titleFontSize,
          scene.style.titleFontWeight);
      painter.setPen(titlePaint.color);
      const qreal ty = -(scene.height - 50.0) / 2.0;
      // SVG positions this <text> by its baseline at y=-200. QRectF overloads
      // vertically align the glyphs inside the rectangle and shift the ink down
      // by about 10px, so use the point overload to preserve SVG baseline
      // semantics. The same scaled-font metrics drive horizontal centering and
      // the adapter's viewBox expansion; point drawing never clips long text.
      drawCenteredBaseline(painter, titleFont, QPointF(0.0, ty), scene.title);
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
    const bool drawLegendText = scene.style.legendTextVisible &&
                                scene.style.legendFontSize > 0.0 &&
                                !legendTextPaint.none;
    const editor::CssPixelFont legendFont = textFont(
        scene.style.legendFontFamily, scene.style.legendFontSize,
        scene.style.legendFontWeight);
    for (int i = 0; i < n; ++i) {
      const PieLegendEntry& e = scene.legends.at(i);
      const qreal vertical = i * scene.legendHeight - offset;
      const auto lc = resolvePieFill(e.fill, inherited);
      const auto ls = color::resolveSvgPaint(e.fill, color::SvgPaintKind::Stroke, inherited);
      painter.setBrush(lc.none ? Qt::NoBrush : QBrush(lc.color));
      painter.setPen(ls.none ? QPen(Qt::NoPen) : QPen(ls.color, 1.0));
      painter.drawRect(QRectF(horizontal, vertical, scene.legendRectSize, scene.legendRectSize));
      if (drawLegendText) {
        painter.setPen(legendTextPaint.color);
        drawLeftBaseline(painter, legendFont,
                         QPointF(horizontal + scene.legendRectSize + scene.legendSpacing,
                                 vertical + scene.legendRectSize - scene.legendSpacing),
                         e.text);
      }
    }
  }

  painter.restore();
}

}  // namespace muffin::mermaid::pie
