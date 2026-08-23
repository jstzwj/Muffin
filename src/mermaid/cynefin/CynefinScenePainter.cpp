#include "mermaid/cynefin/CynefinScenePainter.h"

#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/text/LabelText.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::cynefin {
namespace {

color::SvgPaint fillPaint(const QString &value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
}

color::SvgPaint strokePaint(const QString &value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Stroke, Qt::black);
}

// themeCSS stroke-width resolution needs the shared length context.
struct CynefinPaintContext {
  CssLengthContext lengths;
  qreal diagonal = 1.0;
};

qreal cssStrokeWidthPx(const CynefinElementCss &css, qreal base,
                       const CynefinPaintContext &context) {
  const QString value = css.strokeWidth.trimmed().isEmpty()
                            ? QString::number(base) + QStringLiteral("px")
                            : css.strokeWidth;
  return editor::cssStrokeWidthPx(value, context.lengths, context.diagonal);
}

QPen pathPen(const QString &value, qreal width, const QVector<qreal> &dash) {
  const auto paint = strokePaint(value);
  if (paint.none || !(width > 0.0)) return Qt::NoPen;
  QPen pen(paint.color, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  if (!dash.isEmpty()) {
    QVector<qreal> scaled;
    scaled.reserve(dash.size());
    for (qreal dashLength : dash) scaled.append(dashLength / width);
    pen.setDashPattern(scaled);
  }
  return pen;
}

void paintPath(QPainter &painter, const CynefinPathGeometry &path,
               const CynefinPaintContext &context) {
  if (!path.css.visible) return;
  const auto fill = fillPaint(path.css.fill.isEmpty() ? path.fill
                                                      : path.css.fill);
  if (fill.none) painter.setBrush(Qt::NoBrush);
  else {
    QColor color = fill.color;
    const qreal fillOpacity = path.css.fillOpacity >= 0.0
                                  ? path.css.fillOpacity
                                  : path.fillOpacity;
    const qreal opacity =
        path.css.opacity >= 0.0 ? path.css.opacity : 1.0;
    color.setAlphaF(color.alphaF() * qBound(0.0, fillOpacity, 1.0) *
                    qBound(0.0, opacity, 1.0));
    painter.setBrush(color);
  }
  const QString stroke = path.css.stroke.isEmpty() ? path.stroke
                                                   : path.css.stroke;
  const qreal strokeWidth =
      cssStrokeWidthPx(path.css, path.strokeWidth, context);
  const auto strokeSide = strokePaint(stroke);
  if (strokeSide.none || !(strokeWidth > 0.0)) {
    painter.setPen(Qt::NoPen);
  } else {
    QPen pen = pathPen(stroke, strokeWidth, path.dash);
    QColor strokeColor = pen.color();
    const qreal strokeOpacity = path.css.strokeOpacity >= 0.0
                                    ? path.css.strokeOpacity
                                    : 1.0;
    const qreal opacity = path.css.opacity >= 0.0 ? path.css.opacity : 1.0;
    strokeColor.setAlphaF(strokeColor.alphaF() *
                          qBound(0.0, strokeOpacity, 1.0) *
                          qBound(0.0, opacity, 1.0));
    pen.setColor(strokeColor);
    painter.setPen(pen);
  }
  painter.drawPath(path.path);
}

void paintRect(QPainter &painter, const CynefinRectGeometry &rect,
               const CynefinPaintContext &context) {
  if (!rect.css.visible) return;
  const auto fill = fillPaint(rect.css.fill.isEmpty() ? rect.fill
                                                      : rect.css.fill);
  if (fill.none) painter.setBrush(Qt::NoBrush);
  else {
    QColor color = fill.color;
    const qreal fillOpacity = rect.css.fillOpacity >= 0.0
                                  ? rect.css.fillOpacity
                                  : rect.fillOpacity;
    const qreal opacity = rect.css.opacity >= 0.0 ? rect.css.opacity : 1.0;
    color.setAlphaF(color.alphaF() * qBound(0.0, fillOpacity, 1.0) *
                    qBound(0.0, opacity, 1.0));
    painter.setBrush(color);
  }
  const QString stroke = rect.css.stroke.isEmpty() ? rect.stroke
                                                   : rect.css.stroke;
  const qreal strokeWidth =
      cssStrokeWidthPx(rect.css, rect.strokeWidth, context);
  const auto strokeSide = strokePaint(stroke);
  if (strokeSide.none || !(strokeWidth > 0.0)) {
    painter.setPen(Qt::NoPen);
  } else {
    QPen pen = pathPen(stroke, strokeWidth, rect.dash);
    QColor strokeColor = pen.color();
    const qreal strokeOpacity = rect.css.strokeOpacity >= 0.0
                                    ? rect.css.strokeOpacity
                                    : 1.0;
    const qreal opacity = rect.css.opacity >= 0.0 ? rect.css.opacity : 1.0;
    strokeColor.setAlphaF(strokeColor.alphaF() *
                          qBound(0.0, strokeOpacity, 1.0) *
                          qBound(0.0, opacity, 1.0));
    pen.setColor(strokeColor);
    painter.setPen(pen);
  }
  if (rect.radius > 0.0)
    painter.drawRoundedRect(rect.rect, rect.radius, rect.radius,
                            Qt::AbsoluteSize);
  else
    painter.drawRect(rect.rect);
}

QStringList cssFontFamilies(const QString &expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

void paintText(QPainter &painter, const CynefinScene &scene,
               const CynefinTextGeometry &text) {
  if (!text.css.visible || text.text.isEmpty()) return;
  const qreal fontSize = text.css.fontSize >= 0.0 ? text.css.fontSize
                                                  : text.fontSize;
  if (!(fontSize > 0.0)) return;
  const QString fill = text.css.fill.isEmpty() ? text.fill : text.css.fill;
  const auto resolved = color::resolveSvgPaint(fill,
                                               color::SvgPaintKind::Text,
                                               Qt::black);
  if (resolved.none) return;
  const QString family = text.css.fontFamily.isEmpty()
                             ? scene.style.fontFamily
                             : text.css.fontFamily;
  const QStringList families = cssFontFamilies(family);
  const QString weight = text.css.fontWeight.trimmed().toLower();
  const bool bold = weight.isEmpty()
                        ? text.bold
                        : (weight == QLatin1String("bold") ||
                           weight == QLatin1String("bolder") ||
                           weight.toInt() >= 700);
  const QString fontStyle = text.css.fontStyle.trimmed().toLower();
  const bool italic = fontStyle.isEmpty()
                          ? text.italic
                          : fontStyle == QLatin1String("italic");
  auto font = editor::makeUnhintedCssPixelFont(families.first(), fontSize);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(bold ? QFont::Bold : QFont::Normal);
  font.font.setItalic(italic);
  const QString visible = text::collapsedSvgText(text.text);
  qreal width = QFontMetricsF(font.font).horizontalAdvance(visible) * font.scale;
  qreal x = std::isfinite(text.position.x()) ? text.position.x() : 0.0;
  if (text.anchor == QLatin1String("middle")) x -= width / 2.0;
  else if (text.anchor == QLatin1String("end")) x -= width;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      family, fontSize, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  qreal baseline = std::isfinite(text.position.y()) ? text.position.y() : 0.0;
  if (text.baseline == CynefinTextBaseline::Middle ||
      text.baseline == CynefinTextBaseline::Central)
    baseline += metrics.xHeight / 2.0;
  QColor penColor = resolved.color;
  penColor.setAlphaF(penColor.alphaF() *
                     (text.css.opacity >= 0.0 ? text.css.opacity : 1.0));
  painter.save();
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(penColor);
  painter.drawText(QPointF(0.0, 0.0), visible);
  painter.restore();
}

void paintArrowHead(QPainter &painter, const CynefinScene &scene,
                    const CynefinArrowGeometry &arrow) {
  const CynefinElementCss &css = scene.arrowHeadCss;
  if (!css.visible) return;
  const qreal strokeWidth = arrow.strokeWidth;
  if (!(strokeWidth > 0.0)) return;
  const auto fill = fillPaint(css.fill.isEmpty() ? arrow.stroke : css.fill);
  if (fill.none) return;
  const QPointF tangent = arrow.end - arrow.control;
  const qreal angle = std::atan2(tangent.y(), tangent.x());
  const qreal scale = strokeWidth * 0.6;
  QPolygonF triangle;
  triangle << QPointF(0.6 * scale, 0.0)
           << QPointF(-5.4 * scale, -3.0 * scale)
           << QPointF(-5.4 * scale, 3.0 * scale);
  QTransform transform;
  transform.translate(arrow.end.x(), arrow.end.y());
  transform.rotateRadians(angle);
  QColor fillColor = fill.color;
  fillColor.setAlphaF(fillColor.alphaF() *
                      (css.opacity >= 0.0 ? css.opacity : 1.0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(fillColor);
  painter.drawPolygon(transform.map(triangle));
}

} // namespace

void paintCynefinScene(const CynefinScene &scene, QPainter &painter,
                       const MermaidPaintOptions &) {
  CynefinPaintContext context;
  context.lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene.style.fontFamily),
      scene.style.rootFontSize);
  context.diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                     std::sqrt(2.0);
  painter.save();
  painter.translate(scene.rootTranslation);
  for (const auto &background : scene.backgrounds)
    paintRect(painter, background, context);
  for (const auto &boundary : scene.boundaries)
    paintPath(painter, boundary, context);
  paintPath(painter, scene.confusion, context);
  for (const auto &label : scene.labels)
    paintText(painter, scene, label);
  for (const auto &subtitle : scene.subtitles)
    paintText(painter, scene, subtitle);
  for (const auto &item : scene.items) {
    painter.save();
    painter.translate(item.translation);
    paintRect(painter, item.rect, context);
    paintText(painter, scene, item.text);
    painter.restore();
  }
  for (const auto &arrow : scene.arrows) {
    painter.setBrush(Qt::NoBrush);
    const QString stroke =
        arrow.css.stroke.isEmpty() ? arrow.stroke : arrow.css.stroke;
    const qreal strokeWidth =
        arrow.css.strokeWidth.trimmed().isEmpty()
            ? arrow.strokeWidth
            : editor::cssStrokeWidthPx(arrow.css.strokeWidth, context.lengths,
                                       context.diagonal);
    const auto strokeSide = strokePaint(stroke);
    if (strokeSide.none || !(strokeWidth > 0.0)) {
      painter.setPen(Qt::NoPen);
    } else {
      QPen pen = pathPen(stroke, strokeWidth, {});
      QColor strokeColor = pen.color();
      const qreal opacity = arrow.css.opacity >= 0.0 ? arrow.css.opacity : 1.0;
      const qreal strokeOpacity = arrow.css.strokeOpacity >= 0.0
                                      ? arrow.css.strokeOpacity
                                      : 1.0;
      strokeColor.setAlphaF(strokeColor.alphaF() *
                            qBound(0.0, strokeOpacity, 1.0) *
                            qBound(0.0, opacity, 1.0));
      pen.setColor(strokeColor);
      painter.setPen(pen);
    }
    painter.drawPath(arrow.path);
    paintArrowHead(painter, scene, arrow);
    paintText(painter, scene, arrow.label);
  }
  paintText(painter, scene, scene.title);
  painter.restore();
}

} // namespace muffin::mermaid::cynefin
