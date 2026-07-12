#pragma once

#include <QFont>

namespace muffin::font_rendering {

// Apply the platform screen-rasterization policy without changing any
// typographic property (family, size, weight, spacing, or kerning).
void configureForScreen(QFont& font);

}  // namespace muffin::font_rendering
