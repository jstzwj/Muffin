#include "blocks/math/MathRenderer.h"

namespace muffin {

MathRenderPlaceholder MathRenderer::renderBlockPlaceholder(const QString& tex, const RenderTheme& theme) const {
  MathRenderPlaceholder placeholder;
  placeholder.displayText = tex;
  placeholder.font = theme.mathFont();
  placeholder.padding = theme.codePadding();
  return placeholder;
}

}  // namespace muffin
