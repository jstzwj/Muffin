#include "math/MathBuilder.h"

#include "math/MathDelimiter.h"
#include "math/MathFontMetrics.h"
#include "math/MathSvgGeometry.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>

#include <memory>
#include <vector>

namespace muffin::math {
namespace {

enum class MathAtomClass {
  Leftmost = -2,
  Rightmost = -1,
  Ord = 0,
  Op = 1,
  Bin = 2,
  Rel = 3,
  Open = 4,
  Close = 5,
  Punct = 6,
  Inner = 7
};

MathAtomClass atomClassForNode(const MathParseNode& node) {
  if (node.type == MathNodeType::SupSub && !node.base.isEmpty()) {
    return atomClassForNode(node.base.first());
  }
  if (node.type == MathNodeType::Group) {
    return MathAtomClass::Ord;
  }
  if (node.type == MathNodeType::Class) {
    if (node.mathClass == QStringLiteral("\\mathbin")) return MathAtomClass::Bin;
    if (node.mathClass == QStringLiteral("\\mathrel")) return MathAtomClass::Rel;
    if (node.mathClass == QStringLiteral("\\mathopen")) return MathAtomClass::Open;
    if (node.mathClass == QStringLiteral("\\mathclose")) return MathAtomClass::Close;
    if (node.mathClass == QStringLiteral("\\mathpunct")) return MathAtomClass::Punct;
    if (node.mathClass == QStringLiteral("\\mathinner")) return MathAtomClass::Inner;
    return MathAtomClass::Ord;
  }
  if (node.type == MathNodeType::LeftRight || node.type == MathNodeType::Array || node.type == MathNodeType::HorizBrace) {
    return MathAtomClass::Inner;
  }
  switch (node.type) {
    case MathNodeType::Operator:
      return MathAtomClass::Op;
    case MathNodeType::Binary:
      return MathAtomClass::Bin;
    case MathNodeType::Relation:
      return MathAtomClass::Rel;
    case MathNodeType::Open:
      return MathAtomClass::Open;
    case MathNodeType::Close:
      return MathAtomClass::Close;
    case MathNodeType::Punct:
      return MathAtomClass::Punct;
    default:
      return MathAtomClass::Ord;
  }
}

bool binLeftCancels(MathAtomClass previous) {
  return previous == MathAtomClass::Leftmost || previous == MathAtomClass::Bin || previous == MathAtomClass::Open ||
         previous == MathAtomClass::Rel || previous == MathAtomClass::Op || previous == MathAtomClass::Punct;
}

bool binRightCancels(MathAtomClass next) {
  return next == MathAtomClass::Rightmost || next == MathAtomClass::Rel || next == MathAtomClass::Close || next == MathAtomClass::Punct;
}

qreal axisHeight(const MathOptions& options) {
  return options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).axisHeight;
}

qreal ruleThickness(const MathOptions& options) {
  return qMax<qreal>(1.0, options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness);
}

QString fontMetricsNameForClass(const QString& fontClass) {
  if (fontClass == QStringLiteral("mathit")) return QStringLiteral("Math-Italic");
  if (fontClass == QStringLiteral("amsrm")) return QStringLiteral("AMS-Regular");
  if (fontClass == QStringLiteral("mathbf")) return QStringLiteral("Main-Bold");
  if (fontClass == QStringLiteral("size1")) return QStringLiteral("Size1-Regular");
  if (fontClass == QStringLiteral("size2")) return QStringLiteral("Size2-Regular");
  if (fontClass == QStringLiteral("size3")) return QStringLiteral("Size3-Regular");
  if (fontClass == QStringLiteral("size4")) return QStringLiteral("Size4-Regular");
  return QStringLiteral("Main-Regular");
}

qreal italicCorrection(const MathRenderNode& node, const MathOptions& options) {
  if (node.italic != 0.0) {
    return node.italic;
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->italic * options.fontPointSize() : 0.0;
}

bool operatorCanGrow(const MathParseNode& node) {
  return node.opSymbol && node.label != QStringLiteral("\\smallint");
}

bool operatorUsesLimits(const MathParseNode& node, const MathOptions& options) {
  if (node.explicitLimits) {
    return node.limits;
  }
  return node.limits && (options.style().id() == MathStyle::Display || node.alwaysHandleSupSub);
}

}  // namespace

MathBuilder::MathBuilder(MathOptions options) : options_(std::move(options)) {}

std::unique_ptr<MathRenderNode> MathBuilder::buildExpression(const QVector<MathParseNode>& expression) {
  std::vector<std::unique_ptr<MathRenderNode>> children;
  QVector<MathAtomClass> classes;
  classes.reserve(expression.size());
  for (const MathParseNode& node : expression) {
    classes.push_back(atomClassForNode(node));
  }
  for (int i = 0; i < classes.size(); ++i) {
    if (classes.at(i) != MathAtomClass::Bin) {
      continue;
    }
    const MathAtomClass previous = i == 0 ? MathAtomClass::Leftmost : classes.at(i - 1);
    const MathAtomClass next = i + 1 >= classes.size() ? MathAtomClass::Rightmost : classes.at(i + 1);
    if (binLeftCancels(previous) || binRightCancels(next)) {
      classes[i] = MathAtomClass::Ord;
    }
  }

  for (int i = 0; i < expression.size(); ++i) {
    const MathParseNode& node = expression.at(i);
    const MathAtomClass current = classes.at(i);
    if (i > 0) {
      const qreal spacing = spacingAfter(static_cast<int>(classes.at(i - 1)), static_cast<int>(current));
      if (spacing > 0.0) {
        auto glue = std::make_unique<MathRenderNode>();
        glue->kind = MathRenderKind::Span;
        glue->width = spacing * options_.fontPointSize();
        children.push_back(std::move(glue));
      }
    }
    children.push_back(buildNode(node));
  }
  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::buildNode(const MathParseNode& node) {
  switch (node.type) {
    case MathNodeType::Group:
      return buildExpression(node.body);
    case MathNodeType::SupSub:
      return makeSupSub(node);
    case MathNodeType::Fraction:
      return makeFraction(node);
    case MathNodeType::Sqrt:
      return makeSqrt(node);
    case MathNodeType::Accent:
      return makeAccent(node);
    case MathNodeType::AccentUnder:
      return makeAccentUnder(node);
    case MathNodeType::HorizBrace:
      return makeHorizBrace(node);
    case MathNodeType::Underline:
      return makeUnderline(node);
    case MathNodeType::Overline:
      return makeOverline(node);
    case MathNodeType::Phantom:
      return makePhantom(node);
    case MathNodeType::Smash:
      return makeSmash(node);
    case MathNodeType::Rule:
      return makeRuleNode(node);
    case MathNodeType::Kern:
      return makeKern(node);
    case MathNodeType::Lap:
      return makeLap(node);
    case MathNodeType::RaiseBox:
      return makeRaiseBox(node);
    case MathNodeType::VCenter:
      return makeVCenter(node);
    case MathNodeType::Enclose:
      return makeEnclose(node);
    case MathNodeType::IncludeGraphics:
      return makeIncludeGraphics(node);
    case MathNodeType::MathChoice:
      return makeMathChoice(node);
    case MathNodeType::Href:
    case MathNodeType::Html:
      return node.body.isEmpty() ? makeSymbol(node.text, MathNodeType::Text, options_) : buildExpression(node.body);
    case MathNodeType::Tag:
      return makeTag(node);
    case MathNodeType::Verb:
      return makeVerb(node);
    case MathNodeType::DelimSizing:
      return makeDelimSizing(node);
    case MathNodeType::LeftRight:
      return makeLeftRight(node);
    case MathNodeType::Array:
      return makeArray(node);
    case MathNodeType::Color: {
      QColor color(node.color);
      if (!color.isValid()) {
        static const QHash<QString, QColor> namedColors{
            {QStringLiteral("red"), QColor(QStringLiteral("#df0030"))},
            {QStringLiteral("blue"), QColor(QStringLiteral("#0057d8"))},
            {QStringLiteral("green"), QColor(QStringLiteral("#008000"))},
            {QStringLiteral("black"), QColor(QStringLiteral("#000000"))},
            {QStringLiteral("white"), QColor(QStringLiteral("#ffffff"))},
            {QStringLiteral("gray"), QColor(QStringLiteral("#808080"))},
            {QStringLiteral("grey"), QColor(QStringLiteral("#808080"))},
            {QStringLiteral("purple"), QColor(QStringLiteral("#800080"))},
            {QStringLiteral("orange"), QColor(QStringLiteral("#ff8000"))}};
        color = namedColors.value(node.color, options_.color());
      }
      return MathBuilder(options_.withColor(color)).buildExpression(node.body);
    }
    case MathNodeType::Styling: {
      MathStyle style = options_.style();
      if (node.style == QStringLiteral("\\displaystyle")) style = MathStyle::display();
      else if (node.style == QStringLiteral("\\textstyle")) style = MathStyle::textStyle();
      else if (node.style == QStringLiteral("\\scriptstyle")) style = MathStyle::script();
      else if (node.style == QStringLiteral("\\scriptscriptstyle")) style = MathStyle::scriptScript();
      return MathBuilder(options_.havingStyle(style)).buildExpression(node.body);
    }
    case MathNodeType::Sizing: {
      static const QHash<QString, qreal> scales{
          {QStringLiteral("\\tiny"), 0.5},       {QStringLiteral("\\sixptsize"), 0.55}, {QStringLiteral("\\scriptsize"), 0.7},
          {QStringLiteral("\\footnotesize"), 0.8}, {QStringLiteral("\\small"), 0.9},      {QStringLiteral("\\normalsize"), 1.0},
          {QStringLiteral("\\large"), 1.2},      {QStringLiteral("\\Large"), 1.44},      {QStringLiteral("\\LARGE"), 1.73},
          {QStringLiteral("\\huge"), 2.07},      {QStringLiteral("\\Huge"), 2.49}};
      return MathBuilder(options_.havingSizeScale(scales.value(node.size, 1.0))).buildExpression(node.body);
    }
    case MathNodeType::Class:
      if (!node.fontClass.isEmpty()) {
        std::vector<std::unique_ptr<MathRenderNode>> children;
        for (const MathParseNode& childNode : node.body) {
          if (!childNode.text.isEmpty()) {
            children.push_back(makeSymbol(childNode.text, MathNodeType::Text, options_, node.fontClass));
          } else {
            children.push_back(buildNode(childNode));
          }
        }
        return makeSpan(std::move(children));
      }
      return buildExpression(node.body);
    case MathNodeType::Error:
      return makeError(node.text, options_);
    case MathNodeType::Text:
      if (!node.body.isEmpty()) {
        std::vector<std::unique_ptr<MathRenderNode>> children;
        for (const MathParseNode& childNode : node.body) {
          if (childNode.text.isEmpty() && childNode.body.isEmpty()) {
            children.push_back(buildNode(childNode));
          } else if (!childNode.text.isEmpty()) {
            children.push_back(makeSymbol(childNode.text, MathNodeType::Text, options_, node.fontClass));
          } else {
            children.push_back(MathBuilder(options_).buildExpression(childNode.body));
          }
        }
        return makeSpan(std::move(children));
      }
      return makeSymbol(node.text, MathNodeType::Text, options_, node.fontClass);
    case MathNodeType::Operator:
      if (!node.body.isEmpty()) {
        auto opBody = buildExpression(node.body);
        opBody->fontClass = QStringLiteral("main");
        return opBody;
      }
      if (node.opSymbol) {
        const bool large = operatorCanGrow(node) && options_.style().id() == MathStyle::Display;
        auto op = makeSymbol(node.text, node.type, options_, large ? QStringLiteral("size2") : QStringLiteral("size1"));
        const qreal shift = (op->height - op->depth) / 2.0 - axisHeight(options_);
        if (!qFuzzyIsNull(shift)) {
          op->shift = -shift;
        }
        return op;
      }
      return makeSymbol(node.text, node.type, options_, QStringLiteral("main"));
    default:
      return makeSymbol(node.text, node.type, options_);
  }
}

std::unique_ptr<MathRenderNode> MathBuilder::makeUnderline(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  auto line = std::make_unique<MathRenderNode>();
  line->kind = MathRenderKind::Rule;
  line->width = body->width;
  line->ruleThickness = ruleThickness(options_);
  line->color = options_.color();
  line->xOffset = 0.0;
  line->yOffset = body->depth + options_.fontPointSize() * 0.16;

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = body->height;
  result->depth = body->depth + options_.fontPointSize() * 0.22;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(line));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeOverline(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  const qreal rule = ruleThickness(options_);
  const qreal gap = 3.0 * rule;
  auto line = std::make_unique<MathRenderNode>();
  line->kind = MathRenderKind::Rule;
  line->width = body->width;
  line->ruleThickness = rule;
  line->color = options_.color();
  line->xOffset = 0.0;
  line->yOffset = -(body->height + gap);

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = body->height + gap + rule;
  result->depth = body->depth;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(line));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makePhantom(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Phantom;
  result->width = node.label == QStringLiteral("\\vphantom") ? 0.0 : body->width;
  result->height = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->height;
  result->depth = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->depth;
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSmash(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = node.smashHeight ? 0.0 : body->height;
  result->depth = node.smashDepth ? 0.0 : body->depth;
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeRuleNode(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Rect;
  result->width = dimensionToPoints(node.width);
  result->ruleThickness = dimensionToPoints(node.height);
  result->shift = node.shift.isEmpty() ? 0.0 : dimensionToPoints(node.shift);
  result->height = qMax<qreal>(0.0, result->ruleThickness + result->shift);
  result->depth = qMax<qreal>(0.0, -result->shift);
  result->color = options_.color();
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeKern(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Span;
  result->width = dimensionToPoints(node.width);
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeLap(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = 0.0;
  result->height = body->height;
  result->depth = body->depth;
  if (node.label == QStringLiteral("\\mathllap")) {
    body->xOffset = -body->width;
  } else if (node.label == QStringLiteral("\\mathclap")) {
    body->xOffset = -body->width / 2.0;
  }
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeRaiseBox(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const qreal shift = dimensionToPoints(node.shift);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  body->yOffset = -shift;
  result->height = qMax<qreal>(0.0, body->height + shift);
  result->depth = qMax<qreal>(0.0, body->depth - shift);
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeVCenter(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const qreal axis = axisHeight(options_);
  const qreal dy = 0.5 * ((body->height - axis) - (body->depth + axis));
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  body->yOffset = dy;
  result->height = qMax<qreal>(0.0, body->height - dy);
  result->depth = qMax<qreal>(0.0, body->depth + dy);
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeEnclose(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const qreal pad = options_.fontPointSize() * 0.18;
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width + pad * 2.0;
  result->height = body->height + pad;
  result->depth = body->depth + pad;
  body->xOffset = pad;
  result->children.push_back(std::move(body));

  if (node.label == QStringLiteral("\\fbox") || node.label == QStringLiteral("\\colorbox") || node.label == QStringLiteral("\\fcolorbox")) {
    auto bg = std::make_unique<MathRenderNode>();
    bg->kind = MathRenderKind::Rect;
    bg->width = result->width;
    bg->height = result->height;
    bg->depth = result->depth;
    QColor background(node.backgroundColor);
    bg->color = background.isValid() ? background : QColor(255, 255, 255, 0);
    bg->xOffset = 0.0;
    bg->yOffset = 0.0;
    result->children.insert(result->children.begin(), std::move(bg));
  }
  auto overlay = std::make_unique<MathRenderNode>();
  overlay->kind = MathRenderKind::Stretchy;
  overlay->text = node.label;
  overlay->width = result->width;
  overlay->height = result->height;
  overlay->depth = result->depth;
  overlay->ruleThickness = ruleThickness(options_);
  QColor border(node.borderColor);
  overlay->color = border.isValid() ? border : options_.color();
  overlay->xOffset = 0.0;
  overlay->yOffset = 0.0;
  result->children.push_back(std::move(overlay));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeIncludeGraphics(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Rect;
  result->imageSource = node.href;
  result->text = node.alt.isEmpty() ? QFileInfo(node.href).baseName() : node.alt;
  result->font = options_.fontForClass(QStringLiteral("main"));
  qreal height = node.height.isEmpty() ? options_.fontPointSize() * 0.9 : dimensionToPoints(node.height);
  qreal total = node.totalHeight.isEmpty() ? height : dimensionToPoints(node.totalHeight);
  result->width = node.width.isEmpty() ? qMax<qreal>(height, options_.fontPointSize()) : dimensionToPoints(node.width);
  result->ruleThickness = height;
  result->height = height;
  result->depth = qMax<qreal>(0.0, total - height);
  result->color = options_.color();

  QImageReader reader(node.href);
  const QSize imageSize = reader.size();
  if (node.width.isEmpty() && imageSize.isValid() && imageSize.height() > 0) {
    result->width = height * qreal(imageSize.width()) / qreal(imageSize.height());
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeMathChoice(const MathParseNode& node) {
  switch (options_.style().size()) {
    case 0:
      return buildExpression(node.display);
    case 2:
      return buildExpression(node.script);
    case 3:
      return buildExpression(node.scriptScript);
    case 1:
    default:
      return buildExpression(node.body);
  }
}

std::unique_ptr<MathRenderNode> MathBuilder::makeTag(const MathParseNode& node) {
  if (node.body.isEmpty()) {
    auto open = makeSymbol(QStringLiteral("("), MathNodeType::Open, options_);
    auto tag = MathBuilder(options_.havingStyle(options_.style().text())).buildExpression(node.tag);
    auto close = makeSymbol(QStringLiteral(")"), MathNodeType::Close, options_);
    std::vector<std::unique_ptr<MathRenderNode>> tagChildren;
    tagChildren.push_back(std::move(open));
    tagChildren.push_back(std::move(tag));
    tagChildren.push_back(std::move(close));
    return makeSpan(std::move(tagChildren));
  }
  auto body = MathBuilder(options_).buildExpression(node.body);
  auto tag = MathBuilder(options_.havingStyle(options_.style().text())).buildExpression(node.tag);
  auto gap = std::make_unique<MathRenderNode>();
  gap->kind = MathRenderKind::Span;
  gap->width = options_.fontPointSize() * 2.0;
  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.push_back(std::move(body));
  children.push_back(std::move(gap));
  children.push_back(std::move(tag));
  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeVerb(const MathParseNode& node) {
  QString text = node.text;
  text.replace(QLatin1Char(' '), node.label == QStringLiteral("\\verb*") ? QChar(0x2423) : QChar(0x00a0));
  auto result = makeSymbol(text, MathNodeType::Text, options_.havingStyle(options_.style().text()));
  result->font.setStyleHint(QFont::Monospace);
  result->font.setFamily(QStringLiteral("Consolas"));
  const QFontMetricsF metrics(result->font);
  result->width = metrics.horizontalAdvance(text);
  result->height = metrics.ascent();
  result->depth = metrics.descent();
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSymbol(const QString& text, MathNodeType type, const MathOptions& options, const QString& forcedFontClass) {
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Symbol;
  node->text = text;
  QString fontClass = forcedFontClass.isEmpty() ? QStringLiteral("mathit") : forcedFontClass;
  if (type == MathNodeType::Operator || type == MathNodeType::Binary || type == MathNodeType::Relation || type == MathNodeType::Open ||
      type == MathNodeType::Close || type == MathNodeType::Punct || type == MathNodeType::Spacing) {
    if (forcedFontClass.isEmpty()) {
      fontClass = QStringLiteral("main");
    }
  }
  if (text.size() > 1 && type == MathNodeType::Operator) {
    if (forcedFontClass.isEmpty()) {
      fontClass = QStringLiteral("main");
    }
  }
  if (type == MathNodeType::Text && forcedFontClass.isEmpty()) {
    fontClass = QStringLiteral("main");
  }
  node->fontClass = fontClass;
  node->font = options.fontForClass(fontClass);
  node->color = options.color();
  const qreal em = options.fontPointSize();
  if (text.size() == 1) {
    const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(fontClass), text);
    if (metrics) {
      node->width = metrics->width * em;
      node->height = metrics->height * em;
      node->depth = metrics->depth * em;
      node->italic = metrics->italic * em;
      return node;
    }
  }
  const QFontMetricsF metrics(node->font);
  node->width = text.trimmed().isEmpty() ? em * 0.18 * text.size() : metrics.horizontalAdvance(text);
  node->height = metrics.ascent();
  node->depth = metrics.descent();
  return node;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeDelimiter(const QString& delimiter, qreal targetHeight, MathNodeType type) {
  return MathDelimiter::makeCustom(delimiter, targetHeight, true, type, options_);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpan(std::vector<std::unique_ptr<MathRenderNode>> children) {
  auto span = std::make_unique<MathRenderNode>();
  span->kind = MathRenderKind::Span;
  span->children = std::move(children);
  for (const auto& child : span->children) {
    span->width += child->width;
    span->height = qMax(span->height, child->height - child->shift);
    span->depth = qMax(span->depth, child->depth + child->shift);
  }
  return span;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSupSub(const MathParseNode& node) {
  auto base = node.base.isEmpty() ? makeError(QStringLiteral("?"), options_) : MathBuilder(options_).buildExpression(node.base);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  const bool limitsOperator = !node.base.isEmpty() && node.base.first().type == MathNodeType::Operator &&
                              operatorUsesLimits(node.base.first(), options_);
  if (limitsOperator) {
    auto result = std::make_unique<MathRenderNode>();
    result->kind = MathRenderKind::Accent;
    result->width = base->width;
    result->height = base->height;
    result->depth = base->depth;
    base->xOffset = 0.0;
    result->children.push_back(std::move(base));
    const qreal gap = qMax(ruleThickness(options_) * 3.0, em * 0.16);
    if (!node.sup.isEmpty()) {
      auto sup = MathBuilder(options_.sup()).buildExpression(node.sup);
      result->width = qMax(result->width, sup->width);
      sup->xOffset = (result->width - sup->width) / 2.0;
      sup->yOffset = -(result->children.front()->height + gap);
      result->height = qMax(result->height, result->children.front()->height + sup->height + sup->depth + gap);
      result->children.push_back(std::move(sup));
    }
    if (!node.sub.isEmpty()) {
      auto sub = MathBuilder(options_.sub()).buildExpression(node.sub);
      result->width = qMax(result->width, sub->width);
      sub->xOffset = (result->width - sub->width) / 2.0;
      sub->yOffset = result->children.front()->depth + gap + sub->height;
      result->depth = qMax(result->depth, result->children.front()->depth + sub->height + sub->depth + gap);
      result->children.push_back(std::move(sub));
    }
    result->children.front()->xOffset = (result->width - result->children.front()->width) / 2.0;
    return result;
  }

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::SupSub;
  result->width = base->width;
  result->height = base->height;
  result->depth = base->depth;
  const qreal baseWidth = base->width;
  const qreal baseHeight = base->height;
  const qreal baseDepth = base->depth;
  const qreal baseItalic = italicCorrection(*base, options_);
  result->children.push_back(std::move(base));

  qreal scriptWidth = 0.0;
  qreal supShift = 0.0;
  qreal subShift = 0.0;
  std::unique_ptr<MathRenderNode> supNode;
  std::unique_ptr<MathRenderNode> subNode;
  if (!node.sup.isEmpty()) {
    supNode = MathBuilder(options_.sup()).buildExpression(node.sup);
    const qreal metricShift = (options_.style().id() == MathStyle::Display) ? metrics.sup1 : (options_.style().cramped() ? metrics.sup3 : metrics.sup2);
    supShift = qMax(metricShift * em, baseHeight - metrics.supDrop * em);
    const qreal minSupBottom = 0.25 * em;
    supShift = qMax(supShift, supNode->depth + minSupBottom);
    scriptWidth = qMax(scriptWidth, supNode->width);
  }
  if (!node.sub.isEmpty()) {
    subNode = MathBuilder(options_.sub()).buildExpression(node.sub);
    subShift = qMax(metrics.sub1 * em, baseDepth + metrics.subDrop * em);
    scriptWidth = qMax(scriptWidth, subNode->width);
  }
  if (supNode && subNode) {
    const qreal rule = ruleThickness(options_);
    const qreal minGap = 4.0 * rule;
    const qreal gap = supShift - supNode->depth + subShift - subNode->height;
    if (gap < minGap) {
      subShift += minGap - gap;
    }
    const qreal psi = 0.8 * metrics.xHeight * em;
    const qreal supBottom = supShift - supNode->depth;
    if (supBottom < psi) {
      const qreal delta = psi - supBottom;
      supShift += delta;
      subShift -= delta;
    }
  }
  if (supNode) {
    supNode->shift = -supShift;
    supNode->xOffset = baseWidth + baseItalic;
    result->height = qMax(result->height, supNode->height + supShift);
    result->children.push_back(std::move(supNode));
  }
  if (subNode) {
    subNode->shift = subShift;
    subNode->xOffset = baseWidth;
    result->depth = qMax(result->depth, subNode->depth + subShift);
    result->children.push_back(std::move(subNode));
  }

  if (result->children.size() > 1) {
    result->width = baseWidth + baseItalic + scriptWidth + em * 0.05;
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeFraction(const MathParseNode& node) {
  MathOptions localOptions = options_;
  if (node.style == QStringLiteral("\\displaystyle")) {
    localOptions = options_.havingStyle(MathStyle::display());
  } else if (node.style == QStringLiteral("\\textstyle")) {
    localOptions = options_.havingStyle(MathStyle::textStyle());
  } else if (node.style == QStringLiteral("\\scriptstyle")) {
    localOptions = options_.havingStyle(MathStyle::script());
  } else if (node.style == QStringLiteral("\\scriptscriptstyle")) {
    localOptions = options_.havingStyle(MathStyle::scriptScript());
  }
  auto numerator = MathBuilder(localOptions.fracNum()).buildExpression(node.numerator);
  auto denominator = MathBuilder(localOptions.fracDen()).buildExpression(node.denominator);
  const qreal em = localOptions.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(localOptions.style().size());
  const qreal padding = em * 0.12;
  const qreal width = qMax(numerator->width, denominator->width) + padding * 2.0;
  const qreal rule = node.lineThickness >= 0.0 ? node.lineThickness * em : ruleThickness(localOptions);
  const bool hasBarLine = rule > 0.0;
  const qreal axis = axisHeight(localOptions);
  const bool display = localOptions.style().id() == MathStyle::Display || localOptions.style().id() == MathStyle::DisplayCramped;
  const qreal numShift = (display ? metrics.num1 : (hasBarLine ? metrics.num2 : metrics.num3)) * em;
  const qreal denomShift = (display ? metrics.denom1 : metrics.denom2) * em;
  const qreal ruleSpacing = hasBarLine ? rule : ruleThickness(localOptions);
  const qreal clearance = hasBarLine ? (display ? 3.0 : 1.0) * ruleSpacing : (display ? 7.0 : 3.0) * ruleSpacing;

  if (hasBarLine) {
    numerator->shift = -qMax(numShift, axis + rule / 2.0 + numerator->depth + clearance);
    denominator->shift = qMax(denomShift, -axis + rule / 2.0 + denominator->height + clearance);
  } else {
    const qreal candidateClearance = (numShift - numerator->depth) - (denominator->height - denomShift);
    qreal adjustedNumShift = numShift;
    qreal adjustedDenomShift = denomShift;
    if (candidateClearance < clearance) {
      const qreal delta = 0.5 * (clearance - candidateClearance);
      adjustedNumShift += delta;
      adjustedDenomShift += delta;
    }
    numerator->shift = -adjustedNumShift;
    denominator->shift = adjustedDenomShift;
  }

  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.push_back(std::move(numerator));
  if (hasBarLine) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->width = width;
    line->height = 0;
    line->depth = 0;
    line->ruleThickness = rule;
    line->color = localOptions.color();
    children.push_back(std::move(line));
  }
  children.push_back(std::move(denominator));
  auto frac = std::make_unique<MathRenderNode>();
  frac->kind = MathRenderKind::Fraction;
  frac->children = std::move(children);
  frac->width = width;
  for (const auto& child : frac->children) {
    frac->height = qMax(frac->height, child->height - child->shift);
    frac->depth = qMax(frac->depth, child->depth + child->shift);
  }
  return frac;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSqrt(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.body);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  if (qFuzzyIsNull(body->height)) {
    body->height = metrics.xHeight * em;
  }
  const qreal theta = ruleThickness(options_);
  const qreal phi = options_.style().id() < MathStyle::Text ? metrics.xHeight * em : theta;
  qreal lineClearance = theta + 0.25 * phi;
  const qreal minDelimiterHeightEm = qMax<qreal>((body->height + body->depth + lineClearance + theta) / em, metrics.xHeight);
  const qreal extraVinculum = qMax<qreal>(0.0, options_.settings().minRuleThickness - metrics.sqrtRuleThickness);
  QString sqrtName = QStringLiteral("sqrtTall");
  qreal texHeightEm = minDelimiterHeightEm + extraVinculum;
  qreal spanHeightEm = minDelimiterHeightEm + extraVinculum + 0.08;
  qreal advanceWidthEm = 1.056;
  int viewBoxHeight = qFloor(1000.0 * minDelimiterHeightEm + extraVinculum) + 80;
  if (minDelimiterHeightEm < 1.0) {
    sqrtName = QStringLiteral("sqrtMain");
    texHeightEm = 1.0 + extraVinculum;
    spanHeightEm = 1.0 + extraVinculum + 0.08;
    advanceWidthEm = 0.833;
    viewBoxHeight = 1000 + qRound(1000.0 * extraVinculum) + 80;
  } else if (minDelimiterHeightEm < 1.2) {
    sqrtName = QStringLiteral("sqrtSize1");
    texHeightEm = 1.2 + extraVinculum;
    spanHeightEm = 1.2 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 1.2);
  } else if (minDelimiterHeightEm < 1.8) {
    sqrtName = QStringLiteral("sqrtSize2");
    texHeightEm = 1.8 + extraVinculum;
    spanHeightEm = 1.8 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 1.8);
  } else if (minDelimiterHeightEm < 2.4) {
    sqrtName = QStringLiteral("sqrtSize3");
    texHeightEm = 2.4 + extraVinculum;
    spanHeightEm = 2.4 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 2.4);
  } else if (minDelimiterHeightEm < 3.0) {
    sqrtName = QStringLiteral("sqrtSize4");
    texHeightEm = 3.0 + extraVinculum;
    spanHeightEm = 3.0 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 3.0);
  }
  const qreal rule = (metrics.sqrtRuleThickness + extraVinculum) * em;
  const qreal delimDepth = texHeightEm * em - rule;
  if (delimDepth > body->height + body->depth + lineClearance) {
    lineClearance = (lineClearance + delimDepth - body->height - body->depth) / 2.0;
  }
  const qreal imgShift = texHeightEm * em - body->height - lineClearance - rule;
  body->shift = -(imgShift + lineClearance + rule);
  body->xOffset = advanceWidthEm * em;
  const qreal pad = qMax<qreal>(0.0, spanHeightEm - texHeightEm) * em;
  auto radical = std::make_unique<MathRenderNode>();
  radical->kind = MathRenderKind::Stretchy;
  radical->text = QStringLiteral("\\sqrt");
  radical->pathName = sqrtName;
  radical->svgPath = MathSvgGeometry::sqrtPath(sqrtName, extraVinculum, viewBoxHeight);
  radical->viewBox = QRectF(0.0, 0.0, 400000.0, viewBoxHeight);
  radical->width = advanceWidthEm * em;
  radical->height = texHeightEm * em;
  radical->depth = 0.0;
  radical->ruleThickness = rule;
  radical->color = options_.color();

  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.push_back(std::move(radical));
  children.push_back(std::move(body));
  auto sqrt = makeSpan(std::move(children));
  if (!node.rootIndex.isEmpty()) {
    auto index = MathBuilder(options_.havingStyle(MathStyle::scriptScript())).buildExpression(node.rootIndex);
    const qreal toShift = 0.6 * (sqrt->height - sqrt->depth);
    index->xOffset = em * 0.35 - index->width;
    index->yOffset = -toShift + index->height;
    auto result = std::make_unique<MathRenderNode>();
    result->kind = MathRenderKind::Accent;
    result->width = qMax(sqrt->width, sqrt->width + qMax<qreal>(0.0, index->width - em * 0.35));
    result->height = qMax(sqrt->height + pad, toShift + index->depth);
    result->depth = sqrt->depth;
    sqrt->xOffset = qMax<qreal>(0.0, index->width - em * 0.35);
    result->children.push_back(std::move(sqrt));
    result->children.push_back(std::move(index));
    return result;
  }
  sqrt->height += pad;
  return sqrt;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccent(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  const qreal gap = options_.fontPointSize() * MathFontMetrics::globalMetrics(options_.style().size()).defaultRuleThickness * 2.0;
  std::unique_ptr<MathRenderNode> accent;

  if (node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widetilde") ||
      node.label == QStringLiteral("\\widecheck") || node.label == QStringLiteral("\\overrightarrow") ||
      node.label == QStringLiteral("\\overleftarrow") || node.label == QStringLiteral("\\overleftrightarrow")) {
    accent = makeStretchyAccent(node, *body);
  } else if (node.label == QStringLiteral("\\bar")) {
    accent = std::make_unique<MathRenderNode>();
    accent->kind = MathRenderKind::Rule;
    accent->width = qMax<qreal>(body->width, options_.fontPointSize() * 0.6);
    accent->ruleThickness = ruleThickness(options_);
    accent->color = options_.color();
  } else {
    QString accentText = QStringLiteral("^");
    if (node.label == QStringLiteral("\\tilde") || node.label == QStringLiteral("\\widetilde")) accentText = QStringLiteral("~");
    else if (node.label == QStringLiteral("\\vec")) accentText = QStringLiteral("\u2192");
    else if (node.label == QStringLiteral("\\dot")) accentText = QStringLiteral(".");
    else if (node.label == QStringLiteral("\\ddot")) accentText = QStringLiteral("..");
    else if (node.label == QStringLiteral("\\acute")) accentText = QStringLiteral("\u00b4");
    else if (node.label == QStringLiteral("\\grave")) accentText = QStringLiteral("`");
    else if (node.label == QStringLiteral("\\check") || node.label == QStringLiteral("\\widecheck")) accentText = QStringLiteral("\u02c7");
    else if (node.label == QStringLiteral("\\breve")) accentText = QStringLiteral("\u02d8");
    else if (node.label == QStringLiteral("\\mathring")) accentText = QStringLiteral("\u02da");
    accent = makeSymbol(accentText, MathNodeType::Ord, options_);
  }

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = qMax(body->width, accent->width);
  accent->xOffset = (result->width - accent->width) / 2.0;
  accent->yOffset = -(body->height + gap);
  body->xOffset = (result->width - body->width) / 2.0;
  result->height = body->height + accent->height + accent->depth + gap;
  result->depth = body->depth;
  result->children.push_back(std::move(accent));
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccentUnder(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  auto accent = makeStretchyAccent(node, *body);
  const qreal gap = node.label == QStringLiteral("\\utilde") ? options_.fontPointSize() * 0.12 : ruleThickness(options_);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = qMax(body->width, accent->width);
  body->xOffset = (result->width - body->width) / 2.0;
  accent->xOffset = (result->width - accent->width) / 2.0;
  accent->yOffset = body->depth + gap + accent->height;
  result->height = body->height;
  result->depth = body->depth + gap + accent->height + accent->depth;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(accent));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeHorizBrace(const MathParseNode& node) {
  auto body = MathBuilder(options_.havingStyle(MathStyle::display())).buildExpression(node.base);
  auto brace = makeBraceGlyph(node, *body);
  const qreal gap = options_.fontPointSize() * 0.1;
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = qMax(body->width, brace->width);
  body->xOffset = (result->width - body->width) / 2.0;
  brace->xOffset = (result->width - brace->width) / 2.0;
  if (node.isOver) {
    brace->yOffset = -(body->height + gap);
    result->height = body->height + gap + brace->height;
    result->depth = body->depth;
    result->children.push_back(std::move(brace));
    result->children.push_back(std::move(body));
  } else {
    brace->yOffset = body->depth + gap + brace->height;
    result->height = body->height;
    result->depth = body->depth + gap + brace->height;
    result->children.push_back(std::move(body));
    result->children.push_back(std::move(brace));
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeStretchyAccent(const MathParseNode& node, const MathRenderNode& body) {
  auto accent = std::make_unique<MathRenderNode>();
  accent->kind = MathRenderKind::Stretchy;
  accent->text = node.label;
  static const QHash<QString, QString> pathNames{
      {QStringLiteral("\\widehat"), QStringLiteral("widehat1")},
      {QStringLiteral("\\widecheck"), QStringLiteral("widecheck1")},
      {QStringLiteral("\\widetilde"), QStringLiteral("tilde1")},
      {QStringLiteral("\\overrightarrow"), QStringLiteral("rightarrow")},
      {QStringLiteral("\\overleftarrow"), QStringLiteral("leftarrow")},
      {QStringLiteral("\\overleftrightarrow"), QStringLiteral("leftrightarrow")},
      {QStringLiteral("\\underleftarrow"), QStringLiteral("leftarrow")},
      {QStringLiteral("\\underrightarrow"), QStringLiteral("rightarrow")},
      {QStringLiteral("\\underleftrightarrow"), QStringLiteral("leftrightarrow")}};
  accent->pathName = pathNames.value(node.label);
  accent->width = qMax<qreal>(body.width, options_.fontPointSize() * 0.9);
  accent->height = options_.fontPointSize() * 0.32;
  accent->depth = 0.0;
  accent->ruleThickness = ruleThickness(options_);
  accent->color = options_.color();
  return accent;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeBraceGlyph(const MathParseNode& node, const MathRenderNode& body) {
  auto brace = std::make_unique<MathRenderNode>();
  brace->kind = MathRenderKind::Stretchy;
  brace->text = node.label;
  brace->width = qMax<qreal>(body.width, options_.fontPointSize() * 1.0);
  brace->height = options_.fontPointSize() * 0.42;
  brace->depth = 0.0;
  brace->ruleThickness = ruleThickness(options_);
  brace->color = options_.color();
  return brace;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeDelimSizing(const MathParseNode& node) {
  return MathDelimiter::makeSized(node.text, qMax(1, node.delimiterSize), node.body.isEmpty() ? MathNodeType::Ord : node.body.first().type, options_);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeLeftRight(const MathParseNode& node) {
  auto inner = MathBuilder(options_).buildExpression(node.body);
  const qreal innerHeight = inner->height;
  const qreal innerDepth = inner->depth;
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::LeftRight;

  qreal x = 0.0;
  if (node.leftDelim != QStringLiteral(".")) {
    auto left = MathDelimiter::makeLeftRight(node.leftDelim, innerHeight, innerDepth, MathNodeType::Open, options_);
    left->xOffset = x;
    left->yOffset = 0.0;
    x += left->width + options_.fontPointSize() * 0.08;
    result->height = qMax(result->height, left->height);
    result->depth = qMax(result->depth, left->depth);
    result->children.push_back(std::move(left));
  }

  inner->xOffset = x;
  inner->yOffset = 0.0;
  x += inner->width;
  result->height = qMax(result->height, inner->height);
  result->depth = qMax(result->depth, inner->depth);
  result->children.push_back(std::move(inner));

  if (node.rightDelim != QStringLiteral(".")) {
    x += options_.fontPointSize() * 0.08;
    auto right = MathDelimiter::makeLeftRight(node.rightDelim, innerHeight, innerDepth, MathNodeType::Close, options_);
    right->xOffset = x;
    right->yOffset = 0.0;
    x += right->width;
    result->height = qMax(result->height, right->height);
    result->depth = qMax(result->depth, right->depth);
    result->children.push_back(std::move(right));
  }
  result->width = x;
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeArray(const MathParseNode& node) {
  const int rowCount = node.rows.size();
  int colCount = 0;
  for (const auto& row : node.rows) {
    colCount = qMax(colCount, row.size());
  }
  if (rowCount <= 0 || colCount <= 0) {
    return makeError(QStringLiteral("empty array"), options_);
  }

  std::vector<std::vector<std::unique_ptr<MathRenderNode>>> cells;
  QVector<qreal> colWidths(colCount, 0.0);
  QVector<qreal> rowHeights(rowCount, 0.0);
  QVector<qreal> rowDepths(rowCount, 0.0);

  for (int r = 0; r < rowCount; ++r) {
    std::vector<std::unique_ptr<MathRenderNode>> rowCells;
    for (int c = 0; c < colCount; ++c) {
      std::unique_ptr<MathRenderNode> cell;
      if (c < node.rows.at(r).size()) {
        QVector<MathParseNode> cellBody;
        for (const auto& item : node.rows.at(r).at(c).body) {
          if (item) {
            cellBody.push_back(*item);
          }
        }
        cell = MathBuilder(options_.havingStyle(options_.style().text())).buildExpression(cellBody);
      } else {
        cell = std::make_unique<MathRenderNode>();
      }
      colWidths[c] = qMax(colWidths[c], cell->width);
      rowHeights[r] = qMax(rowHeights[r], cell->height);
      rowDepths[r] = qMax(rowDepths[r], cell->depth);
      rowCells.push_back(std::move(cell));
    }
    cells.push_back(std::move(rowCells));
  }

  const GlobalFontMetrics fontMetrics = MathFontMetrics::globalMetrics(options_.style().size());
  const qreal pt = fontMetrics.ptPerEm > 0.0 ? 1.0 / fontMetrics.ptPerEm : 0.1;
  const qreal arrayColSepEm = node.colSeparationType == QStringLiteral("small") ? 0.2778 : 5.0 * pt;
  const qreal baselineSkipEm = 12.0 * pt;
  const qreal jotEm = 3.0 * pt;
  const qreal arrayskipEm = qMax<qreal>(0.0, node.arrayStretch) * baselineSkipEm;
  const qreal arstrutHeight = options_.fontPointSize() * 0.7 * arrayskipEm;
  const qreal arstrutDepth = options_.fontPointSize() * 0.3 * arrayskipEm;
  const qreal rowGap = options_.fontPointSize() * ((node.colSeparationType == QStringLiteral("small")) ? 0.10 : (0.16 + node.arrayStretch - 1.0 + (node.addJot ? 0.09 : 0.0)));
  const qreal rule = ruleThickness(options_);

  for (int r = 0; r < rowCount; ++r) {
    rowHeights[r] = qMax(rowHeights[r], arstrutHeight);
    rowDepths[r] = qMax(rowDepths[r], arstrutDepth);
    if (node.addJot && r + 1 < rowCount) {
      rowDepths[r] += options_.fontPointSize() * jotEm;
    }
    if (r < node.rowGaps.size() && node.rowGaps.at(r) > 0.0) {
      rowDepths[r] = qMax(rowDepths[r], options_.fontPointSize() * node.rowGaps.at(r));
    }
  }

  struct PlacedColumn {
    int column = -1;
    qreal x = 0.0;
    qreal width = 0.0;
    QChar align = QLatin1Char('c');
  };
  struct PlacedSeparator {
    qreal x = 0.0;
    bool dashed = false;
  };

  QVector<PlacedColumn> placedColumns;
  QVector<PlacedSeparator> verticalSeparators;
  qreal bodyWidth = 0.0;
  int alignIndex = 0;
  const bool useExplicitSpec = !node.columns.isEmpty();
  const auto addGap = [&](qreal em) {
    if (em > 0.0) {
      bodyWidth += em * options_.fontPointSize();
    }
  };

  if (useExplicitSpec) {
    bool sawAlign = false;
    bool previousWasSeparator = false;
    for (const MathArrayColumn& spec : node.columns) {
      if (spec.type == MathArrayColumn::Type::Separator) {
        if (previousWasSeparator) {
          addGap(fontMetrics.doubleRuleSep);
        }
        verticalSeparators.push_back({bodyWidth, spec.separator == QLatin1Char(':')});
        previousWasSeparator = true;
        continue;
      }
      if (alignIndex >= colCount) {
        continue;
      }
      previousWasSeparator = false;
      const qreal pregap = spec.pregap >= 0.0 ? spec.pregap : (sawAlign || node.hskipBeforeAndAfter ? arrayColSepEm : 0.0);
      addGap(pregap);
      placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], spec.align});
      bodyWidth += colWidths[alignIndex];
      const qreal postgap = spec.postgap >= 0.0 ? spec.postgap : ((alignIndex + 1 < colCount || node.hskipBeforeAndAfter) ? arrayColSepEm : 0.0);
      addGap(postgap);
      ++alignIndex;
      sawAlign = true;
    }
  }
  while (alignIndex < colCount) {
    const qreal pregap = alignIndex > 0 ? arrayColSepEm : 0.0;
    addGap(pregap);
    const QChar align = alignIndex < node.columnAlignments.size() ? node.columnAlignments.at(alignIndex) : QLatin1Char('c');
    placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], align});
    bodyWidth += colWidths[alignIndex];
    if (alignIndex + 1 < colCount) {
      addGap(arrayColSepEm);
    }
    ++alignIndex;
  }

  auto array = std::make_unique<MathRenderNode>();
  array->kind = MathRenderKind::Array;
  array->columns = colCount;
  array->rows = rowCount;
  qreal totalHeight = 0.0;
  for (int r = 0; r < rowCount; ++r) {
    totalHeight += rowHeights[r] + rowDepths[r];
    if (r + 1 < rowCount) {
      totalHeight += rowGap;
    }
  }
  qreal xOffset = 0.0;
  const qreal targetDelimHeight = totalHeight;
  if (!node.leftDelim.isEmpty()) {
    auto left = makeDelimiter(node.leftDelim, targetDelimHeight, MathNodeType::Open);
    left->xOffset = 0.0;
    left->yOffset = 0.0;
    xOffset = left->width + options_.fontPointSize() * 0.12;
    array->children.push_back(std::move(left));
  }

  qreal baseline = totalHeight / 2.0;
  qreal y = -baseline;
  for (int r = 0; r < rowCount; ++r) {
    for (const MathArrayLine& arrayLine : node.arrayLines) {
      if (arrayLine.beforeRow != r) {
        continue;
      }
      auto line = std::make_unique<MathRenderNode>();
      line->kind = MathRenderKind::Rule;
      line->width = bodyWidth;
      line->ruleThickness = rule;
      line->color = options_.color();
      line->xOffset = xOffset;
      line->yOffset = y;
      line->text = arrayLine.dashed ? QStringLiteral("dashed") : QString();
      array->children.push_back(std::move(line));
    }
    const qreal rowBaseline = y + rowHeights[r];
    for (const PlacedColumn& placed : placedColumns) {
      const int c = placed.column;
      auto cell = std::move(cells[r][c]);
      qreal alignedX = 0.0;
      if (placed.align == QLatin1Char('r')) {
        alignedX = colWidths[c] - cell->width;
      } else if (placed.align == QLatin1Char('c')) {
        alignedX = (colWidths[c] - cell->width) / 2.0;
      }
      cell->xOffset = xOffset + placed.x + alignedX;
      cell->yOffset = rowBaseline;
      array->children.push_back(std::move(cell));
    }
    y += rowHeights[r] + rowDepths[r] + rowGap;
  }
  for (const MathArrayLine& arrayLine : node.arrayLines) {
    if (arrayLine.beforeRow != rowCount) {
      continue;
    }
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->width = bodyWidth;
    line->ruleThickness = rule;
    line->color = options_.color();
    line->xOffset = xOffset;
    line->yOffset = y - rowGap;
    line->text = arrayLine.dashed ? QStringLiteral("dashed") : QString();
    array->children.push_back(std::move(line));
  }

  for (const PlacedSeparator& separator : verticalSeparators) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->height = baseline;
    line->depth = totalHeight - baseline;
    line->width = 0.0;
    line->ruleThickness = rule;
    line->color = options_.color();
    line->xOffset = xOffset + separator.x;
    line->yOffset = 0.0;
    line->shift = -1.0;
    line->text = separator.dashed ? QStringLiteral("dashed") : QString();
    array->children.push_back(std::move(line));
  }

  if (!node.rightDelim.isEmpty()) {
    auto right = makeDelimiter(node.rightDelim, targetDelimHeight, MathNodeType::Close);
    right->xOffset = xOffset + bodyWidth + options_.fontPointSize() * 0.12;
    right->yOffset = 0.0;
    array->children.push_back(std::move(right));
  }

  array->width = bodyWidth + xOffset + (node.rightDelim.isEmpty() ? 0.0 : options_.fontPointSize() * 0.7);
  array->height = baseline;
  array->depth = totalHeight - baseline;
  return array;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeError(const QString& text, const MathOptions& options) {
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Error;
  node->text = text;
  node->font = options.fontForClass(QStringLiteral("main"));
  node->color = options.settings().errorColor;
  const QFontMetricsF metrics(node->font);
  node->width = metrics.horizontalAdvance(text);
  node->height = metrics.ascent();
  node->depth = metrics.descent();
  return node;
}

qreal MathBuilder::dimensionToPoints(const QString& value) const {
  const QString trimmed = value.trimmed();
  static const QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(trimmed);
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2);
  const qreal em = options_.fontPointSize();
  if (unit == QStringLiteral("em")) return number * em;
  if (unit == QStringLiteral("ex")) return number * em * 0.431;
  if (unit == QStringLiteral("mu")) return number * em / 18.0;
  if (unit == QStringLiteral("pt")) return number;
  if (unit == QStringLiteral("mm")) return number * 72.0 / 25.4;
  if (unit == QStringLiteral("cm")) return number * 72.0 / 2.54;
  if (unit == QStringLiteral("in")) return number * 72.0;
  if (unit == QStringLiteral("px")) return number;
  if (unit == QStringLiteral("bp")) return number;
  if (unit == QStringLiteral("pc")) return number * 12.0;
  if (unit == QStringLiteral("dd")) return number * 1238.0 / 1157.0;
  if (unit == QStringLiteral("cc")) return number * 12.0 * 1238.0 / 1157.0;
  if (unit == QStringLiteral("sp")) return number / 65536.0;
  return 0.0;
}

qreal MathBuilder::spacingAfter(int previous, int current) const {
  const auto prev = static_cast<MathAtomClass>(previous);
  const auto next = static_cast<MathAtomClass>(current);
  const qreal thin = 3.0 / 18.0;
  const qreal medium = 4.0 / 18.0;
  const qreal thick = 5.0 / 18.0;

  if (options_.style().isTight()) {
    if (prev == MathAtomClass::Ord && next == MathAtomClass::Op) return thin;
    if (prev == MathAtomClass::Op && (next == MathAtomClass::Ord || next == MathAtomClass::Op)) return thin;
    if (prev == MathAtomClass::Close && next == MathAtomClass::Op) return thin;
    if (prev == MathAtomClass::Inner && next == MathAtomClass::Op) return thin;
    return 0.0;
  }

  switch (prev) {
    case MathAtomClass::Ord:
      if (next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Op:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Bin:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Inner) return medium;
      break;
    case MathAtomClass::Rel:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Inner) return thick;
      break;
    case MathAtomClass::Close:
      if (next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Punct:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Close ||
          next == MathAtomClass::Punct || next == MathAtomClass::Inner) {
        return thin;
      }
      if (next == MathAtomClass::Rel) return thick;
      break;
    case MathAtomClass::Inner:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Punct ||
          next == MathAtomClass::Inner) {
        return thin;
      }
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      break;
    default:
      break;
  }
  return 0.0;
}

}  // namespace muffin::math
