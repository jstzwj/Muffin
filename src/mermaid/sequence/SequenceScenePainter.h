#pragma once

#include "mermaid/sequence/SequenceScene.h"

#include <QImage>

class QPainter;

namespace muffin::mermaid::sequence {

struct SequenceViewportOptions {
  qreal diagramMarginX = 50.0;
  qreal diagramMarginY = 10.0;
  qreal boxMargin = 10.0;
  qreal bottomMarginAdj = 1.0;
  bool mirrorActors = true;
};

void paintSequenceScene(const SequenceScene& scene, QPainter& painter);
QRectF sequenceViewportRect(const SequenceScene& scene,
                            SequenceViewportOptions options = {});
QImage renderSequenceSceneToImage(const SequenceScene& scene, qreal dpr = 1.0,
                                  qreal padding = 8.0);
QImage renderSequenceSceneToImage(const SequenceScene& scene, qreal dpr,
                                  SequenceViewportOptions options);

}  // namespace muffin::mermaid::sequence
