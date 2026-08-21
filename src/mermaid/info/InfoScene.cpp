#include "mermaid/info/InfoScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/info/InfoScenePainter.h"

#include <QJsonObject>

#include <utility>

namespace muffin::mermaid::info {
namespace {

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

}  // namespace

void InfoScene::paint(QPainter& painter,
                      const MermaidPaintOptions& options) const {
  paintInfoScene(painter, *this, options);
}

QJsonObject InfoScene::toJsonObject() const {
  return {{QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("text"), text},
          {QStringLiteral("textBounds"), rectJson(textBounds)},
          {QStringLiteral("textAdvance"), textAdvance},
          {QStringLiteral("x"), anchor.x()},
          {QStringLiteral("y"), anchor.y()},
          {QStringLiteral("fontSize"), style.fontSize},
          {QStringLiteral("fontFamily"), style.fontFamily},
          {QStringLiteral("textColor"), style.textColor},
          {QStringLiteral("fontWeight"), int(style.fontWeight)},
          {QStringLiteral("opacity"), style.opacity},
          {QStringLiteral("textVisible"), style.textVisible},
          {QStringLiteral("useMaxWidth"), true}};
}

InfoScene buildInfoScene(InfoSceneStyle style) {
  InfoScene scene;
  scene.style = std::move(style);
  editor::CssPixelFont cssFont = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(scene.style.fontFamily), scene.style.fontSize);
  cssFont.font.setWeight(scene.style.fontWeight);
  if (!scene.style.textVisible || scene.style.fontSize <= 0.0) {
    scene.textBounds = {};
    return scene;
  }
  flowchart::FlowLabelDocument document;
  document.text = scene.text;
  document.baseWeight = scene.style.fontWeight;
  scene.textAdvance = flowchart::measureOpenTypeDesignAdvance(
                          document,
                          editor::firstFontFamily(scene.style.fontFamily),
                          scene.style.fontSize)
                          .value_or(cssFont.horizontalAdvance(scene.text));
  const QRectF ink = flowchart::measureChromiumSvgTextBounds(
      document, editor::firstFontFamily(scene.style.fontFamily),
      scene.style.fontSize, scene.style.fontWeight);
  // The shared SVG helper models createFormattedText() with its first baseline
  // at 1em. Info emits a raw <text y="40">, so move that model's baseline to
  // the literal anchor while preserving text-anchor:middle's design advance.
  scene.textBounds = ink.translated(scene.anchor.x() - scene.textAdvance / 2.0,
                                   scene.anchor.y() - scene.style.fontSize);
  return scene;
}

}  // namespace muffin::mermaid::info
