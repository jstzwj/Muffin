#pragma once

#include "mermaid/MermaidScene.h"

class QPainter;

namespace muffin::mermaid::ishikawa {

struct IshikawaScene;

void paintIshikawaScene(const IshikawaScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::ishikawa
