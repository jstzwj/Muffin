#pragma once

#include "theme/ThemeDefinition.h"

class QBrush;
class QRectF;

namespace muffin {

// Builds a Qt brush from a parsed CSS GradientSpec against a target rect.
// Gradients are rect-relative, so this is called at paint time per element (a
// theme's radial glow centres on each heading's own rect, not the page). Pure
// geometry and used values — no CSS parsing (that lives in CssValueParser).
namespace GradientPainter {

// True when the spec carries a usable gradient (kind set + at least two stops).
bool isGradient(const GradientSpec& spec);

// Linear: the gradient line follows the CSS angle (0 = to top, clockwise),
// spanning the rect's projection onto that direction. Radial circles/ellipses
// resolve their extent against the rect. Length-percentage stops, omitted stop
// positions and hard edges resolve against that used gradient axis.
QBrush makeBrush(const GradientSpec& spec, const QRectF& target, qreal lengthScale = 1);

}  // namespace GradientPainter

}  // namespace muffin
