#pragma once

#include "math/MathOptions.h"
#include "math/MathParseNode.h"
#include "math/MathRenderNode.h"

namespace muffin::math {

class MathDelimiter {
public:
  static std::unique_ptr<MathRenderNode> makeSized(const QString& delimiter, int size, MathNodeType type, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeCustom(const QString& delimiter, qreal targetTotalHeight, bool center, MathNodeType type, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeLeftRight(const QString& delimiter, qreal height, qreal depth, MathNodeType type, const MathOptions& options);

private:
  enum class VariantKind {
    Small,
    Large,
    Stack
  };

  struct Variant {
    VariantKind kind = VariantKind::Small;
    int size = 0;
    MathStyle style = MathStyle::textStyle();
  };

  struct StackParts {
    QString top;
    QString middle;
    QString bottom;
    QString repeat;
    QString fontClass;
    QString svgLabel;
    int svgViewBoxWidth = 0;
  };

  static QString normalizeDelimiter(QString delimiter);
  static bool stacksLarge(const QString& delimiter);
  static bool stacksAlways(const QString& delimiter);
  static bool stacksNever(const QString& delimiter);
  static QVector<Variant> sequenceFor(const QString& delimiter);
  static Variant traverseSequence(const QString& delimiter, qreal targetTotalHeight, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeSmall(const QString& delimiter, MathStyle style, bool center, MathNodeType type, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeLarge(const QString& delimiter, int size, bool center, MathNodeType type, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeStacked(const QString& delimiter, qreal targetTotalHeight, bool center, MathNodeType type, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeTallSvg(const StackParts& parts, qreal targetTotalHeight, bool center, const MathOptions& options);
  static std::unique_ptr<MathRenderNode> makeGlyph(const QString& text, const QString& fontClass, MathNodeType type, const MathOptions& options);
  static StackParts stackParts(const QString& delimiter);
  static qreal metricsTotalHeight(const QString& text, const QString& fontClass, qreal em);
  static void centerOnAxis(MathRenderNode& node, const MathOptions& options);
};

}  // namespace muffin::math
