#pragma once

#include "mermaid/MermaidPaintOptions.h"

class QPainter;

namespace muffin::mermaid::cynefin {

struct CynefinScene;

void paintCynefinScene(const CynefinScene &scene, QPainter &painter,
                       const MermaidPaintOptions &options = {});

} // namespace muffin::mermaid::cynefin
