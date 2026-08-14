#pragma once

#include <QColor>

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::journey {

struct JourneyScene;

// Resolved SVG paint for one CSS value, shared by the painter and the
// themeCSS parity comparator. `none` means the declaration paints nothing.
struct JourneyPaintState {
  bool none = false;
  QColor color = Qt::black;
};

// The svg root: no declaration reaches it in the journey DOM, so CSS
// globals, empty declarations and garbage resolve to the initial black.
JourneyPaintState journeyRootSvgFill(const QString& value);

// An element's `fill`: keywords participate (inherit/unset/revert take the
// root, revert-layer/empty take the presentation attribute), currentColor
// resolves against the journey root's black color.
JourneyPaintState journeyElementSvgFill(
    const QString& value, const JourneyPaintState& root,
    const JourneyPaintState& presentation);

// `stroke`: SVG's initial value is none, so the keyword family suppresses
// painting instead of inheriting.
JourneyPaintState journeyLineStroke(const QString& value,
                                    const QColor& presentation);

void paintJourneyScene(const JourneyScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::journey
