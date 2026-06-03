#pragma once

#include "math/MathOptions.h"
#include "math/MathParseNode.h"
#include "math/MathRenderNode.h"

namespace muffin::math {

class MathBuilder {
public:
  explicit MathBuilder(MathOptions options);

  std::unique_ptr<MathRenderNode> buildExpression(const QVector<MathParseNode>& expression);
  std::unique_ptr<MathRenderNode> buildNode(const MathParseNode& node);

private:
  std::unique_ptr<MathRenderNode> makeSymbol(const QString& text, MathNodeType type, const MathOptions& options, const QString& forcedFontClass = {});
  std::unique_ptr<MathRenderNode> makeSpan(std::vector<std::unique_ptr<MathRenderNode>> children);
  std::unique_ptr<MathRenderNode> makeSupSub(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeFraction(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeSqrt(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeAccent(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeAccentUnder(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeHorizBrace(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeUnderline(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeOverline(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makePhantom(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeSmash(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeRuleNode(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeKern(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeLap(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeRaiseBox(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeVCenter(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeEnclose(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeIncludeGraphics(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeMathChoice(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeTag(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeVerb(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeDelimSizing(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeLeftRight(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeArray(const MathParseNode& node);
  std::unique_ptr<MathRenderNode> makeDelimiter(const QString& delimiter, qreal targetHeight, MathNodeType type);
  std::unique_ptr<MathRenderNode> makeStretchyAccent(const MathParseNode& node, const MathRenderNode& body);
  std::unique_ptr<MathRenderNode> makeBraceGlyph(const MathParseNode& node, const MathRenderNode& body);
  std::unique_ptr<MathRenderNode> makeError(const QString& text, const MathOptions& options);
  qreal dimensionToPoints(const QString& value) const;
  qreal spacingAfter(int previous, int current) const;

  MathOptions options_;
};

}  // namespace muffin::math
