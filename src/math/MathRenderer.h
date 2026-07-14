#pragma once

#include "math/MathRenderNode.h"
#include "math/MathSettings.h"

#include <QColor>
#include <QString>

namespace muffin { class RenderTheme; }

namespace muffin::math {

class MathRenderer {
public:
  static qreal katexRootFontPixelSize(const RenderTheme& theme);

  MathLayoutResult render(const QString& tex, const RenderTheme& theme, bool displayMode, qreal maxWidth = 0.0) const;
  MathLayoutResult render(const QString& tex, const RenderTheme& theme, bool displayMode, const MathSettings& settings, qreal maxWidth = 0.0) const;
  MathLayoutResult render(const QString& tex, qreal rootFontPixelSize, const QColor& color,
                          bool displayMode, qreal maxWidth = 0.0) const;
  MathLayoutResult render(const QString& tex, qreal rootFontPixelSize, const QColor& color,
                          bool displayMode, const MathSettings& settings,
                          qreal maxWidth = 0.0) const;
};

}  // namespace muffin::math
