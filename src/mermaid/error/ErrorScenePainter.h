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

// Paints a single icon path (by index into scene.iconPaths) with its resolved
// per-path CSS. The painter must already be in viewBox space (the scale block
// paintErrorScene applies). Split out so the per-path pixel oracle can render
// one icon in isolation, mirroring the browser fixture's visibility-isolation
// capture.
void paintErrorIcon(const ErrorScene& scene, QPainter& painter, int index);

}  // namespace muffin::mermaid::error
