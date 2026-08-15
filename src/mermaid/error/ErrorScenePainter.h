#pragma once

// Painter for the native "error" diagram scene (see ErrorScene.h for the
// upstream contract). Lives in its own TU like every other family painter.

#include <QRectF>

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::error {

struct ErrorScene;

void paintErrorScene(const ErrorScene& scene, QPainter& painter);

}  // namespace muffin::mermaid::error
