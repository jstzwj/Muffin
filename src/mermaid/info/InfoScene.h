#pragma once

#include "mermaid/MermaidScene.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace muffin::mermaid::info {

struct InfoSceneStyle {
  QString fontFamily;
  QString textColor;
};

struct InfoScene final : MermaidScene {
  QRectF bounds{0.0, 0.0, 400.0, 150.0};
  QPointF anchor{100.0, 40.0};
  QRectF textBounds;
  QString text = QStringLiteral("v11.16.0");
  InfoSceneStyle style;

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
};

InfoScene buildInfoScene(InfoSceneStyle style);

}  // namespace muffin::mermaid::info
