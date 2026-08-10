#include "mermaid/info/InfoScenePainter.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

namespace muffin::mermaid::info {
namespace {

color::SvgPaint rootFill(const QString& value) {
  const QString text = value.trimmed();
  if (text.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return {true, {}};
  if (color::isParsableColor(text)) return {false, color::toQColor(text)};
  return {false, Qt::black};
}

}  // namespace

void paintInfoScene(QPainter& painter, const InfoScene& scene,
                    const MermaidPaintOptions&) {
  const color::SvgPaint fill = rootFill(scene.style.textColor);
  if (fill.none) return;

  QFont font;
  MermaidFontRegistry::configureFont(font, scene.style.fontFamily);
  font.setPixelSize(32);
  font.setHintingPreference(QFont::PreferNoHinting);
  painter.setFont(font);
  painter.setPen(QPen(fill.color));
  painter.setBrush(Qt::NoBrush);
  const qreal advance = QFontMetricsF(font).horizontalAdvance(scene.text);
  painter.drawText(QPointF(scene.anchor.x() - advance / 2.0,
                           scene.anchor.y()),
                   scene.text);
}

}  // namespace muffin::mermaid::info
