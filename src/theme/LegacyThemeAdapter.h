#pragma once
#include "theme/ThemeDefinition.h"

namespace muffin {
// Compatibility ends at the loading boundary: native/JSON tokens become author
// declarations, then follow the same cascade and layout path as CSS themes.
ThemeDefinition adaptLegacyTheme(const ThemeDefinition& definition);
}  // namespace muffin
