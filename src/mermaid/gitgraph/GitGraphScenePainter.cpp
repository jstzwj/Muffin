#include "mermaid/gitgraph/GitGraphScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/scene/SvgStroke.h"
#include "mermaid/theme/MermaidColor.h"

#include <QLinearGradient>
#include <QPainter>

namespace muffin::mermaid::gitgraph {
namespace {

QColor paintColor(const QString& value, const QColor& fallback = Qt::black) {
  const auto paint = color::resolveSvgPaint(value, color::SvgPaintKind::Fill,
                                            fallback);
  return paint.none || !paint.color.isValid() ? fallback : paint.color;
}

// themeCSS stroke-width resolves like any CSS length: em/ex against the root
// font, percentages against the viewBox diagonal.
qreal cssStrokeWidthValue(const QString& raw, const GitGraphScene& scene) {
  CssLengthContext context = editor::pieCssLengthContext(
      scene.style.fontFamily, scene.style.fontSize);
  context.viewportPx = scene.bounds.size();
  const qreal diagonal =
      std::hypot(scene.bounds.width(), scene.bounds.height()) /
      std::sqrt(2.0);
  return editor::cssStrokeWidthPx(raw, context, diagonal);
}

}  // namespace

void GitGraphScene::paint(QPainter& painter, const MermaidPaintOptions&) const {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  // Mermaid paints `#id { fill: textColor }` on the svg root: fill keywords
  // and color-less elements inherit it, while stroke inheritance resolves
  // against the root's initial `none` (resolveSvgPaint models both).
  const QColor rootFill = color::resolveSvgPaint(
      style.textColor, color::SvgPaintKind::Fill, QColor(Qt::black)).color;
  for (const GitGraphPrimitive& value : primitives) {
    if (!value.css.visible) continue;
    const QString fillValue = !value.css.fill.isEmpty() ? value.css.fill : value.fill;
    const QString strokeValue = !value.css.stroke.isEmpty() ? value.css.stroke : value.stroke;
    const qreal strokeWidth = !value.css.strokeWidth.isEmpty()
        ? cssStrokeWidthValue(value.css.strokeWidth, *this)
        : value.strokeWidth;
    // The css slot carries the composed effective opacity (base stylesheet
    // value plus the ancestor product); without themeCSS the base stands.
    const qreal opacity = value.css.opacity >= 0.0 ? value.css.opacity : value.opacity;
    painter.save();painter.setOpacity(opacity);
    const color::SvgPaint fillPaint = color::resolveSvgPaint(
        fillValue, color::SvgPaintKind::Fill, rootFill);
    const color::SvgPaint strokePaint = color::resolveSvgPaint(
        strokeValue, color::SvgPaintKind::Stroke, {});
    QPen pen = (strokePaint.none || !strokePaint.color.isValid() ||
                !(strokeWidth > 0.0))
        ? QPen(Qt::NoPen)
        : QPen(strokePaint.color, strokeWidth);
    if (pen.style() != Qt::NoPen) {
      pen.setCapStyle(Qt::FlatCap);
      pen.setJoinStyle(Qt::MiterJoin);
      if (!value.dash.isEmpty())
        pen.setDashPattern(scene::normalizedSvgDashPattern(
            value.dash, strokeWidth));
    }
    painter.setPen(pen);
    if (value.gradientStroke && value.css.stroke.isEmpty()) {
      QLinearGradient gradient(value.rect.left(), value.rect.center().y(),
                               value.rect.right(), value.rect.center().y());
      gradient.setColorAt(0.0, paintColor(style.gradientStart));
      gradient.setColorAt(1.0, paintColor(style.gradientStop));
      QPen gradientPen(QBrush(gradient), strokeWidth);
      gradientPen.setCapStyle(Qt::FlatCap);
      gradientPen.setJoinStyle(Qt::MiterJoin);
      painter.setPen(gradientPen);
    }
    const QBrush brush =
        (fillPaint.none || fillValue == QLatin1String("none") ||
         fillValue == QLatin1String("transparent"))
        ? QBrush(Qt::NoBrush)
        : QBrush(fillPaint.color.isValid() ? fillPaint.color
                                           : paintColor(fillValue, rootFill));
    painter.setBrush(brush);
    if (!value.translation.isNull()) painter.translate(value.translation);
    if (value.rotation != 0) {
      painter.translate(value.rotationOrigin);
      painter.rotate(value.rotation);
      painter.translate(-value.rotationOrigin);
    }
    switch (value.kind) {
    case PrimitiveKind::Line: painter.drawLine(value.line); break;
    case PrimitiveKind::Path: painter.drawPath(value.path); break;
    case PrimitiveKind::Circle: painter.drawEllipse(value.center, value.radius, value.radius); break;
    case PrimitiveKind::Rect: painter.drawRoundedRect(value.rect, value.rx, value.rx); break;
    case PrimitiveKind::Polygon: painter.drawPolygon(value.polygon); break;
    case PrimitiveKind::Text: {
      const QString family = !value.css.fontFamily.trimmed().isEmpty()
          ? value.css.fontFamily : style.fontFamily;
      const qreal size = value.css.fontSize >= 0.0 ? value.css.fontSize : value.fontSize;
      QFont::Weight weight = value.bold ? QFont::DemiBold : QFont::Normal;
      if (!value.css.fontWeight.trimmed().isEmpty())
        weight = editor::cssFontWeightToQt(QJsonValue(value.css.fontWeight),
                                           QFont::Normal);
      auto css = editor::makeUnhintedCssPixelFont(family, size);
      css.font.setWeight(weight);
      painter.setFont(css.font);
      painter.setPen(paintColor(fillValue, rootFill));
      painter.translate(value.position);
      painter.scale(css.scale, css.scale);
      QFontMetricsF fm(css.font);
      const QStringList lines = value.textLines.isEmpty()
          ? QStringList{value.text} : value.textLines;
      for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        qreal x = 0;
        if (value.anchor == QLatin1String("middle"))
          x = -fm.horizontalAdvance(line) / 2;
        else if (value.anchor == QLatin1String("end"))
          x = -fm.horizontalAdvance(line);
        painter.drawText(QPointF(x, size * i), line);
      }
      break;
    }
    }
    painter.restore();
  }
}

}  // namespace muffin::mermaid::gitgraph
