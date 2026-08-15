#include "mermaid/info/InfoScene.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/info/InfoScenePainter.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonObject>

#include <algorithm>
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
  QFont font;
  MermaidFontRegistry::configureFont(font, scene.style.fontFamily);
  font.setPixelSize(std::max(1, qRound(scene.style.fontSize)));
  font.setWeight(scene.style.fontWeight);
  font.setHintingPreference(QFont::PreferNoHinting);
  if (!scene.style.textVisible || scene.style.fontSize <= 0.0) {
    scene.textBounds = {};
    return scene;
  }
  const QFontMetricsF metrics(font);
  const qreal advance = metrics.horizontalAdvance(scene.text);
  const QRectF ink = metrics.boundingRect(scene.text);
  scene.textBounds = ink.translated(
      scene.anchor.x() - advance / 2.0, scene.anchor.y());
  return scene;
}

}  // namespace muffin::mermaid::info
