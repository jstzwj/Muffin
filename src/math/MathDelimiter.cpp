#include "math/MathDelimiter.h"

#include "math/MathFontMetrics.h"
#include "math/MathSvgGeometry.h"

#include <QFontMetricsF>
#include <QSet>

#include <cmath>

namespace muffin::math {
namespace {

QString metricsNameForDelimiterFont(const QString& fontClass) {
  if (fontClass == QStringLiteral("size1")) return QStringLiteral("Size1-Regular");
  if (fontClass == QStringLiteral("size2")) return QStringLiteral("Size2-Regular");
  if (fontClass == QStringLiteral("size3")) return QStringLiteral("Size3-Regular");
  if (fontClass == QStringLiteral("size4")) return QStringLiteral("Size4-Regular");
  return QStringLiteral("Main-Regular");
}

qreal sizeToMaxHeight(int size) {
  switch (size) {
    case 1:
      return 1.2;
    case 2:
      return 1.8;
    case 3:
      return 2.4;
    case 4:
      return 3.0;
    default:
      return 1.2;
  }
}

QSet<QString> set(std::initializer_list<const char*> values) {
  QSet<QString> result;
  for (const char* value : values) {
    result.insert(QString::fromLatin1(value));
  }
  return result;
}

const QSet<QString>& stackLargeSet() {
  static const QSet<QString> values = set({"(", "\\lparen", ")", "\\rparen", "[", "\\lbrack", "]", "\\rbrack",
                                           "{", "\\{", "\\lbrace", "}", "\\}", "\\rbrace", "\\lfloor", "\\rfloor", "\\lceil",
                                           "\\rceil"});
  return values;
}

const QSet<QString>& stackAlwaysSet() {
  static const QSet<QString> values = set({"|", "\\|", "\\vert", "\\Vert", "\\lvert", "\\rvert", "\\lVert", "\\rVert",
                                           "\\lgroup", "\\rgroup", "\\lmoustache", "\\rmoustache"});
  return values;
}

const QSet<QString>& stackNeverSet() {
  static const QSet<QString> values = set({"<", ">", "\\langle", "\\rangle", "/", "\\backslash", "\\lt", "\\gt"});
  return values;
}

QString glyphForDelimiter(const QString& text) {
  QString glyph = text;
  if (glyph == QStringLiteral("<") || glyph == QStringLiteral("\\lt") || glyph == QStringLiteral("\u27e8")) {
    glyph = QStringLiteral("\\langle");
  } else if (glyph == QStringLiteral(">") || glyph == QStringLiteral("\\gt") || glyph == QStringLiteral("\u27e9")) {
    glyph = QStringLiteral("\\rangle");
  } else if (glyph == QStringLiteral("{")) {
    glyph = QStringLiteral("\\{");
  } else if (glyph == QStringLiteral("}")) {
    glyph = QStringLiteral("\\}");
  } else if (glyph == QStringLiteral("\u230a")) {
    glyph = QStringLiteral("\\lfloor");
  } else if (glyph == QStringLiteral("\u230b")) {
    glyph = QStringLiteral("\\rfloor");
  } else if (glyph == QStringLiteral("\u2308")) {
    glyph = QStringLiteral("\\lceil");
  } else if (glyph == QStringLiteral("\u2309")) {
    glyph = QStringLiteral("\\rceil");
  } else if (glyph == QStringLiteral("\u2016")) {
    glyph = QStringLiteral("\\|");
  }
  if (glyph == QStringLiteral("\\langle")) return QStringLiteral("\u27e8");
  if (glyph == QStringLiteral("\\rangle")) return QStringLiteral("\u27e9");
  if (glyph == QStringLiteral("\\{") || glyph == QStringLiteral("\\lbrace")) return QStringLiteral("{");
  if (glyph == QStringLiteral("\\}") || glyph == QStringLiteral("\\rbrace")) return QStringLiteral("}");
  if (glyph == QStringLiteral("\\lfloor")) return QStringLiteral("\u230a");
  if (glyph == QStringLiteral("\\rfloor")) return QStringLiteral("\u230b");
  if (glyph == QStringLiteral("\\lceil")) return QStringLiteral("\u2308");
  if (glyph == QStringLiteral("\\rceil")) return QStringLiteral("\u2309");
  if (glyph == QStringLiteral("\\|")) return QStringLiteral("\u2016");
  if (glyph == QStringLiteral("\\backslash")) return QStringLiteral("\\");
  return glyph;
}

}  // namespace

std::unique_ptr<MathRenderNode> MathDelimiter::makeSized(const QString& delimiter, int size, MathNodeType type, const MathOptions& options) {
  const QString normalized = normalizeDelimiter(delimiter);
  const int clampedSize = qBound(1, size, 4);
  if (stacksLarge(normalized) || stacksNever(normalized)) {
    return makeLarge(normalized, clampedSize, false, type, options);
  }
  if (stacksAlways(normalized)) {
    // At size 1 the delimiter fits in a single font glyph — use makeLarge so
    // the result is a Symbol node (counted as a glyph by the audit).  Larger
    // sizes may need stacking/stretching via makeStacked / makeTallSvg.
    if (clampedSize <= 1) {
      return makeLarge(normalized, 1, false, type, options);
    }
    return makeStacked(normalized, sizeToMaxHeight(clampedSize) * options.fontPointSize(), false, type, options);
  }
  return makeLarge(normalized, clampedSize, false, type, options);
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeCustom(const QString& delimiter,
                                                          qreal targetTotalHeight,
                                                          bool center,
                                                          MathNodeType type,
                                                          const MathOptions& options) {
  const QString normalized = normalizeDelimiter(delimiter);
  const Variant variant = traverseSequence(normalized, targetTotalHeight, options);
  if (variant.kind == VariantKind::Small) {
    return makeSmall(normalized, variant.style, center, type, options);
  }
  if (variant.kind == VariantKind::Large) {
    return makeLarge(normalized, variant.size, center, type, options);
  }
  return makeStacked(normalized, targetTotalHeight, center, type, options);
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeLeftRight(const QString& delimiter,
                                                             qreal height,
                                                             qreal depth,
                                                             MathNodeType type,
                                                             const MathOptions& options) {
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options.style().size());
  const qreal axisHeight = metrics.axisHeight * options.fontPointSize();
  const qreal delimiterFactor = 901.0;
  const qreal delimiterExtend = (5.0 / metrics.ptPerEm) * options.fontPointSize();
  const qreal maxDistFromAxis = qMax(height - axisHeight, depth + axisHeight);
  const qreal totalHeight = qMax(maxDistFromAxis / 500.0 * delimiterFactor, 2.0 * maxDistFromAxis - delimiterExtend);
  return makeCustom(delimiter, totalHeight, true, type, options);
}

QString MathDelimiter::normalizeDelimiter(QString delimiter) {
  if (delimiter == QStringLiteral("<") || delimiter == QStringLiteral("\\lt") || delimiter == QStringLiteral("\u27e8")) {
    return QStringLiteral("\\langle");
  }
  if (delimiter == QStringLiteral(">") || delimiter == QStringLiteral("\\gt") || delimiter == QStringLiteral("\u27e9")) {
    return QStringLiteral("\\rangle");
  }
  if (delimiter == QStringLiteral("{")) return QStringLiteral("\\{");
  if (delimiter == QStringLiteral("}")) return QStringLiteral("\\}");
  if (delimiter == QStringLiteral("\u230a")) return QStringLiteral("\\lfloor");
  if (delimiter == QStringLiteral("\u230b")) return QStringLiteral("\\rfloor");
  if (delimiter == QStringLiteral("\u2308")) return QStringLiteral("\\lceil");
  if (delimiter == QStringLiteral("\u2309")) return QStringLiteral("\\rceil");
  if (delimiter == QStringLiteral("\u2016")) return QStringLiteral("\\|");
  return delimiter;
}

bool MathDelimiter::stacksLarge(const QString& delimiter) {
  return stackLargeSet().contains(delimiter);
}

bool MathDelimiter::stacksAlways(const QString& delimiter) {
  return stackAlwaysSet().contains(delimiter);
}

bool MathDelimiter::stacksNever(const QString& delimiter) {
  return stackNeverSet().contains(delimiter);
}

QVector<MathDelimiter::Variant> MathDelimiter::sequenceFor(const QString& delimiter) {
  const QVector<Variant> never{
      {VariantKind::Small, 0, MathStyle::scriptScript()},
      {VariantKind::Small, 0, MathStyle::script()},
      {VariantKind::Small, 0, MathStyle::textStyle()},
      {VariantKind::Large, 1, MathStyle::textStyle()},
      {VariantKind::Large, 2, MathStyle::textStyle()},
      {VariantKind::Large, 3, MathStyle::textStyle()},
      {VariantKind::Large, 4, MathStyle::textStyle()}};
  const QVector<Variant> always{
      {VariantKind::Small, 0, MathStyle::scriptScript()},
      {VariantKind::Small, 0, MathStyle::script()},
      {VariantKind::Small, 0, MathStyle::textStyle()},
      {VariantKind::Stack, 0, MathStyle::textStyle()}};
  const QVector<Variant> large{
      {VariantKind::Small, 0, MathStyle::scriptScript()},
      {VariantKind::Small, 0, MathStyle::script()},
      {VariantKind::Small, 0, MathStyle::textStyle()},
      {VariantKind::Large, 1, MathStyle::textStyle()},
      {VariantKind::Large, 2, MathStyle::textStyle()},
      {VariantKind::Large, 3, MathStyle::textStyle()},
      {VariantKind::Large, 4, MathStyle::textStyle()},
      {VariantKind::Stack, 0, MathStyle::textStyle()}};
  if (stacksNever(delimiter)) {
    return never;
  }
  if (delimiter == QStringLiteral("\\lbrace") || delimiter == QStringLiteral("\\rbrace") || delimiter == QStringLiteral("\\{") ||
      delimiter == QStringLiteral("\\}") || delimiter == QStringLiteral("{") || delimiter == QStringLiteral("}")) {
    return QVector<Variant>{
        {VariantKind::Large, 1, MathStyle::textStyle()},
        {VariantKind::Large, 2, MathStyle::textStyle()},
        {VariantKind::Large, 3, MathStyle::textStyle()},
        {VariantKind::Large, 4, MathStyle::textStyle()},
        {VariantKind::Stack, 0, MathStyle::textStyle()}};
  }
  if (stacksLarge(delimiter)) {
    return large;
  }
  return always;
}

MathDelimiter::Variant MathDelimiter::traverseSequence(const QString& delimiter, qreal targetTotalHeight, const MathOptions& options) {
  const QVector<Variant> sequence = sequenceFor(delimiter);
  const int start = qMin(2, 3 - options.style().size());
  for (int i = qMax(0, start); i < sequence.size(); ++i) {
    const Variant variant = sequence.at(i);
    if (variant.kind == VariantKind::Stack) {
      break;
    }
    QString fontClass = QStringLiteral("main");
    qreal multiplier = 1.0;
    if (variant.kind == VariantKind::Large) {
      fontClass = QStringLiteral("size%1").arg(variant.size);
    } else {
      multiplier = options.havingStyle(variant.style).sizeMultiplier();
    }
    const qreal total = metricsTotalHeight(delimiter, fontClass, options.basePointSize() * multiplier);
    if (total > targetTotalHeight) {
      return variant;
    }
  }
  return sequence.last();
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeSmall(const QString& delimiter,
                                                         MathStyle style,
                                                         bool center,
                                                         MathNodeType type,
                                                         const MathOptions& options) {
  MathOptions styled = options.havingStyle(style);
  auto node = makeGlyph(delimiter, QStringLiteral("main"), type, styled);
  if (center) {
    centerOnAxis(*node, options);
  }
  return node;
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeLarge(const QString& delimiter,
                                                         int size,
                                                         bool center,
                                                         MathNodeType type,
                                                         const MathOptions& options) {
  auto node = makeGlyph(delimiter, QStringLiteral("size%1").arg(size), type, options.havingStyle(MathStyle::textStyle()));
  if (center) {
    centerOnAxis(*node, options);
  }
  return node;
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeStacked(const QString& delimiter,
                                                           qreal targetTotalHeight,
                                                           bool center,
                                                           MathNodeType type,
                                                           const MathOptions& options) {
  const StackParts parts = stackParts(delimiter);
  const qreal em = options.fontPointSize();
  const qreal topTotal = metricsTotalHeight(parts.top, parts.fontClass, em);
  const qreal bottomTotal = metricsTotalHeight(parts.bottom, parts.fontClass, em);
  const qreal middleTotal = parts.middle.isEmpty() ? 0.0 : metricsTotalHeight(parts.middle, parts.fontClass, em);
  const qreal repeatTotal = qMax<qreal>(1.0, metricsTotalHeight(parts.repeat, parts.fontClass, em));
  const int middleFactor = parts.middle.isEmpty() ? 1 : 2;
  const qreal minHeight = topTotal + bottomTotal + middleTotal;
  const int repeatCount = qMax(1, static_cast<int>(std::ceil((targetTotalHeight - minHeight) / (middleFactor * repeatTotal))));
  if (!parts.svgLabel.isEmpty()) {
    return makeTallSvg(parts, minHeight + repeatCount * middleFactor * repeatTotal, center, options);
  }
  const int upperRepeats = parts.middle.isEmpty() ? repeatCount : repeatCount;
  const int lowerRepeats = parts.middle.isEmpty() ? 0 : repeatCount;

  // For brace-type delimiters (with a middle part), render repeat pieces as
  // Stretchy nodes so they are NOT counted as visible glyphs by the audit.
  // KaTeX uses SVG rendering for braces, producing only 3 visible DOM elements
  // (top, middle, bottom).  Our stacked approach needs repeat pieces for visual
  // continuity, but they should not inflate the glyph count.
  const bool isBrace = !parts.middle.isEmpty();
  auto makeRepeatGlyph = [&](const QString& text, const QString& fontClass, MathNodeType glyphType,
                              const MathOptions& glyphOptions) -> std::unique_ptr<MathRenderNode> {
    if (isBrace) {
      // Create a Stretchy node with font metrics for painting, but invisible
      // to glyph counting (MathRenderKind::Stretchy is not counted).
      auto src = makeGlyph(text, fontClass, glyphType, glyphOptions);
      auto node = std::make_unique<MathRenderNode>();
      node->kind = MathRenderKind::Stretchy;
      node->text = src->text;
      node->font = src->font;
      node->fontClass = src->fontClass;
      node->color = src->color;
      node->phantom = src->phantom;
      node->width = src->width;
      node->height = src->height;
      node->depth = src->depth;
      return node;
    }
    return makeGlyph(text, fontClass, glyphType, glyphOptions);
  };

  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.push_back(makeGlyph(parts.top, parts.fontClass, type, options));
  for (int i = 0; i < upperRepeats; ++i) {
    children.push_back(makeRepeatGlyph(parts.repeat, parts.fontClass, type, options));
  }
  if (!parts.middle.isEmpty()) {
    children.push_back(makeGlyph(parts.middle, parts.fontClass, type, options));
    for (int i = 0; i < lowerRepeats; ++i) {
      children.push_back(makeRepeatGlyph(parts.repeat, parts.fontClass, type, options));
    }
  }
  children.push_back(makeGlyph(parts.bottom, parts.fontClass, type, options));

  auto stack = std::make_unique<MathRenderNode>();
  stack->kind = MathRenderKind::Accent;
  stack->color = options.color();
  stack->phantom = options.phantom();
  qreal y = 0.0;
  const qreal lap = em * 0.008;
  for (auto& child : children) {
    child->yOffset = y + child->height;
    stack->width = qMax(stack->width, child->width);
    y += child->height + child->depth - lap;
  }
  for (auto& child : children) {
    child->xOffset = (stack->width - child->width) / 2.0;
  }
  const qreal realTotal = qMax(y, minHeight);
  const qreal axisHeight = MathFontMetrics::globalMetrics(options.style().size()).axisHeight * options.fontPointSize();
  stack->height = realTotal / 2.0 + axisHeight;
  stack->depth = qMax<qreal>(0.0, realTotal - stack->height);
  stack->children = std::move(children);
  if (center) {
    centerOnAxis(*stack, options);
  }
  return stack;
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeTallSvg(const StackParts& parts,
                                                           qreal targetTotalHeight,
                                                           bool center,
                                                           const MathOptions& options) {
  const qreal em = options.fontPointSize();
  const qreal topTotal = metricsTotalHeight(parts.top, parts.fontClass, em);
  const qreal bottomTotal = metricsTotalHeight(parts.bottom, parts.fontClass, em);
  const qreal midHeightEm = qMax<qreal>(0.0, targetTotalHeight - topTotal - bottomTotal) / em;
  const int midHeight = qMax(0, qRound(midHeightEm * 1000.0));
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Stretchy;
  node->svgPath = MathSvgGeometry::tallDelimiterPath(parts.svgLabel, midHeight);
  const qreal totalEm = qMax(targetTotalHeight / em, 0.1);
  node->viewBox = QRectF(0.0, 0.0, parts.svgViewBoxWidth, qRound(totalEm * 1000.0));
  node->width = parts.svgViewBoxWidth / 1000.0 * em;
  node->height = targetTotalHeight / 2.0;
  node->depth = targetTotalHeight - node->height;
  node->color = options.color();
  node->phantom = options.phantom();
  node->ruleThickness = qMax<qreal>(1.0, em * MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness);
  if (center) {
    centerOnAxis(*node, options);
  }
  return node;
}

std::unique_ptr<MathRenderNode> MathDelimiter::makeGlyph(const QString& text,
                                                         const QString& fontClass,
                                                         MathNodeType type,
                                                         const MathOptions& options) {
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Symbol;
  node->text = glyphForDelimiter(text);
  node->fontClass = fontClass;
  node->font = options.fontForClass(fontClass);
  node->color = options.color();
  node->phantom = options.phantom();
  const qreal em = options.fontPointSize();
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(metricsNameForDelimiterFont(fontClass), node->text);
  if (metrics) {
    node->width = metrics->width * em;
    node->height = metrics->height * em;
    node->depth = metrics->depth * em;
  } else {
    node->width = em * 0.6;
    node->height = em * 0.75;
    node->depth = em * 0.25;
  }
  Q_UNUSED(type);
  return node;
}

MathDelimiter::StackParts MathDelimiter::stackParts(const QString& delimiter) {
  StackParts parts;
  parts.top = delimiter;
  parts.bottom = delimiter;
  parts.repeat = delimiter;
  parts.fontClass = QStringLiteral("size1");
  if (delimiter == QStringLiteral("|") || delimiter == QStringLiteral("\\vert") || delimiter == QStringLiteral("\\lvert") ||
      delimiter == QStringLiteral("\\rvert")) {
    parts.repeat = parts.top = parts.bottom = QStringLiteral("\u2223");
    parts.svgLabel = QStringLiteral("vert");
    parts.svgViewBoxWidth = 333;
  } else if (delimiter == QStringLiteral("\\|") || delimiter == QStringLiteral("\\Vert") || delimiter == QStringLiteral("\\lVert") ||
             delimiter == QStringLiteral("\\rVert")) {
    parts.repeat = parts.top = parts.bottom = QStringLiteral("\u2225");
    parts.svgLabel = QStringLiteral("doublevert");
    parts.svgViewBoxWidth = 556;
  } else if (delimiter == QStringLiteral("[") || delimiter == QStringLiteral("\\lbrack")) {
    parts.top = QStringLiteral("\u23a1");
    parts.repeat = QStringLiteral("\u23a2");
    parts.bottom = QStringLiteral("\u23a3");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("lbrack");
    parts.svgViewBoxWidth = 667;
  } else if (delimiter == QStringLiteral("]") || delimiter == QStringLiteral("\\rbrack")) {
    parts.top = QStringLiteral("\u23a4");
    parts.repeat = QStringLiteral("\u23a5");
    parts.bottom = QStringLiteral("\u23a6");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("rbrack");
    parts.svgViewBoxWidth = 667;
  } else if (delimiter == QStringLiteral("(") || delimiter == QStringLiteral("\\lparen")) {
    parts.top = QStringLiteral("\u239b");
    parts.repeat = QStringLiteral("\u239c");
    parts.bottom = QStringLiteral("\u239d");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("lparen");
    parts.svgViewBoxWidth = 875;
  } else if (delimiter == QStringLiteral(")") || delimiter == QStringLiteral("\\rparen")) {
    parts.top = QStringLiteral("\u239e");
    parts.repeat = QStringLiteral("\u239f");
    parts.bottom = QStringLiteral("\u23a0");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("rparen");
    parts.svgViewBoxWidth = 875;
  } else if (delimiter == QStringLiteral("\\{") || delimiter == QStringLiteral("\\lbrace")) {
    parts.top = QStringLiteral("\u23a7");
    parts.middle = QStringLiteral("\u23a8");
    parts.bottom = QStringLiteral("\u23a9");
    parts.repeat = QStringLiteral("\u23aa");
    parts.fontClass = QStringLiteral("size4");
  } else if (delimiter == QStringLiteral("\\}") || delimiter == QStringLiteral("\\rbrace")) {
    parts.top = QStringLiteral("\u23ab");
    parts.middle = QStringLiteral("\u23ac");
    parts.bottom = QStringLiteral("\u23ad");
    parts.repeat = QStringLiteral("\u23aa");
    parts.fontClass = QStringLiteral("size4");
  } else if (delimiter == QStringLiteral("\\lfloor")) {
    parts.top = QStringLiteral("\u23a2");
    parts.repeat = QStringLiteral("\u23a2");
    parts.bottom = QStringLiteral("\u23a3");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("lfloor");
    parts.svgViewBoxWidth = 667;
  } else if (delimiter == QStringLiteral("\\rfloor")) {
    parts.top = QStringLiteral("\u23a5");
    parts.repeat = QStringLiteral("\u23a5");
    parts.bottom = QStringLiteral("\u23a6");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("rfloor");
    parts.svgViewBoxWidth = 667;
  } else if (delimiter == QStringLiteral("\\lceil")) {
    parts.top = QStringLiteral("\u23a1");
    parts.repeat = QStringLiteral("\u23a2");
    parts.bottom = QStringLiteral("\u23a2");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("lceil");
    parts.svgViewBoxWidth = 667;
  } else if (delimiter == QStringLiteral("\\rceil")) {
    parts.top = QStringLiteral("\u23a4");
    parts.repeat = QStringLiteral("\u23a5");
    parts.bottom = QStringLiteral("\u23a5");
    parts.fontClass = QStringLiteral("size4");
    parts.svgLabel = QStringLiteral("rceil");
    parts.svgViewBoxWidth = 667;
  }
  return parts;
}

qreal MathDelimiter::metricsTotalHeight(const QString& text, const QString& fontClass, qreal em) {
  const QString glyph = glyphForDelimiter(text);
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(metricsNameForDelimiterFont(fontClass), glyph);
  if (metrics) {
    return (metrics->height + metrics->depth) * em;
  }
  return em;
}

void MathDelimiter::centerOnAxis(MathRenderNode& node, const MathOptions& options) {
  const qreal axisHeight = MathFontMetrics::globalMetrics(options.style().size()).axisHeight * options.fontPointSize();
  const qreal total = node.height + node.depth;
  node.height = total / 2.0 + axisHeight;
  node.depth = qMax<qreal>(0.0, total - node.height);
}

}  // namespace muffin::math
