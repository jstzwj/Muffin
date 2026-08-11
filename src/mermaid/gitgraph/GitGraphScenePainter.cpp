#include "mermaid/gitgraph/GitGraphScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
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
}

void GitGraphScene::paint(QPainter& painter, const MermaidPaintOptions&) const {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const GitGraphPrimitive& value : primitives) {
    painter.save();painter.setOpacity(value.opacity);
    QPen pen=value.stroke==QLatin1String("none")?Qt::NoPen:QPen(paintColor(value.stroke));if(pen.style()!=Qt::NoPen){pen.setWidthF(value.strokeWidth);pen.setCapStyle(Qt::RoundCap);if(!value.dash.isEmpty())pen.setDashPattern(value.dash);}painter.setPen(pen);
    if(value.gradientStroke){QLinearGradient gradient(value.rect.left(),value.rect.center().y(),value.rect.right(),value.rect.center().y());gradient.setColorAt(0.0,paintColor(style.gradientStart));gradient.setColorAt(1.0,paintColor(style.gradientStop));QPen gradientPen(QBrush(gradient),value.strokeWidth);painter.setPen(gradientPen);}
    const QBrush brush=(value.fill==QLatin1String("none")||value.fill==QLatin1String("transparent"))?Qt::NoBrush:QBrush(paintColor(value.fill));painter.setBrush(brush);
    if (!value.translation.isNull()) painter.translate(value.translation);
    if(value.rotation!=0){painter.translate(value.rotationOrigin);painter.rotate(value.rotation);painter.translate(-value.rotationOrigin);}
    switch(value.kind){case PrimitiveKind::Line:painter.drawLine(value.line);break;case PrimitiveKind::Path:painter.drawPath(value.path);break;case PrimitiveKind::Circle:painter.drawEllipse(value.center,value.radius,value.radius);break;case PrimitiveKind::Rect:painter.drawRoundedRect(value.rect,value.rx,value.rx);break;case PrimitiveKind::Polygon:painter.drawPolygon(value.polygon);break;case PrimitiveKind::Text:{auto css=editor::makeUnhintedCssPixelFont(style.fontFamily,value.fontSize);if(value.bold)css.font.setWeight(QFont::DemiBold);painter.setFont(css.font);painter.setPen(paintColor(value.fill));painter.translate(value.position);painter.scale(css.scale,css.scale);QFontMetricsF fm(css.font);const QStringList lines=value.textLines.isEmpty()?QStringList{value.text}:value.textLines;for(qsizetype i=0;i<lines.size();++i){const QString& line=lines.at(i);qreal x=0;if(value.anchor==QLatin1String("middle"))x=-fm.horizontalAdvance(line)/2;else if(value.anchor==QLatin1String("end"))x=-fm.horizontalAdvance(line);painter.drawText(QPointF(x,value.fontSize*i),line);}break;}}
    painter.restore();
  }
}

}  // namespace muffin::mermaid::gitgraph
