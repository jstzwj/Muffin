#include "math/MathRenderer.h"

#include "math/MathBuilder.h"
#include "math/MathFontRegistry.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"

namespace muffin::math {

MathLayoutResult MathRenderer::render(const QString& tex, const RenderTheme& theme, bool displayMode, qreal maxWidth) const {
  Q_UNUSED(maxWidth);
  MathFontRegistry::ensureLoaded();

  MathSettings settings;
  settings.displayMode = displayMode;
  MathLayoutResult result;
  result.source = tex;
  QVector<MathParseNode> tree;
  try {
    MathParser parser(tex, settings);
    tree = parser.parse();
  } catch (const MathParseError& error) {
    result.error = error.message();
    MathParseNode errorNode;
    errorNode.type = MathNodeType::Error;
    errorNode.text = result.error;
    tree.push_back(std::move(errorNode));
  }

  const MathStyle style = displayMode ? MathStyle::display() : MathStyle::textStyle();
  MathOptions options(style, theme.mathFont().pointSizeF(), theme.textColor(), settings);
  MathBuilder builder(options);

  result.root = builder.buildExpression(tree);
  if (!result.root) {
    return result;
  }
  const qreal horizontalPadding = displayMode ? theme.codePadding().left() + theme.codePadding().right() : 0.0;
  const qreal verticalPadding = displayMode ? theme.codePadding().top() + theme.codePadding().bottom() : 0.0;
  result.baseline = result.root->height + (displayMode ? theme.codePadding().top() : 0.0);
  result.size = QSizeF(result.root->width + horizontalPadding, result.root->height + result.root->depth + verticalPadding);
  return result;
}

}  // namespace muffin::math
