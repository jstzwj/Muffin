#pragma once

#include "mermaid/sequence/SequenceScene.h"

#include <QImage>

class QPainter;

namespace muffin::mermaid::sequence {

void paintSequenceScene(const SequenceScene& scene, QPainter& painter);
QImage renderSequenceSceneToImage(const SequenceScene& scene, qreal dpr = 1.0,
                                  qreal padding = 8.0);

}  // namespace muffin::mermaid::sequence
