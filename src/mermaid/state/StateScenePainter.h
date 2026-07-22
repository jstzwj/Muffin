#pragma once

#include "mermaid/state/StateScene.h"

#include <QImage>

class QPainter;

namespace muffin::mermaid::state {

void paintStateScene(const StateScene& scene, QPainter& painter);
QImage renderStateSceneToImage(const StateScene& scene, qreal dpr = 1.0,
                               qreal padding = 8.0);

}  // namespace muffin::mermaid::state
