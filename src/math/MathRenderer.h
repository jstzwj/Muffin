#pragma once

#include "math/MathRenderNode.h"
#include "math/MathSettings.h"
#include "theme/RenderTheme.h"

#include <QString>

namespace muffin::math {

class MathRenderer {
public:
  static qreal katexRootFontPixelSize(const RenderTheme& theme);

  MathLayoutResult render(const QString& tex, const RenderTheme& theme, bool displayMode, qreal maxWidth = 0.0) const;
  MathLayoutResult render(const QString& tex, const RenderTheme& theme, bool displayMode, const MathSettings& settings, qreal maxWidth = 0.0) const;
};

}  // namespace muffin::math
