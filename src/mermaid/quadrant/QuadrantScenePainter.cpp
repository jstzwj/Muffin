// Native quadrantChart painter. See QuadrantScenePainter.h.

#include "mermaid/quadrant/QuadrantScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
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

qreal usedSvgFontSize(qreal requested, qreal inherited) {
  if (!std::isfinite(requested) || requested < 0.0) return inherited;
  return std::min<qreal>(requested, 10000.0);
}

void drawScaledText(QPainter& painter, const editor::CssPixelFont& font,
                    const QRectF& rect, Qt::Alignment alignment,
                    const QString& text) {
  if (!(font.scale > 0.0) || text.isEmpty()) return;
  painter.save();
  painter.translate(rect.topLeft());
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.drawText(QRectF(0.0, 0.0, rect.width() / font.scale,
                          rect.height() / font.scale),
                   alignment | Qt::TextDontClip, text);
  painter.restore();
}

editor::CssPixelFont styledFont(const QString& family, qreal size,
                                QFont::Weight weight) {
  editor::CssPixelFont result = editor::makeCssPixelFont(family, size);
  result.font.setWeight(weight);
  return result;
}

}  // namespace

// Theme colors arrive as CSS strings (hex / hsl() / rgba() / named / ...). The
// shared color::toQColor parses every notation the way the browser does. Paint
// values that are none / currentColor / inherit / garbage are resolved per
// property by color::resolveSvgPaint (fill/text -> color or NoBrush/hide;
// stroke -> color or NoPen).
void paintQuadrantScene(const QuadrantScene& scene, QPainter& painter,
                        const MermaidPaintOptions& /*options*/) {
  const bool pointsEmpty = scene.points.isEmpty();
  const QColor inherited = color::toQColor(scene.style.inheritedColor);

  // DOM order is quadrant groups (rect + text), then borders, then data-point
  // groups (circle + text), axis labels, and title. Preserve it because labels
  // and large styled points can overlap later groups.
  for (const QuadrantRect& q : scene.quadrants) {
    const QRectF rect(q.x, q.y, q.width, q.height);
    if (q.shapeVisible && q.shapeOpacity > 0.0) {
      painter.save();
      painter.setOpacity(painter.opacity() * q.shapeOpacity);
      painter.setPen(Qt::NoPen);
      const auto fill = color::resolveSvgPaint(q.fill, color::SvgPaintKind::Fill, inherited);
      painter.setBrush(fill.none ? Qt::NoBrush : QBrush(fill.color));
      painter.drawRect(rect);
      painter.restore();
    }
    const qreal qSize = usedSvgFontSize(q.textFontSize,
                                        scene.style.inheritedFontSize);
    if (!q.textVisible || q.textOpacity <= 0.0 || q.text.isEmpty() || qSize <= 0.0)
      continue;
    const auto tf = color::resolveSvgPaint(q.textFill, color::SvgPaintKind::Text, inherited);
    if (tf.none) continue;
    painter.save();
    painter.setOpacity(painter.opacity() * q.textOpacity);
    painter.setPen(tf.color);
    const editor::CssPixelFont qFont =
        styledFont(q.textFontFamily, qSize, q.textFontWeight);
    const qreal cx = q.x + q.width / 2.0;
    const qreal cy = pointsEmpty ? q.y + q.height / 2.0 : q.y + scene.quadrantTextTopPadding;
    // pointsEmpty => centered (middle baseline); else top (hanging baseline).
    const QRectF box(cx - q.width / 2.0, pointsEmpty ? cy - 30.0 : cy,
                     q.width, pointsEmpty ? 60.0 : 40.0);
    drawScaledText(painter, qFont, box,
                   pointsEmpty ? Qt::AlignCenter : (Qt::AlignHCenter | Qt::AlignTop),
                   q.text);
    painter.restore();
  }

  // Borders. stroke:none/invalid or width<=0 -> NoPen.
  for (const QuadrantBorder& b : scene.borders) {
    if (!b.visible || b.opacity <= 0.0) continue;
    painter.save();
    painter.setOpacity(painter.opacity() * b.opacity);
    const auto sc = color::resolveSvgPaint(b.strokeFill, color::SvgPaintKind::Stroke, inherited);
    if (sc.none || b.strokeWidth <= 0.0)
      painter.setPen(Qt::NoPen);
    else {
      QPen pen(sc.color, b.strokeWidth);
      // SVG line defaults to butt caps; QPen defaults to SquareCap and would
      // extend every border by half its width.
      pen.setCapStyle(Qt::FlatCap);
      painter.setPen(pen);
    }
    painter.drawLine(QPointF(b.x1, b.y1), QPointF(b.x2, b.y2));
    painter.restore();
  }
  for (const QuadrantPointG& p : scene.points) {
    QPen pen(Qt::NoPen);
    if (p.strokeWidth > 0.0) {
      const auto ps = color::resolveSvgPaint(p.stroke, color::SvgPaintKind::Stroke, inherited);
      if (!ps.none) {
        pen = QPen(ps.color, p.strokeWidth);
        pen.setCapStyle(Qt::FlatCap);
      }
    }
    if (p.shapeVisible && p.shapeOpacity > 0.0 && p.radius > 0.0) {
      painter.save();
      painter.setOpacity(painter.opacity() * p.shapeOpacity);
      painter.setPen(pen);
      const auto pfill = color::resolveSvgPaint(p.fill, color::SvgPaintKind::Fill, inherited);
      painter.setBrush(pfill.none ? Qt::NoBrush : QBrush(pfill.color));
      painter.drawEllipse(QPointF(p.x, p.y), p.radius, p.radius);
      painter.restore();
    }
    const qreal pSize = usedSvgFontSize(p.textFontSize,
                                        scene.style.inheritedFontSize);
    const auto ptText = color::resolveSvgPaint(
        p.textFill, color::SvgPaintKind::Text, inherited);
    if (p.textVisible && p.textOpacity > 0.0 && pSize > 0.0 &&
        !ptText.none && !p.text.isEmpty()) {
      painter.save();
      painter.setOpacity(painter.opacity() * p.textOpacity);
      painter.setPen(ptText.color);
      const editor::CssPixelFont pFont =
          styledFont(p.textFontFamily, pSize, p.textFontWeight);
      drawScaledText(painter, pFont,
                     QRectF(p.x - 200.0, p.y + scene.pointTextPadding, 400.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, p.text);
      painter.restore();
    }
  }

  // Axis labels use their own font-size and upstream text-anchor. The old
  // fixed QRect(-400,...)+AlignLeft placed start-anchored labels 400px outside
  // the chart and also used the X-axis font for Y-axis text.
  for (const QuadrantAxisLabel& a : scene.axisLabels) {
    if (!a.visible || a.opacity <= 0.0 || a.text.isEmpty()) continue;
    const auto af = color::resolveSvgPaint(a.fill, color::SvgPaintKind::Text, inherited);
    if (af.none) continue;
    painter.setPen(af.color);
    painter.save();
    painter.setOpacity(painter.opacity() * a.opacity);
    painter.translate(a.x, a.y);
    painter.rotate(a.rotation);
    const qreal aSize = usedSvgFontSize(a.fontSize, scene.style.inheritedFontSize);
    const editor::CssPixelFont aFont =
        styledFont(a.fontFamily, aSize, a.fontWeight);
    const qreal width = std::max<qreal>(aFont.horizontalAdvance(a.text) + 4.0, 4.0);
    drawScaledText(painter, aFont,
                   QRectF(a.centered ? -width / 2.0 : 0.0, 0.0, width, aSize + 8.0),
                   Qt::AlignLeft | Qt::AlignTop, a.text);
    painter.restore();
  }

  // Title.
  if (scene.titleVisible && scene.titleOpacity > 0.0 &&
      !scene.titleText.isEmpty()) {
    const qreal tSize = usedSvgFontSize(scene.titleFontSizeCfg,
                                        scene.style.inheritedFontSize);
    const auto tf =
        color::resolveSvgPaint(scene.titleFill, color::SvgPaintKind::Text, inherited);
    if (!tf.none && tSize > 0.0) {
      painter.save();
      painter.setOpacity(painter.opacity() * scene.titleOpacity);
      painter.setPen(tf.color);
      const editor::CssPixelFont tFont =
          styledFont(scene.titleFontFamily, tSize, scene.titleFontWeight);
      const qreal width = std::max<qreal>(tFont.horizontalAdvance(scene.titleText) + 4.0, 4.0);
      drawScaledText(painter, tFont,
                     QRectF(scene.titleX - width / 2.0, scene.titleY,
                            width, tSize + 8.0),
                     Qt::AlignLeft | Qt::AlignTop, scene.titleText);
      painter.restore();
    }
  }
}

}  // namespace muffin::mermaid::quadrant
