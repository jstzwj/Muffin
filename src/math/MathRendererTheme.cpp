#include "math/MathRenderer.h"

#include "theme/RenderTheme.h"

namespace muffin::math {
namespace {
constexpr qreal kKatexRootFontScale = 1.21;
constexpr qreal kCssPixelsPerPoint = 96.0 / 72.0;
}

qreal MathRenderer::katexRootFontPixelSize(const RenderTheme& theme) {
  return theme.mathFont().pointSizeF() * kCssPixelsPerPoint * kKatexRootFontScale;
}

MathLayoutResult MathRenderer::render(const QString& tex, const RenderTheme& theme,
                                      bool displayMode, qreal maxWidth) const {
  MathSettings settings;
  settings.displayMode = displayMode;
  return render(tex, theme, displayMode, settings, maxWidth);
}

MathLayoutResult MathRenderer::render(const QString& tex, const RenderTheme& theme,
                                      bool displayMode, const MathSettings& settings,
                                      qreal maxWidth) const {
  return render(tex, katexRootFontPixelSize(theme), theme.textColor(),
                displayMode, settings, maxWidth);
}

}  // namespace muffin::math
