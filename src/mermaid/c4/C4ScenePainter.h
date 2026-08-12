#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::c4 {
struct C4Scene;
void paintC4Scene(const C4Scene& scene, QPainter& painter,
                  const MermaidPaintOptions& options = {});
}
