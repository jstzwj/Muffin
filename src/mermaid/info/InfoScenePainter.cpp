#include "mermaid/info/InfoScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QPainter>
#include <QPainterPath>

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
  if (!scene.style.textVisible || scene.style.fontSize <= 0.0 ||
      scene.style.opacity <= 0.0)
    return;
  const color::SvgPaint fill = rootFill(scene.style.textColor);
  if (fill.none) return;

  editor::CssPixelFont cssFont = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(scene.style.fontFamily), scene.style.fontSize);
  cssFont.font.setWeight(scene.style.fontWeight);
  painter.save();
  painter.scale(cssFont.scale, cssFont.scale);
  painter.setFont(cssFont.font);
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill.color);
  painter.setOpacity(painter.opacity() * scene.style.opacity);
  QPainterPath path;
  path.addText(QPointF((scene.anchor.x() - scene.textAdvance / 2.0) /
                           cssFont.scale,
                       (scene.anchor.y() - 0.5) / cssFont.scale),
               cssFont.font, scene.text);
  painter.drawPath(path);
  painter.restore();
}

}  // namespace muffin::mermaid::info
