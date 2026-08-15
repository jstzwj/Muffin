#include "mermaid/eventmodeling/EventModelingScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::eventmodeling {
namespace {

QColor resolved(const QString& value, const QColor& fallback = Qt::black) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Fill, fallback);
  return paint.none ? QColor(Qt::transparent) : paint.color;
}

QFont textFont(const QString& family, qreal size, bool bold) {
  QFont font = flowchart::makeFlowLabelFont(
      family, size, bold ? QFont::Bold : QFont::Normal);
  return font;
}

// themeCSS stroke-width resolution against the shared length context.
qreal cssStrokeWidthOf(const EventModelingElementCss& css, qreal base,
                       const CssLengthContext& lengths, qreal diagonal) {
  const QString value =
      css.strokeWidth.trimmed().isEmpty()
          ? QString::number(base) + QStringLiteral("px")
          : css.strokeWidth;
  return editor::cssStrokeWidthPx(value, lengths, diagonal);
}

void drawSwimlaneLabel(const EventModelingScene& scene, QPainter& painter,
                       const EventModelingSwimlaneGeometry& lane) {
  if (!lane.textCss.visible) return;
  const EventModelingElementCss& css = lane.textCss;
  const QString family = css.fontFamily.isEmpty() ? scene.style.fontFamily
                                                  : css.fontFamily;
  const qreal size = css.fontSize >= 0.0 ? css.fontSize
                                         : scene.style.rootFontSize;
  const QString weight = css.fontWeight.trimmed().toLower();
  const bool bold = weight.isEmpty()
                        ? true  // upstream font-weight attr defaults to bold
                        : (weight == QLatin1String("bold") ||
                           weight == QLatin1String("bolder") ||
                           weight.toInt() >= 700);
  painter.save();
  painter.setFont(textFont(family, size, bold));
  QColor pen = resolved(css.fill.isEmpty() ? scene.style.textColor : css.fill);
  const qreal opacity = css.opacity >= 0.0 ? css.opacity : 1.0;
  pen.setAlphaF(std::clamp(pen.alphaF() * opacity, 0.0, 1.0));
  painter.setPen(pen);
  painter.drawText(lane.labelPosition, lane.label);
  painter.restore();
}

QPen strokePen(const QString& value, qreal strokeWidth) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Stroke, Qt::black);
  if (paint.none) return QPen(Qt::NoPen);
  QPen pen(paint.color, strokeWidth);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

QBrush fillBrush(const QString& value) {
  const color::SvgPaint paint =
      color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

void drawArrow(const EventModelingScene& scene, QPainter& painter,
               const QLineF& line) {
  if (line.length() <= 0.0) return;
  if (!scene.markerCss.visible) return;
  const qreal angle = std::atan2(line.dy(), line.dx());
  const QPointF tip = line.p2();
  const QPointF back(tip.x() - 10.0 * std::cos(angle),
                     tip.y() - 10.0 * std::sin(angle));
  const QPointF normal(-std::sin(angle) * 3.5, std::cos(angle) * 3.5);
  painter.save();
  painter.setPen(Qt::NoPen);
  QColor arrow = resolved(scene.markerCss.fill.isEmpty()
                              ? scene.style.arrowhead
                              : scene.markerCss.fill);
  const qreal opacity =
      scene.markerCss.opacity >= 0.0 ? scene.markerCss.opacity : 1.0;
  arrow.setAlphaF(std::clamp(arrow.alphaF() * opacity, 0.0, 1.0));
  painter.setBrush(arrow);
  painter.drawPolygon(QPolygonF{back + normal, tip, back - normal});
  painter.restore();
}

}  // namespace

void paintEventModelingScene(const EventModelingScene& scene,
                             QPainter& painter,
                             const MermaidPaintOptions&) {
  painter.save();
  const CssLengthContext lengths = editor::pieCssLengthContext(
      editor::firstFontFamily(scene.style.fontFamily),
      scene.style.rootFontSize);
  const qreal diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                         std::sqrt(2.0);
  for (const auto& lane : scene.swimlanes) {
    if (!lane.rectCss.visible) continue;
    const qreal sw = cssStrokeWidthOf(lane.rectCss, 1.0, lengths, diagonal);
    QColor laneFill = resolved(lane.rectCss.fill.isEmpty()
                                   ? scene.style.swimlaneFill
                                   : lane.rectCss.fill);
    const qreal laneOpacity =
        lane.rectCss.opacity >= 0.0 ? lane.rectCss.opacity : 1.0;
    laneFill.setAlphaF(std::clamp(laneFill.alphaF() * laneOpacity, 0.0, 1.0));
    painter.setBrush(laneFill);
    const QString laneStroke = lane.rectCss.stroke.isEmpty()
                                   ? scene.style.swimlaneStroke
                                   : lane.rectCss.stroke;
    if (sw > 0.0 && !laneStroke.isEmpty() &&
        laneStroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0)
      painter.setPen(strokePen(laneStroke, sw));
    else
      painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(lane.rect, 3.0, 3.0);
    drawSwimlaneLabel(scene, painter, lane);
  }
  for (const auto& box : scene.boxes) {
    if (!box.rectCss.visible) continue;
    const qreal sw = cssStrokeWidthOf(box.rectCss, 1.0, lengths, diagonal);
    QColor boxFill = resolved(
        box.rectCss.fill.isEmpty() ? box.fill : box.rectCss.fill);
    const qreal boxOpacity =
        box.rectCss.opacity >= 0.0 ? box.rectCss.opacity : 1.0;
    boxFill.setAlphaF(std::clamp(boxFill.alphaF() * boxOpacity, 0.0, 1.0));
    painter.setBrush(boxFill);
    const QString boxStroke = box.rectCss.stroke.isEmpty()
                                  ? box.stroke
                                  : box.rectCss.stroke;
    if (sw > 0.0 && !boxStroke.isEmpty() &&
        boxStroke.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0)
      painter.setPen(strokePen(boxStroke, sw));
    else
      painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(box.rect, 3.0, 3.0);
    if (!box.labelCss.visible) continue;
    // The box label lives in a foreignObject span: HTML color semantics,
    // with the chain starting at the initial black (nothing upstream sets
    // `color`).
    const QString labelFamily = box.labelCss.fontFamily.isEmpty()
                                    ? scene.style.fontFamily
                                    : box.labelCss.fontFamily;
    const qreal labelSize = box.labelCss.fontSize >= 0.0
                                ? box.labelCss.fontSize
                                : scene.style.rootFontSize;
    const QString labelWeight = box.labelCss.fontWeight.trimmed().toLower();
    const bool labelBold = labelWeight.isEmpty()
                               ? true  // the html content wraps names in <b>
                               : (labelWeight == QLatin1String("bold") ||
                                  labelWeight == QLatin1String("bolder") ||
                                  labelWeight.toInt() >= 700);
    QColor labelColor = resolved(box.labelCss.color.isEmpty()
                                     ? QStringLiteral("black")
                                     : box.labelCss.color);
    const qreal labelOpacity =
        box.labelCss.opacity >= 0.0 ? box.labelCss.opacity : 1.0;
    labelColor.setAlphaF(
        std::clamp(labelColor.alphaF() * labelOpacity, 0.0, 1.0));
    flowchart::paintFlowLabel(painter, box.label, box.foreignObjectRect,
                              labelFamily, labelSize, 1.2, labelColor,
                              labelBold,
                              flowchart::FlowLabelAlign::Center);
  }
  for (const auto& relation : scene.relations) {
    if (!relation.css.visible) continue;
    const qreal sw =
        cssStrokeWidthOf(relation.css, 1.0, lengths, diagonal);
    const QString strokeValue =
        relation.css.stroke.isEmpty() ? relation.stroke : relation.css.stroke;
    if (sw > 0.0 && !strokeValue.isEmpty() &&
        strokeValue.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
      QPen pen = strokePen(strokeValue, sw);
      QColor penColor = pen.color();
      const qreal opacity =
          relation.css.opacity >= 0.0 ? relation.css.opacity : 1.0;
      penColor.setAlphaF(std::clamp(penColor.alphaF() * opacity, 0.0, 1.0));
      pen.setColor(penColor);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(relation.line);
    }
    drawArrow(scene, painter, relation.line);
  }
  painter.restore();
}

}  // namespace muffin::mermaid::eventmodeling
