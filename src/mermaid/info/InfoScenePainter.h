#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::info {

struct InfoScene;

void paintInfoScene(QPainter& painter, const InfoScene& scene,
                    const MermaidPaintOptions& options = {});

}  // namespace muffin::mermaid::info
