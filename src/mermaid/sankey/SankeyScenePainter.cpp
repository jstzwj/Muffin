#include "mermaid/sankey/SankeyScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/text/ChromiumTextMetrics.h"
#include "mermaid/text/LabelText.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::sankey {
namespace {

QColor paintColor(const QString &value, const QColor &fallback = Qt::black) {
  const QColor result = color::toQColor(value);
  return result.isValid() ? result : fallback;
}

QColor withOpacity(QColor color, qreal opacity) {
  if (!std::isfinite(opacity)) opacity = 1.0;
  color.setAlphaF(std::clamp(color.alphaF() * opacity, 0.0, 1.0));
  return color;
}

void drawLabel(const SankeyScene &scene, const SankeyLabelGeometry &label,
               QPainter &painter) {
  if (!label.visible || !(label.opacity > 0.0)) return;
  const QString family = label.fontFamily.isEmpty() ? scene.style.fontFamily
                                                     : label.fontFamily;
  auto font = editor::makeUnhintedCssPixelFont(family, label.fontSize);
  font.font.setWeight(label.fontWeight);
  if (!(font.scale > 0.0))
    return;
  const QString text = text::collapsedSvgText(label.text);
  const QFontMetricsF metrics(font.font);
  const qreal qtAdvance = metrics.horizontalAdvance(text) * font.scale;
  const qreal advance =
      std::ceil(textmetrics::harfBuzzAdvance(text, family, label.fontSize,
                                             label.fontWeight)
                    .value_or(qtAdvance) *
                64.0) /
      64.0;
  qreal x = label.position.x();
  if (label.anchor == QLatin1String("end"))
    x -= advance;
  else if (label.anchor == QLatin1String("middle"))
    x -= advance / 2.0;
  const qreal baseline = label.position.y() + label.dyEm * label.fontSize;

  painter.save();
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  if (label.backgroundLayer) {
    QPainterPath glyphs;
    glyphs.addText(QPointF(0, 0), font.font, text);
    QPen pen(paintColor(label.stroke, Qt::white),
             label.strokeWidth / font.scale, Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    pen.setColor(withOpacity(pen.color(), label.opacity));
    painter.setPen(pen);
    painter.setBrush(paintColor(scene.style.textColor));
    painter.drawPath(glyphs);
  } else {
    painter.setFont(font.font);
    painter.setPen(withOpacity(
        paintColor(label.fill, paintColor(scene.style.textColor)),
        label.opacity));
    painter.drawText(QPointF(0, 0), text);
  }
  painter.restore();
}

} // namespace

void paintSankeyScene(const SankeyScene &scene, QPainter &painter,
                      const MermaidPaintOptions &) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  for (const auto &node : scene.nodes) {
    if (!node.visible) continue;
    const color::SvgPaint stroke = color::resolveSvgPaint(
        node.stroke, color::SvgPaintKind::Stroke, QColor(Qt::black));
    if (stroke.none || !(node.strokeWidth > 0.0))
      painter.setPen(Qt::NoPen);
    else
      painter.setPen(QPen(withOpacity(stroke.color, node.strokeOpacity),
                          node.strokeWidth));
    painter.setBrush(withOpacity(paintColor(node.color), node.opacity));
    painter.drawRect(
        QRectF(node.x0, node.y0, node.x1 - node.x0, node.y1 - node.y0));
  }
  painter.restore();

  for (const auto &label : scene.labels)
    drawLabel(scene, label, painter);

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const auto &link : scene.links) {
    if (!link.visible) continue;
    painter.save();
    if (link.mixBlendMode.compare(QLatin1String("multiply"),
                                  Qt::CaseInsensitive) == 0)
      painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.setOpacity(link.opacity);
    QBrush brush;
    if (link.stroke == QLatin1String("gradient")) {
      const auto &source = scene.nodes.at(link.source);
      const auto &target = scene.nodes.at(link.target);
      QLinearGradient gradient(source.x1, 0, target.x0, 0);
      gradient.setColorAt(0, paintColor(link.sourceColor));
      gradient.setColorAt(1, paintColor(link.targetColor));
      brush = QBrush(gradient);
    } else {
      brush = QBrush(paintColor(link.stroke));
    }
    painter.setPen(
        QPen(brush, link.width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(link.path);
    painter.restore();
  }
  painter.restore();
}

} // namespace muffin::mermaid::sankey
