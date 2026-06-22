#pragma once

#include "theme/ThemeDefinition.h"

class QBrush;
class QRectF;

namespace muffin {

// Builds a Qt brush from a parsed CSS GradientSpec against a target rect.
// Gradients are rect-relative, so this is called at paint time per element (a
// theme's radial glow centres on each heading's own rect, not the page). Pure
// geometry — no CSS parsing (that lives in CssThemeMapper).
namespace GradientPainter {

// True when the spec carries a usable gradient (kind set + at least one stop).
bool isGradient(const GradientSpec& spec);

// Linear: the gradient line follows the CSS angle (0 = to top, clockwise),
// spanning the rect's projection onto that direction. Radial: centred at
// spec.radialCenter (fractions of the rect) with radius spec.radialRadius × the
// rect's larger side. Returns an invalid brush when !isGradient.
QBrush makeBrush(const GradientSpec& spec, const QRectF& target);

}  // namespace GradientPainter

}  // namespace muffin
