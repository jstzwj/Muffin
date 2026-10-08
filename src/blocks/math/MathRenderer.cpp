#include "blocks/math/MathRenderer.h"

namespace muffin {

MathRenderPlaceholder MathRenderer::renderBlockPlaceholder(const QString& tex, const RenderTheme& theme) const {
  MathRenderPlaceholder placeholder;
  placeholder.displayText = tex;
  placeholder.font = theme.mathFont();
  placeholder.padding = theme.elementBoxStyle(QStringLiteral("pre")).padding;
  return placeholder;
}

}  // namespace muffin
