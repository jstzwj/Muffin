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

  const qreal qSize = usedSvgFontSize(scene.quadrantLabelFontSize,
                                      scene.style.inheritedFontSize);
  const editor::CssPixelFont qFont =
      editor::makeCssPixelFont(scene.style.fontFamily, qSize);

  // DOM order is quadrant groups (rect + text), then borders, then data-point
  // groups (circle + text), axis labels, and title. Preserve it because labels
  // and large styled points can overlap later groups.
  for (const QuadrantRect& q : scene.quadrants) {
    const QRectF rect(q.x, q.y, q.width, q.height);
    painter.setPen(Qt::NoPen);
    const auto fill = color::resolveSvgPaint(q.fill, color::SvgPaintKind::Fill, inherited);
    painter.setBrush(fill.none ? Qt::NoBrush : QBrush(fill.color));
    painter.drawRect(rect);
    if (q.text.isEmpty() || qSize <= 0.0) continue;
    const auto tf = color::resolveSvgPaint(q.textFill, color::SvgPaintKind::Text, inherited);
    if (tf.none) continue;
    painter.setPen(tf.color);
    const qreal cx = q.x + q.width / 2.0;
    const qreal cy = pointsEmpty ? q.y + q.height / 2.0 : q.y + scene.quadrantTextTopPadding;
    // pointsEmpty => centered (middle baseline); else top (hanging baseline).
    const QRectF box(cx - q.width / 2.0, pointsEmpty ? cy - 30.0 : cy,
                     q.width, pointsEmpty ? 60.0 : 40.0);
    drawScaledText(painter, qFont, box,
                   pointsEmpty ? Qt::AlignCenter : (Qt::AlignHCenter | Qt::AlignTop),
                   q.text);
  }

  // Borders. stroke:none/invalid or width<=0 -> NoPen.
  for (const QuadrantBorder& b : scene.borders) {
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
  }

  const qreal pSize = usedSvgFontSize(scene.pointLabelFontSize,
                                      scene.style.inheritedFontSize);
  const editor::CssPixelFont pFont =
      editor::makeCssPixelFont(scene.style.fontFamily, pSize);
  const auto ptText = color::resolveSvgPaint(scene.style.quadrantPointTextFill,
                                             color::SvgPaintKind::Text, inherited);
  for (const QuadrantPointG& p : scene.points) {
    QPen pen(Qt::NoPen);
    if (p.strokeWidth > 0.0) {
      const auto ps = color::resolveSvgPaint(p.stroke, color::SvgPaintKind::Stroke, inherited);
      if (!ps.none) {
        pen = QPen(ps.color, p.strokeWidth);
        pen.setCapStyle(Qt::FlatCap);
      }
    }
    if (p.radius > 0.0) {
      painter.setPen(pen);
      const auto pfill = color::resolveSvgPaint(p.fill, color::SvgPaintKind::Fill, inherited);
      painter.setBrush(pfill.none ? Qt::NoBrush : QBrush(pfill.color));
      painter.drawEllipse(QPointF(p.x, p.y), p.radius, p.radius);
    }
    if (pSize > 0.0 && !ptText.none && !p.text.isEmpty()) {
      painter.setPen(ptText.color);
      drawScaledText(painter, pFont,
                     QRectF(p.x - 200.0, p.y + scene.pointTextPadding, 400.0, 40.0),
                     Qt::AlignHCenter | Qt::AlignTop, p.text);
    }
  }

  // Axis labels use their own font-size and upstream text-anchor. The old
  // fixed QRect(-400,...)+AlignLeft placed start-anchored labels 400px outside
  // the chart and also used the X-axis font for Y-axis text.
  for (const QuadrantAxisLabel& a : scene.axisLabels) {
    if (a.text.isEmpty()) continue;
    const auto af = color::resolveSvgPaint(a.fill, color::SvgPaintKind::Text, inherited);
    if (af.none) continue;
    painter.setPen(af.color);
    painter.save();
    painter.translate(a.x, a.y);
    painter.rotate(a.rotation);
    const qreal aSize = usedSvgFontSize(a.fontSize, scene.style.inheritedFontSize);
    const editor::CssPixelFont aFont =
        editor::makeCssPixelFont(scene.style.fontFamily, aSize);
    const qreal width = std::max<qreal>(aFont.horizontalAdvance(a.text) + 4.0, 4.0);
    drawScaledText(painter, aFont,
                   QRectF(a.centered ? -width / 2.0 : 0.0, 0.0, width, aSize + 8.0),
                   Qt::AlignLeft | Qt::AlignTop, a.text);
    painter.restore();
  }

  // Title.
  if (!scene.titleText.isEmpty()) {
    const qreal tSize = usedSvgFontSize(scene.titleFontSizeCfg,
                                        scene.style.inheritedFontSize);
    const auto tf =
        color::resolveSvgPaint(scene.style.quadrantTitleFill, color::SvgPaintKind::Text, inherited);
    if (!tf.none && tSize > 0.0) {
      painter.setPen(tf.color);
      const editor::CssPixelFont tFont =
          editor::makeCssPixelFont(scene.style.fontFamily, tSize);
      const qreal width = std::max<qreal>(tFont.horizontalAdvance(scene.titleText) + 4.0, 4.0);
      drawScaledText(painter, tFont,
                     QRectF(scene.titleX - width / 2.0, scene.titleY,
                            width, tSize + 8.0),
                     Qt::AlignLeft | Qt::AlignTop, scene.titleText);
    }
  }
}

}  // namespace muffin::mermaid::quadrant
