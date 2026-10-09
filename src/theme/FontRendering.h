#pragma once

#include <QFont>

namespace muffin::font_rendering {

// Apply the platform screen-rasterization policy without changing any
// typographic property (family, size, weight, spacing, or kerning).
void configureForScreen(QFont& font);

// Author spacing remains separate from the platform's synthetic-bold advance
// correction. Reapplying this function is idempotent.
void configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing);

}  // namespace muffin::font_rendering
