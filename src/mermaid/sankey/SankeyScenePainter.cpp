#include "mermaid/sankey/SankeyScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/text/ChromiumTextMetrics.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::sankey {
namespace {

QString visibleText(QString value) {
  value.replace(
      QRegularExpression(QStringLiteral(R"([\x{0009}-\x{000D}\x{0020}]+)")),
      QStringLiteral(" "));
  return value.trimmed();
}

QColor paintColor(const QString &value, const QColor &fallback = Qt::black) {
  const QColor result = color::toQColor(value);
  return result.isValid() ? result : fallback;
}

void drawLabel(const SankeyScene &scene, const SankeyLabelGeometry &label,
               QPainter &painter) {
  const auto font =
      editor::makeUnhintedCssPixelFont(scene.style.fontFamily, 14.0);
  if (!(font.scale > 0.0))
    return;
  const QString text = visibleText(label.text);
  const QFontMetricsF metrics(font.font);
  const qreal qtAdvance = metrics.horizontalAdvance(text) * font.scale;
  const qreal advance =
      std::ceil(textmetrics::harfBuzzAdvance(text, scene.style.fontFamily, 14.0)
                    .value_or(qtAdvance) *
                64.0) /
      64.0;
  qreal x = label.position.x();
  if (label.anchor == QLatin1String("end"))
    x -= advance;
  else if (label.anchor == QLatin1String("middle"))
    x -= advance / 2.0;
  const qreal baseline = label.position.y() + label.dyEm * 14.0;

  painter.save();
  painter.translate(x, baseline);
  painter.scale(font.scale, font.scale);
  if (label.backgroundLayer) {
    QPainterPath glyphs;
    glyphs.addText(QPointF(0, 0), font.font, text);
    QPen pen(paintColor(label.stroke, Qt::white),
             label.strokeWidth / font.scale, Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(paintColor(scene.style.textColor));
    painter.drawPath(glyphs);
  } else {
    painter.setFont(font.font);
    painter.setPen(paintColor(label.fill, paintColor(scene.style.textColor)));
    painter.drawText(QPointF(0, 0), text);
  }
  painter.restore();
}

} // namespace

void paintSankeyScene(const SankeyScene &scene, QPainter &painter,
                      const MermaidPaintOptions &) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  for (const auto &node : scene.nodes) {
    painter.setBrush(paintColor(node.color));
    painter.drawRect(
        QRectF(node.x0, node.y0, node.x1 - node.x0, node.y1 - node.y0));
  }
  painter.restore();

  for (const auto &label : scene.labels)
    drawLabel(scene, label, painter);

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setCompositionMode(QPainter::CompositionMode_Multiply);
  painter.setOpacity(0.5);
  for (const auto &link : scene.links) {
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
  }
  painter.restore();
}

} // namespace muffin::mermaid::sankey
