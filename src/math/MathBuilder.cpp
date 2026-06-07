#include "math/MathBuilder.h"

#include "math/MathDelimiter.h"
#include "math/MathFontMetrics.h"
#include "math/MathLayoutTree.h"
#include "math/MathSvgGeometry.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QFontDatabase>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <initializer_list>
#include <memory>
#include <vector>

namespace muffin::math {
namespace {

QString firstAvailableFontFamily(std::initializer_list<QString> candidates) {
  const QStringList availableFamilies = QFontDatabase::families();
  for (const QString& candidate : candidates) {
    for (const QString& family : availableFamilies) {
      if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
        return family;
      }
    }
  }
  const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
  return systemFamily.isEmpty() ? QStringLiteral("monospace") : systemFamily;
}

QString verbFontFamily() {
  static const QString family = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Consolas"),
      QStringLiteral("Lucida Console"),
      QStringLiteral("Courier"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("Menlo"),
      QStringLiteral("Monaco"),
      QStringLiteral("Courier New"),
#else
      QStringLiteral("DejaVu Sans Mono"),
      QStringLiteral("Noto Sans Mono"),
      QStringLiteral("Liberation Mono"),
#endif
      QStringLiteral("monospace"),
  });
  return family;
}

qreal axisHeight(const MathOptions& options) {
  return options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).axisHeight;
}

qreal ruleThickness(const MathOptions& options) {
  const qreal ruleEm = qMax(MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness, options.settings().minRuleThickness);
  return options.fontPointSize() * ruleEm;
}

QString fontMetricsNameForClass(const QString& fontClass) {
  if (fontClass == QStringLiteral("mathnormal")) return QStringLiteral("Math-Italic");
  if (fontClass == QStringLiteral("mathit")) return QStringLiteral("Main-Italic");
  if (fontClass == QStringLiteral("amsrm")) return QStringLiteral("AMS-Regular");
  if (fontClass == QStringLiteral("mathbf")) return QStringLiteral("Main-Bold");
  if (fontClass == QStringLiteral("mathcal")) return QStringLiteral("Caligraphic-Regular");
  if (fontClass == QStringLiteral("mathfrak")) return QStringLiteral("Fraktur-Regular");
  if (fontClass == QStringLiteral("sans")) return QStringLiteral("SansSerif-Regular");
  if (fontClass == QStringLiteral("typewriter")) return QStringLiteral("Typewriter-Regular");
  if (fontClass == QStringLiteral("script")) return QStringLiteral("Script-Regular");
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
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return italicCorrection(*node.children.front(), options);
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->italic * options.fontPointSize() : 0.0;
}

qreal skewCorrection(const MathRenderNode& node, const MathOptions& options) {
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return skewCorrection(*node.children.front(), options);
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->skew * options.fontPointSize() : 0.0;
}

bool isMathFontCommand(const QString& label) {
  static const QSet<QString> commands{
      QStringLiteral("\\mathrm"),     QStringLiteral("\\mathbf"), QStringLiteral("\\mathit"), QStringLiteral("\\mathnormal"),
      QStringLiteral("\\mathsf"),     QStringLiteral("\\mathtt"), QStringLiteral("\\mathbb"), QStringLiteral("\\mathcal"),
      QStringLiteral("\\mathfrak"),   QStringLiteral("\\mathscr"), QStringLiteral("\\mathsfit"), QStringLiteral("\\Bbb"),
      QStringLiteral("\\bold"),       QStringLiteral("\\frak"),   QStringLiteral("\\rm"),     QStringLiteral("\\sf"),
      QStringLiteral("\\tt"),         QStringLiteral("\\bf"),     QStringLiteral("\\it"),     QStringLiteral("\\cal")};
  return commands.contains(label);
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

bool isCharacterBoxRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol) {
    return true;
  }
  if ((node.kind == MathRenderKind::Span || node.kind == MathRenderKind::Accent) && node.children.size() == 1) {
    return isCharacterBoxRenderNode(*node.children.front());
  }
  return false;
}

bool isSingleSymbolRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol && node.text.size() == 1) {
    return true;
  }
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return isSingleSymbolRenderNode(*node.children.front());
  }
  return false;
}

const MathParseNode* baseElement(const MathParseNode& node) {
  if ((node.type == MathNodeType::Group || node.type == MathNodeType::Color || node.type == MathNodeType::Class) && node.body.size() == 1) {
    return baseElement(node.body.first());
  }
  return &node;
}

bool isCharacterBoxNode(const MathParseNode& node) {
  const MathParseNode* base = baseElement(node);
  if (base == nullptr) {
    return false;
  }
  return base->type == MathNodeType::Ord || base->type == MathNodeType::Text || base->type == MathNodeType::Binary ||
         base->type == MathNodeType::Relation || base->type == MathNodeType::Open || base->type == MathNodeType::Close ||
         base->type == MathNodeType::Punct || base->type == MathNodeType::Inner;
}

int accentBaseCharacterCount(const QVector<MathParseNode>& base) {
  if (base.isEmpty()) {
    return 1;
  }
  int count = 0;
  for (const MathParseNode& node : base) {
    if (!node.text.isEmpty()) {
      count += qMax(1, node.text.size());
    } else if (!node.body.isEmpty()) {
      count += accentBaseCharacterCount(node.body);
    } else {
      ++count;
    }
  }
  return qMax(1, count);
}

QString xArrowKey(const QString& label) {
  QString key = label;
  if (key.startsWith(QLatin1Char('\\'))) {
    key.remove(0, 1);
  }
  return key;
}

QString xArrowPathName(const QString& key) {
  static const QHash<QString, QString> paths{
      {QStringLiteral("xrightarrow"), QStringLiteral("rightarrow")},
      {QStringLiteral("xleftarrow"), QStringLiteral("leftarrow")},
      {QStringLiteral("xRightarrow"), QStringLiteral("doublerightarrow")},
      {QStringLiteral("xLeftarrow"), QStringLiteral("doubleleftarrow")},
      {QStringLiteral("xrightharpoondown"), QStringLiteral("rightharpoondown")},
      {QStringLiteral("xrightharpoonup"), QStringLiteral("rightharpoon")},
      {QStringLiteral("xleftharpoondown"), QStringLiteral("leftharpoondown")},
      {QStringLiteral("xleftharpoonup"), QStringLiteral("leftharpoon")},
      {QStringLiteral("xlongequal"), QStringLiteral("longequal")},
      {QStringLiteral("xtwoheadrightarrow"), QStringLiteral("twoheadrightarrow")},
      {QStringLiteral("xtwoheadleftarrow"), QStringLiteral("twoheadleftarrow")}};
  return paths.value(key, QStringLiteral("rightarrow"));
}

qreal xArrowMinWidthEm(const QString& key) {
  static const QHash<QString, qreal> widths{
      {QStringLiteral("xrightarrow"), 1.469},       {QStringLiteral("xleftarrow"), 1.469},
      {QStringLiteral("xRightarrow"), 1.526},       {QStringLiteral("xLeftarrow"), 1.526},
      {QStringLiteral("xleftrightarrow"), 1.75},    {QStringLiteral("xLeftrightarrow"), 1.75},
      {QStringLiteral("xhookleftarrow"), 1.08},     {QStringLiteral("xhookrightarrow"), 1.08},
      {QStringLiteral("xmapsto"), 1.5},             {QStringLiteral("xrightleftharpoons"), 1.75},
      {QStringLiteral("xleftrightharpoons"), 1.75}, {QStringLiteral("xtofrom"), 1.75},
      {QStringLiteral("xrightleftarrows"), 1.75},   {QStringLiteral("xrightequilibrium"), 1.75},
      {QStringLiteral("xleftequilibrium"), 1.75}};
  return widths.value(key, 0.888);
}

qreal xArrowHeightEm(const QString& key) {
  static const QHash<QString, qreal> heights{
      {QStringLiteral("xRightarrow"), 0.560},          {QStringLiteral("xLeftarrow"), 0.560},
      {QStringLiteral("xLeftrightarrow"), 0.560},      {QStringLiteral("xlongequal"), 0.334},
      {QStringLiteral("xtwoheadrightarrow"), 0.334},   {QStringLiteral("xtwoheadleftarrow"), 0.334},
      {QStringLiteral("xrightleftharpoons"), 0.716},   {QStringLiteral("xleftrightharpoons"), 0.716},
      {QStringLiteral("xtofrom"), 0.528},              {QStringLiteral("xrightleftarrows"), 0.901},
      {QStringLiteral("xrightequilibrium"), 0.716},    {QStringLiteral("xleftequilibrium"), 0.716}};
  return heights.value(key, 0.522);
}

}  // namespace

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
    case MathNodeType::XArrow:
      return makeXArrow(node);
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
            children.push_back(makeSymbol(childNode.text, childNode.type, options_, node.fontClass));
          } else {
            children.push_back(buildNode(childNode));
          }
        }
        return makeSpan(std::move(children));
      }
      return buildExpression(node.body);
    case MathNodeType::Error:
      return makeError(node.text, options_);
    case MathNodeType::Text: {
      const MathNodeType fontNodeType = isMathFontCommand(node.label) ? MathNodeType::Ord : MathNodeType::Text;
      if (!node.body.isEmpty()) {
        std::vector<std::unique_ptr<MathRenderNode>> children;
        for (const MathParseNode& childNode : node.body) {
          if (childNode.text.isEmpty() && childNode.body.isEmpty()) {
            children.push_back(buildNode(childNode));
          } else if (!childNode.text.isEmpty()) {
            children.push_back(makeSymbol(childNode.text, fontNodeType, options_, node.fontClass));
          } else {
            children.push_back(MathBuilder(options_).buildExpression(childNode.body));
          }
        }
        return makeSpan(std::move(children));
      }
      if (node.text.size() > 1) {
        return makeTextSpan(node.text, fontNodeType, options_, node.fontClass);
      }
      return makeSymbol(node.text, fontNodeType, options_, node.fontClass);
    }
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
          op->yOffset = -shift;
        }
        return op;
      }
      if (node.text.size() > 1) {
        return makeTextSpan(node.text, node.type, options_, QStringLiteral("main"));
      }
      return makeSymbol(node.text, node.type, options_, node.fontClass.isEmpty() ? QStringLiteral("main") : node.fontClass);
    case MathNodeType::Spacing:
      return makeSpacing(node);
    default:
      return makeSymbol(node.text, node.type, options_, node.fontClass);
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
  auto body = MathBuilder(options_.withPhantom()).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Phantom;
  result->width = node.label == QStringLiteral("\\vphantom") ? 0.0 : body->width;
  result->height = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->height;
  result->depth = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->depth;
  result->children.push_back(std::move(body));
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
  result->text = QStringLiteral("mspace");
  result->atomClass = QStringLiteral("mspace");
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
  const QString label = node.label;
  const bool isCancel = label == QStringLiteral("\\cancel") || label == QStringLiteral("\\bcancel") || label == QStringLiteral("\\xcancel");
  const bool isBox = label == QStringLiteral("\\fbox") || label == QStringLiteral("\\boxed") || label == QStringLiteral("\\colorbox") ||
                     label == QStringLiteral("\\fcolorbox");
  const bool isSingleChar = isCharacterBoxRenderNode(*body);
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  const qreal em = options_.fontPointSize();
  const qreal boxRule = qMax(metrics.fboxrule, options_.settings().minRuleThickness) * em;
  const qreal bodyHeight = body->height;
  const qreal bodyDepth = body->depth;
  qreal hPad = 0.0;
  qreal topPad = 0.0;
  qreal bottomPad = 0.0;

  if (isCancel) {
    hPad = isSingleChar ? 0.0 : 0.2 * em;
    topPad = isSingleChar ? 0.2 * em : 0.0;
    bottomPad = topPad;
  } else if (isBox) {
    hPad = metrics.fboxsep * em;
    topPad = (metrics.fboxsep * em) + (label == QStringLiteral("\\colorbox") ? 0.0 : boxRule);
    bottomPad = topPad;
  } else {
    hPad = 0.3 * em;
    topPad = 0.3 * em;
    bottomPad = 0.3 * em;
  }

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width + hPad * 2.0;
  result->height = isCancel ? body->height : body->height + topPad;
  result->depth = isCancel ? body->depth : body->depth + bottomPad;
  body->xOffset = hPad;
  result->children.push_back(std::move(body));

  if (label == QStringLiteral("\\fbox") || label == QStringLiteral("\\colorbox") || label == QStringLiteral("\\fcolorbox")) {
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
  overlay->text = label;
  overlay->width = result->width;
  overlay->height = bodyHeight + topPad;
  overlay->depth = bodyDepth + bottomPad;
  overlay->ruleThickness = isBox ? boxRule : ruleThickness(options_);
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
  result->font.setFamily(verbFontFamily());
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
  QString fontClass = forcedFontClass.isEmpty() ? QStringLiteral("mathnormal") : forcedFontClass;
  if (forcedFontClass.isEmpty() && type == MathNodeType::Ord && text.size() == 1 && text.at(0).isDigit()) {
    fontClass = QStringLiteral("main");
  }
  if (type == MathNodeType::Operator || type == MathNodeType::Binary || type == MathNodeType::Relation || type == MathNodeType::Open ||
      type == MathNodeType::Close || type == MathNodeType::Punct || type == MathNodeType::Inner || type == MathNodeType::Spacing) {
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
  node->phantom = options.phantom();
  const qreal em = options.fontPointSize();
  if (text.size() == 1) {
    const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(fontClass), text);
    if (metrics) {
      node->width = metrics->width * em;
      node->height = metrics->height * em;
      node->depth = metrics->depth * em;
      node->italic = metrics->italic * em;
      if (node->italic > 0.0 && type != MathNodeType::Text && fontClass != QStringLiteral("mathit")) {
        node->italicMarginRight = node->italic;
        node->width += node->italicMarginRight;
      }
      return node;
    }
  }
  const QFontMetricsF metrics(node->font);
  node->width = text.trimmed().isEmpty() ? em * 0.18 * text.size() : metrics.horizontalAdvance(text);
  node->height = metrics.ascent();
  node->depth = metrics.descent();
  return node;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeTextSpan(const QString& text,
                                                          MathNodeType type,
                                                          const MathOptions& options,
                                                          const QString& forcedFontClass) {
  std::vector<std::unique_ptr<MathRenderNode>> children;
  for (const QChar& ch : text) {
    children.push_back(makeSymbol(QString(ch), type, options, forcedFontClass));
  }
  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpacing(const MathParseNode& node) {
  const QString key = node.label.isEmpty() ? node.text : node.label;
  static const QHash<QString, qreal> widths{
      {QStringLiteral("\\,"), 3.0 / 18.0},
      {QStringLiteral("\\thinspace"), 3.0 / 18.0},
      {QStringLiteral("\\:"), 4.0 / 18.0},
      {QStringLiteral("\\medspace"), 4.0 / 18.0},
      {QStringLiteral("\\;"), 5.0 / 18.0},
      {QStringLiteral("\\thickspace"), 5.0 / 18.0},
      {QStringLiteral("\\!"), -3.0 / 18.0},
      {QStringLiteral("\\negthinspace"), -3.0 / 18.0},
      {QStringLiteral("\\negmedspace"), -4.0 / 18.0},
      {QStringLiteral("\\negthickspace"), -5.0 / 18.0},
      {QStringLiteral("\\quad"), 1.0},
      {QStringLiteral("\\qquad"), 2.0},
      {QStringLiteral("\\ "), 0.25},
      {QStringLiteral("\\space"), 0.25},
      {QStringLiteral("\\nobreakspace"), 0.25},
      {QStringLiteral("\\allowbreak"), 0.0},
      {QStringLiteral(" "), 0.25},
      {QStringLiteral("~"), 0.25}};
  auto space = std::make_unique<MathRenderNode>();
  space->kind = MathRenderKind::Span;
  space->text = key == QStringLiteral("\\allowbreak") ? QStringLiteral("allowbreak") : QStringLiteral("mspace");
  space->atomClass = QStringLiteral("mspace");
  space->allowBreak = key == QStringLiteral("\\allowbreak");
  space->width = widths.value(key, node.text.isEmpty() || node.text.size() > 1 ? 0.0 : 0.25) * options_.fontPointSize();
  space->phantom = options_.phantom();
  return space;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeDelimiter(const QString& delimiter, qreal targetHeight, MathNodeType type) {
  return MathDelimiter::makeCustom(delimiter, targetHeight, true, type, options_);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpan(std::vector<std::unique_ptr<MathRenderNode>> children) {
  auto span = std::make_unique<MathRenderNode>();
  span->kind = MathRenderKind::Span;
  span->phantom = options_.phantom();
  span->children = std::move(children);
  for (const auto& child : span->children) {
    span->width += child->width;
    span->height = qMax(span->height, child->height - child->shift);
    span->depth = qMax(span->depth, child->depth + child->shift);
  }
  return span;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSupSub(const MathParseNode& node) {
  if (node.base.size() == 1 && node.base.first().type == MathNodeType::HorizBrace) {
    return makeHorizBraceSupSub(node);
  }
  auto base = node.base.isEmpty() ? makeError(QStringLiteral("?"), options_) : MathBuilder(options_).buildExpression(node.base);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  const bool limitsOperator = !node.base.isEmpty() && node.base.first().type == MathNodeType::Operator &&
                              operatorUsesLimits(node.base.first(), options_);
  if (limitsOperator) {
    const qreal baseShift = !node.base.isEmpty() && node.base.first().opSymbol
                                ? ((base->height - base->depth) / 2.0 - axisHeight(options_))
                                : 0.0;
    base->yOffset = 0.0;
    qreal slant = italicCorrection(*base, options_);
    if (qFuzzyIsNull(slant) && !node.base.isEmpty() && node.base.first().opSymbol && !node.base.first().text.isEmpty()) {
      const bool large = operatorCanGrow(node.base.first()) && options_.style().id() == MathStyle::Display;
      const std::optional<CharacterMetrics> opMetrics =
          MathFontMetrics::characterMetrics(large ? QStringLiteral("Size2-Regular") : QStringLiteral("Size1-Regular"), node.base.first().text);
      if (opMetrics) {
        slant = opMetrics->italic * em;
      }
    }
    std::unique_ptr<MathRenderNode> sup;
    std::unique_ptr<MathRenderNode> sub;
    qreal supKern = 0.0;
    qreal subKern = 0.0;
    qreal width = base->width;
    if (!node.sup.isEmpty()) {
      sup = MathBuilder(options_.sup()).buildExpression(node.sup);
      supKern = qMax(metrics.bigOpSpacing1 * em, metrics.bigOpSpacing3 * em - sup->depth);
      width = qMax(width, sup->width);
    }
    if (!node.sub.isEmpty()) {
      sub = MathBuilder(options_.sub()).buildExpression(node.sub);
      subKern = qMax(metrics.bigOpSpacing2 * em, metrics.bigOpSpacing4 * em - sub->height);
      width = qMax(width, sub->width);
    }

    base->xOffset = (width - base->width) / 2.0;
    const qreal baseHeight = base->height;
    const qreal baseDepth = base->depth;
    auto baseLayout = layoutFromRenderNode(std::move(base));
    std::unique_ptr<MathLayoutNode> limitsLayout;
    if (sup && sub) {
      sup->xOffset = (width - sup->width) / 2.0 + slant / 2.0;
      sub->xOffset = (width - sub->width) / 2.0 - slant / 2.0;
      const qreal bottom = metrics.bigOpSpacing5 * em + sub->height + sub->depth + subKern + baseDepth + baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sub)), 0.0, -slant, 0.0}));
      entries.push_back(makeLayoutVListKern(subKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      entries.push_back(makeLayoutVListKern(supKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sup)), 0.0, slant, 0.0}));
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      limitsLayout = makeLayoutVListBottom(bottom, std::move(entries));
    } else if (sub) {
      sub->xOffset = (width - sub->width) / 2.0 - slant / 2.0;
      const qreal top = baseHeight - baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sub)), 0.0, -slant, 0.0}));
      entries.push_back(makeLayoutVListKern(subKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      limitsLayout = makeLayoutVListTop(top, std::move(entries));
    } else if (sup) {
      sup->xOffset = (width - sup->width) / 2.0 + slant / 2.0;
      const qreal bottom = baseDepth + baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      entries.push_back(makeLayoutVListKern(supKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sup)), 0.0, slant, 0.0}));
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      limitsLayout = makeLayoutVListBottom(bottom, std::move(entries));
    } else {
      return renderNodeFromLayout(*baseLayout);
    }
    limitsLayout->width = width;
    auto limitsNode = renderNodeFromLayout(*limitsLayout);
    limitsNode->atomClass = QStringLiteral("mop");
    if (sub && !qFuzzyIsNull(slant) && !node.sub.isEmpty() && !isCharacterBoxNode(node.sub.first())) {
      auto spacer = std::make_unique<MathRenderNode>();
      spacer->kind = MathRenderKind::Span;
      spacer->text = QStringLiteral("op-limits-spacer");
      spacer->atomClass = QStringLiteral("mspace");
      spacer->width = slant;
      std::vector<std::unique_ptr<MathRenderNode>> parts;
      parts.push_back(std::move(spacer));
      parts.push_back(std::move(limitsNode));
      auto wrapper = makeSpan(std::move(parts));
      wrapper->atomClass = QStringLiteral("mop");
      return wrapper;
    }
    return limitsNode;
  }

  const qreal baseWidth = base->width;
  const qreal baseHeight = base->height;
  const qreal baseDepth = base->depth;
  const qreal baseItalic = italicCorrection(*base, options_);
  const bool isCharBox = !node.base.isEmpty() && isCharacterBoxNode(node.base.first());
  const bool shiftSubByItalic = isSingleSymbolRenderNode(*base);

  qreal scriptWidth = 0.0;
  qreal supShift = 0.0;
  qreal subShift = 0.0;
  std::unique_ptr<MathRenderNode> supNode;
  std::unique_ptr<MathRenderNode> subNode;
  if (!node.sup.isEmpty()) {
    const MathOptions scriptOptions = options_.sup();
    supNode = MathBuilder(scriptOptions).buildExpression(node.sup);
    const qreal metricShift = (options_.style().id() == MathStyle::Display) ? metrics.sup1 : (options_.style().cramped() ? metrics.sup3 : metrics.sup2);
    if (!isCharBox) {
      supShift = baseHeight - MathFontMetrics::globalMetrics(scriptOptions.style().size()).supDrop * scriptOptions.fontPointSize();
    }
    supShift = qMax(supShift, metricShift * em);
    const qreal minSupBottom = 0.25 * metrics.xHeight * em;
    supShift = qMax(supShift, supNode->depth + minSupBottom);
    scriptWidth = qMax(scriptWidth, supNode->width);
  }
  if (!node.sub.isEmpty()) {
    const MathOptions scriptOptions = options_.sub();
    subNode = MathBuilder(scriptOptions).buildExpression(node.sub);
    if (!isCharBox) {
      subShift = baseDepth + MathFontMetrics::globalMetrics(scriptOptions.style().size()).subDrop * scriptOptions.fontPointSize();
    }
    if (!supNode) {
      subShift = qMax(subShift, metrics.sub1 * em);
      subShift = qMax(subShift, subNode->height - 0.8 * metrics.xHeight * em);
    } else {
      subShift = qMax(subShift, metrics.sub2 * em);
    }
    scriptWidth = qMax(scriptWidth, subNode->width);
  }
  if (supNode && subNode) {
    const qreal rule = ruleThickness(options_);
    const qreal minGap = 4.0 * rule;
    const qreal gap = (supShift - supNode->depth) - (subNode->height - subShift);
    if (gap < minGap) {
      subShift += minGap - gap;
    }
    const qreal psi = 0.8 * metrics.xHeight * em - (supShift - supNode->depth);
    if (psi > 0.0) {
      supShift += psi;
      subShift -= psi;
    }
  }

  std::vector<MathVListChild> scriptChildren;
  const qreal scriptspace = (0.5 / metrics.ptPerEm) * options_.basePointSize();
  if (supNode) {
    scriptChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(supNode)), -supShift, 0.0, scriptspace});
  }
  if (subNode) {
    const qreal marginLeft = shiftSubByItalic ? -baseItalic : 0.0;
    scriptChildren.insert(scriptChildren.begin(), MathVListChild{layoutFromRenderNode(std::move(subNode)), subShift, marginLeft, scriptspace});
  }

  if (scriptChildren.empty()) {
    return base;
  }

  std::unique_ptr<MathLayoutNode> scriptLayout;
  const bool hasSupScript = !node.sup.isEmpty();
  const bool hasSubScript = !node.sub.isEmpty();
  if (hasSupScript && hasSubScript) {
    scriptLayout = makeLayoutVListIndividualShift(std::move(scriptChildren));
  } else if (hasSubScript) {
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(std::move(scriptChildren.front())));
    scriptLayout = makeLayoutVListShift(subShift, std::move(entries));
  } else {
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(std::move(scriptChildren.front())));
    scriptLayout = makeLayoutVListShift(-supShift, std::move(entries));
  }
  std::vector<std::unique_ptr<MathLayoutNode>> supsubChildren;
  supsubChildren.push_back(layoutFromRenderNode(std::move(base)));
  supsubChildren.push_back(std::move(scriptLayout));
  return renderNodeFromLayout(*makeLayoutSpan(std::move(supsubChildren)));
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
  if (node.continuedFraction) {
    numerator->height = qMax(numerator->height, (8.5 / metrics.ptPerEm) * em);
    numerator->depth = qMax(numerator->depth, (3.5 / metrics.ptPerEm) * em);
  }
  const qreal width = qMax(numerator->width, denominator->width);
  const qreal rule = node.lineThickness >= 0.0 ? node.lineThickness * em : ruleThickness(localOptions);
  const bool hasBarLine = rule > 0.0;
  const qreal axis = axisHeight(localOptions);
  const bool display = localOptions.style().size() == MathStyle::display().size();
  qreal numShift = (display ? metrics.num1 : (hasBarLine ? metrics.num2 : metrics.num3)) * em;
  qreal denomShift = (display ? metrics.denom1 : metrics.denom2) * em;
  const qreal ruleSpacing = hasBarLine ? rule : ruleThickness(localOptions);
  const qreal clearance = hasBarLine ? (display ? 3.0 : 1.0) * ruleSpacing : (display ? 7.0 : 3.0) * ruleSpacing;

  if (hasBarLine) {
    const qreal numeratorClearance = (numShift - numerator->depth) - (axis + 0.5 * rule);
    if (numeratorClearance < clearance) {
      numShift += clearance - numeratorClearance;
    }
    const qreal denominatorClearance = (axis - 0.5 * rule) - (denominator->height - denomShift);
    if (denominatorClearance < clearance) {
      denomShift += clearance - denominatorClearance;
    }
  } else {
    const qreal candidateClearance = (numShift - numerator->depth) - (denominator->height - denomShift);
    if (candidateClearance < clearance) {
      const qreal delta = 0.5 * (clearance - candidateClearance);
      numShift += delta;
      denomShift += delta;
    }
  }

  numerator->xOffset = (width - numerator->width) / 2.0;
  denominator->xOffset = (width - denominator->width) / 2.0;

  std::vector<MathVListChild> vlistChildren;
  vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(denominator)), denomShift});
  if (hasBarLine) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->width = width;
    line->height = rule;
    line->depth = 0;
    line->ruleThickness = rule;
    line->color = localOptions.color();
    vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(line)), -(axis - 0.5 * rule)});
  }
  vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(numerator)), -numShift});
  auto fracLayout = makeLayoutVListIndividualShift(std::move(vlistChildren));
  fracLayout->width = width;
  auto frac = renderNodeFromLayout(*fracLayout);
  return makeGenfrac(node, std::move(frac));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeGenfrac(const MathParseNode& node, std::unique_ptr<MathRenderNode> frac) {
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  qreal delimiterSize = 0.0;
  if (options_.style().size() == MathStyle::display().size()) {
    delimiterSize = metrics.delim1 * options_.fontPointSize();
  } else if (options_.style().size() == MathStyle::scriptScript().size()) {
    delimiterSize = MathFontMetrics::globalMetrics(MathStyle::script().size()).delim2 * options_.fontPointSize();
  } else {
    delimiterSize = metrics.delim2 * options_.fontPointSize();
  }

  const qreal nullDelimiterWidth = (1.2 / metrics.ptPerEm) * options_.basePointSize();
  std::vector<std::unique_ptr<MathRenderNode>> children;

  if (node.leftDelim.isEmpty() || node.leftDelim == QStringLiteral(".")) {
    auto nullDelimiter = std::make_unique<MathRenderNode>();
    nullDelimiter->kind = MathRenderKind::Span;
    nullDelimiter->text = QStringLiteral("nulldelimiter");
    nullDelimiter->width = nullDelimiterWidth;
    children.push_back(std::move(nullDelimiter));
  } else {
    children.push_back(MathDelimiter::makeCustom(node.leftDelim, delimiterSize, true, MathNodeType::Open, options_));
  }

  children.push_back(std::move(frac));

  if (node.continuedFraction) {
    auto rightDelimiter = std::make_unique<MathRenderNode>();
    rightDelimiter->kind = MathRenderKind::Span;
    rightDelimiter->text = QStringLiteral("cfrac-right-delimiter");
    children.push_back(std::move(rightDelimiter));
  } else if (node.rightDelim.isEmpty() || node.rightDelim == QStringLiteral(".")) {
    auto nullDelimiter = std::make_unique<MathRenderNode>();
    nullDelimiter->kind = MathRenderKind::Span;
    nullDelimiter->text = QStringLiteral("nulldelimiter");
    nullDelimiter->width = nullDelimiterWidth;
    children.push_back(std::move(nullDelimiter));
  } else {
    children.push_back(MathDelimiter::makeCustom(node.rightDelim, delimiterSize, true, MathNodeType::Close, options_));
  }

  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSqrt(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.body);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  if (qFuzzyIsNull(body->height)) {
    body->height = metrics.xHeight * em;
  }
  const qreal theta = metrics.defaultRuleThickness * em;
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

  body->xOffset = advanceWidthEm * em;
  const qreal innerHeight = body->height;
  Q_UNUSED(pad);
  std::vector<MathVListEntry> sqrtBodyChildren;
  sqrtBodyChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
  sqrtBodyChildren.push_back(makeLayoutVListKern(-(innerHeight + imgShift)));
  sqrtBodyChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(radical))}));
  sqrtBodyChildren.push_back(makeLayoutVListKern(rule));
  auto sqrtBodyLayout = makeLayoutVListFirstBaseline(std::move(sqrtBodyChildren));
  sqrtBodyLayout->width = qMax(sqrtBodyLayout->width, advanceWidthEm * em);

  auto sqrtBody = renderNodeFromLayout(*sqrtBodyLayout);
  if (!node.rootIndex.isEmpty()) {
    auto index = MathBuilder(options_.havingStyle(MathStyle::scriptScript())).buildExpression(node.rootIndex);
    const qreal toShift = 0.6 * (sqrtBody->height - sqrtBody->depth);
    std::vector<MathVListEntry> rootChildren;
    rootChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(index))}));
    auto rootLayout = makeLayoutVListShift(-toShift, std::move(rootChildren));
    auto root = renderNodeFromLayout(*rootLayout);
    const qreal rootMarginLeft = em * (5.0 / 18.0);
    const qreal rootMarginRight = -em * (10.0 / 18.0);
    const qreal rootAdvance = qMax<qreal>(0.0, rootMarginLeft + root->width + rootMarginRight);
    root->xOffset = rootMarginLeft;
    root->width = rootAdvance;
    sqrtBody->xOffset = 0.0;
    std::vector<std::unique_ptr<MathRenderNode>> indexedChildren;
    indexedChildren.push_back(std::move(root));
    indexedChildren.push_back(std::move(sqrtBody));
    return makeSpan(std::move(indexedChildren));
  }
  return sqrtBody;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccent(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  std::unique_ptr<MathRenderNode> accent;

  if (node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widetilde") ||
      node.label == QStringLiteral("\\widecheck") || node.label == QStringLiteral("\\overrightarrow") ||
      node.label == QStringLiteral("\\overleftarrow") || node.label == QStringLiteral("\\Overrightarrow") ||
      node.label == QStringLiteral("\\overleftrightarrow") || node.label == QStringLiteral("\\overgroup") ||
      node.label == QStringLiteral("\\overlinesegment") || node.label == QStringLiteral("\\overleftharpoon") ||
      node.label == QStringLiteral("\\overrightharpoon")) {
    accent = makeStretchyAccent(node, *body);
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(accent))}));
    auto layout = makeLayoutVListFirstBaseline(std::move(entries));
    layout->renderKind = MathRenderKind::Accent;
    return renderNodeFromLayout(*layout);
  } else {
    QString accentText = QStringLiteral("^");
    if (node.label == QStringLiteral("\\tilde") || node.label == QStringLiteral("\\widetilde")) accentText = QStringLiteral("~");
    else if (node.label == QStringLiteral("\\bar")) accentText = QStringLiteral("ˉ");
    else if (node.label == QStringLiteral("\\dot")) accentText = QStringLiteral(".");
    else if (node.label == QStringLiteral("\\ddot")) accentText = QStringLiteral("..");
    else if (node.label == QStringLiteral("\\acute")) accentText = QStringLiteral("´");
    else if (node.label == QStringLiteral("\\grave")) accentText = QStringLiteral("`");
    else if (node.label == QStringLiteral("\\check") || node.label == QStringLiteral("\\widecheck")) accentText = QStringLiteral("ˇ");
    else if (node.label == QStringLiteral("\\breve")) accentText = QStringLiteral("˘");
    else if (node.label == QStringLiteral("\\mathring")) accentText = QStringLiteral("˚");
    if (node.label == QStringLiteral("\\vec")) {
      accent = std::make_unique<MathRenderNode>();
      accent->kind = MathRenderKind::Stretchy;
      accent->pathName = QStringLiteral("vec");
      accent->svgPath = MathSvgGeometry::path(QStringLiteral("vec"));
      accent->viewBox = QRectF(0.0, 0.0, 471.0, 714.0);
      accent->width = options_.fontPointSize() * 0.471;
      accent->height = options_.fontPointSize() * 0.714;
      accent->depth = 0.0;
      accent->color = options_.color();
    } else {
      accent = makeSymbol(accentText, MathNodeType::Text, options_.havingStyle(options_.style().text()), QStringLiteral("main"));
      accent->italic = 0.0;
      accent->italicMarginRight = 0.0;
    }
  }

  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  qreal clearance = qMin(body->height, metrics.xHeight * options_.fontPointSize());
  const qreal accentWidth = accent->width;
  const qreal skew = isCharacterBoxRenderNode(*body) ? skewCorrection(*body, options_) : 0.0;
  accent->xOffset = (body->width - accentWidth) / 2.0 + skew;
  accent->height += accent->depth;
  accent->depth = 0.0;

  std::vector<std::unique_ptr<MathRenderNode>> accentChildren;
  accentChildren.push_back(std::move(accent));
  auto accentWrapper = makeSpan(std::move(accentChildren));
  accentWrapper->atomClass = QStringLiteral("accent-body");
  accentWrapper->width = 0.0;

  std::vector<MathVListEntry> entries;
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
  entries.push_back(makeLayoutVListKern(-clearance));
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(accentWrapper))}));
  auto layout = makeLayoutVListFirstBaseline(std::move(entries));
  layout->renderKind = MathRenderKind::Accent;
  return renderNodeFromLayout(*layout);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccentUnder(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  auto accent = makeStretchyAccent(node, *body);
  const qreal kern = node.label == QStringLiteral("\\utilde") ? options_.fontPointSize() * 0.12 : 0.0;
  const qreal bodyHeight = body->height;
  const qreal width = qMax(body->width, accent->width);
  body->xOffset = (width - body->width) / 2.0;
  accent->xOffset = (width - accent->width) / 2.0;

  std::vector<MathVListEntry> entries;
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(accent))}));
  if (!qFuzzyIsNull(kern)) {
    entries.push_back(makeLayoutVListKern(kern));
  }
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
  auto layout = makeLayoutVListTop(bodyHeight, std::move(entries));
  layout->renderKind = MathRenderKind::Accent;
  layout->width = width;
  auto result = renderNodeFromLayout(*layout);
  result->atomClass = QStringLiteral("mord");
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

std::unique_ptr<MathRenderNode> MathBuilder::makeHorizBraceSupSub(const MathParseNode& node) {
  const MathParseNode& braceNode = node.base.first();
  auto braceBase = makeHorizBrace(braceNode);
  const bool useSup = braceNode.isOver && !node.sup.isEmpty();
  const bool useSub = !braceNode.isOver && !node.sub.isEmpty();
  if (!useSup && !useSub) {
    return braceBase;
  }

  auto annotation = MathBuilder(braceNode.isOver ? options_.sup() : options_.sub()).buildExpression(useSup ? node.sup : node.sub);
  const qreal gap = options_.fontPointSize() * 0.2;
  const qreal width = qMax(braceBase->width, annotation->width);
  braceBase->xOffset = (width - braceBase->width) / 2.0;
  annotation->xOffset = (width - annotation->width) / 2.0;

  std::vector<MathVListEntry> entries;
  std::unique_ptr<MathLayoutNode> layout;
  if (braceNode.isOver) {
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(braceBase))}));
    entries.push_back(makeLayoutVListKern(gap));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(annotation))}));
    layout = makeLayoutVListFirstBaseline(std::move(entries));
  } else {
    const qreal bottom = braceBase->depth + gap + annotation->height + annotation->depth;
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(annotation))}));
    entries.push_back(makeLayoutVListKern(gap));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(braceBase))}));
    layout = makeLayoutVListBottom(bottom, std::move(entries));
  }
  layout->width = width;
  return renderNodeFromLayout(*layout);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeXArrow(const MathParseNode& node) {
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  auto upperGroup = MathBuilder(options_.sup()).buildExpression(node.body);
  auto lowerGroup = node.sub.isEmpty() ? nullptr : MathBuilder(options_.sub()).buildExpression(node.sub);

  const qreal pad = 0.3 * em;
  upperGroup->xOffset = pad;
  upperGroup->width += 2.0 * pad;
  if (lowerGroup) {
    lowerGroup->xOffset = pad;
    lowerGroup->width += 2.0 * pad;
  }

  const QString key = xArrowKey(node.label);
  const qreal arrowWidth = qMax(xArrowMinWidthEm(key) * em, qMax(upperGroup->width, lowerGroup ? lowerGroup->width : 0.0));
  const qreal arrowHeight = xArrowHeightEm(key) * em;

  auto arrowBody = std::make_unique<MathRenderNode>();
  arrowBody->kind = MathRenderKind::Stretchy;
  arrowBody->text = node.label;
  arrowBody->atomClass = QStringLiteral("x-arrow-body");
  arrowBody->pathName = xArrowPathName(key);
  arrowBody->width = arrowWidth;
  arrowBody->height = arrowHeight;
  arrowBody->depth = 0.0;
  arrowBody->ruleThickness = ruleThickness(options_);
  arrowBody->color = options_.color();

  const qreal arrowShift = -metrics.axisHeight * em + 0.5 * arrowBody->height;
  qreal upperShift = -metrics.axisHeight * em - 0.5 * arrowBody->height - 0.111 * em;
  if (upperGroup->depth > 0.25 * em || node.label == QStringLiteral("\\xleftequilibrium")) {
    upperShift -= upperGroup->depth;
  }

  upperGroup->xOffset += (arrowWidth - upperGroup->width) / 2.0;
  arrowBody->xOffset = 0.0;
  std::vector<MathVListChild> children;
  children.push_back(MathVListChild{layoutFromRenderNode(std::move(upperGroup)), upperShift});
  children.push_back(MathVListChild{layoutFromRenderNode(std::move(arrowBody)), arrowShift});
  if (lowerGroup) {
    const qreal lowerShift = -metrics.axisHeight * em + lowerGroup->height + 0.5 * arrowHeight + 0.111 * em;
    lowerGroup->xOffset += (arrowWidth - lowerGroup->width) / 2.0;
    children.push_back(MathVListChild{layoutFromRenderNode(std::move(lowerGroup)), lowerShift});
  }

  auto layout = makeLayoutVListIndividualShift(std::move(children));
  layout->width = arrowWidth;
  auto result = renderNodeFromLayout(*layout);
  result->atomClass = QStringLiteral("mrel");
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeStretchyAccent(const MathParseNode& node, const MathRenderNode& body) {
  auto accent = std::make_unique<MathRenderNode>();
  accent->kind = MathRenderKind::Stretchy;
  accent->text = node.label;
  const qreal em = options_.fontPointSize();
  if (node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widecheck") ||
      node.label == QStringLiteral("\\widetilde") || node.label == QStringLiteral("\\utilde")) {
    const int numChars = accentBaseCharacterCount(node.base);
    const bool hatLike = node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widecheck");
    const QString prefix = node.label == QStringLiteral("\\widecheck") ? QStringLiteral("widecheck") : QStringLiteral("widehat");
    if (numChars > 5) {
      accent->pathName = hatLike ? prefix + QStringLiteral("4") : QStringLiteral("tilde4");
      accent->viewBox = hatLike ? QRectF(0.0, 0.0, 2364.0, 420.0) : QRectF(0.0, 0.0, 2340.0, 312.0);
      accent->height = em * (hatLike ? 0.42 : 0.34);
    } else {
      static const int imgIndexByChars[6] = {1, 1, 2, 2, 3, 3};
      const int imgIndex = imgIndexByChars[qBound(0, numChars, 5)];
      if (hatLike) {
        static const qreal viewWidths[5] = {0.0, 1062.0, 2364.0, 2364.0, 2364.0};
        static const qreal viewHeights[5] = {0.0, 239.0, 300.0, 360.0, 420.0};
        static const qreal heights[5] = {0.0, 0.24, 0.30, 0.36, 0.42};
        accent->pathName = prefix + QString::number(imgIndex);
        accent->viewBox = QRectF(0.0, 0.0, viewWidths[imgIndex], viewHeights[imgIndex]);
        accent->height = em * heights[imgIndex];
      } else {
        static const qreal viewWidths[5] = {0.0, 600.0, 1033.0, 2339.0, 2340.0};
        static const qreal viewHeights[5] = {0.0, 260.0, 286.0, 306.0, 312.0};
        static const qreal heights[5] = {0.0, 0.26, 0.286, 0.306, 0.34};
        accent->pathName = QStringLiteral("tilde") + QString::number(imgIndex);
        accent->viewBox = QRectF(0.0, 0.0, viewWidths[imgIndex], viewHeights[imgIndex]);
        accent->height = em * heights[imgIndex];
      }
    }
    accent->width = body.width;
  } else {
    static const QHash<QString, QString> pathNames{
        {QStringLiteral("\\overrightarrow"), QStringLiteral("rightarrow")},
        {QStringLiteral("\\overleftarrow"), QStringLiteral("leftarrow")},
        {QStringLiteral("\\underleftarrow"), QStringLiteral("leftarrow")},
        {QStringLiteral("\\underrightarrow"), QStringLiteral("rightarrow")}};
    static const QHash<QString, qreal> minWidths{
        {QStringLiteral("\\overbrace"), 1.6},        {QStringLiteral("\\underbrace"), 1.6},
        {QStringLiteral("\\overbracket"), 1.6},      {QStringLiteral("\\underbracket"), 1.6},
        {QStringLiteral("\\overparent"), 1.6},       {QStringLiteral("\\underparent"), 1.6},
        {QStringLiteral("\\overrightarrow"), 0.888}, {QStringLiteral("\\overleftarrow"), 0.888},
        {QStringLiteral("\\overleftrightarrow"), 0.888},
        {QStringLiteral("\\underleftarrow"), 0.888}, {QStringLiteral("\\underrightarrow"), 0.888},
        {QStringLiteral("\\underleftrightarrow"), 0.888},
        {QStringLiteral("\\overlinesegment"), 0.888},
        {QStringLiteral("\\underlinesegment"), 0.888},
        {QStringLiteral("\\overgroup"), 0.888},
        {QStringLiteral("\\undergroup"), 0.888}};
    static const QHash<QString, qreal> heights{
        {QStringLiteral("\\Overrightarrow"), 0.560}, {QStringLiteral("\\xRightarrow"), 0.560},
        {QStringLiteral("\\overbracket"), 0.440},    {QStringLiteral("\\underbracket"), 0.410},
        {QStringLiteral("\\overgroup"), 0.342},      {QStringLiteral("\\undergroup"), 0.342}};
    const QString pathName = pathNames.value(node.label);
    accent->width = qMax<qreal>(body.width, em * minWidths.value(node.label, 0.888));
    accent->height = em * heights.value(node.label, 0.522);
    accent->pathName = pathName;
  }
  accent->depth = 0.0;
  accent->ruleThickness = ruleThickness(options_);
  accent->color = options_.color();
  return accent;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeBraceGlyph(const MathParseNode& node, const MathRenderNode& body) {
  auto brace = std::make_unique<MathRenderNode>();
  brace->kind = MathRenderKind::Stretchy;
  brace->text = node.label;
  brace->width = qMax<qreal>(body.width, options_.fontPointSize() * 1.6);
  brace->height = options_.fontPointSize() * 0.548;
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
  const qreal nullDelimiterWidth = options_.fontPointSize() * 0.12;

  qreal x = 0.0;
  if (node.leftDelim == QStringLiteral(".")) {
    x += nullDelimiterWidth;
  } else {
    auto left = MathDelimiter::makeLeftRight(node.leftDelim, innerHeight, innerDepth, MathNodeType::Open, options_);
    left->xOffset = x;
    left->yOffset = 0.0;
    x += left->width;
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

  if (node.rightDelim == QStringLiteral(".")) {
    x += nullDelimiterWidth;
  } else {
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
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2).toLower();
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

}  // namespace muffin::math
