#pragma once

#include "mermaid/theme/MermaidColor.h"

#include <QColor>

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::timeline {

struct TimelineScene;

// Resolved SVG paint for one CSS value, shared by the painter and the
// themeCSS parity comparator. `none` means the declaration paints nothing.
using TimelinePaintState = color::SvgPaint;

// An element's `fill` against the timeline root: the keyword family resolves
// (inherit/unset/revert/empty/garbage take the root fill, currentColor takes
// the element's resolved `color`, none suppresses painting).
TimelinePaintState timelineElementFill(const QString& value, const QColor& root,
                                       const QColor& currentColor);

// `stroke`: SVG's initial value is none, and an empty or invalid declaration
// falls back to the element's presentation attribute stroke. Keyword values
// resolve against `inheritedColor` (the root chain's paint).
TimelinePaintState timelineLineStroke(const QString& value,
                                      const QColor& presentation,
                                      const QColor& inheritedColor);

void paintTimelineScene(const TimelineScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::timeline
