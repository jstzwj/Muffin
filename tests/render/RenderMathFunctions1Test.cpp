#include "document/MarkdownDocument.h"
#include "math/MathBuilder.h"
#include "math/MathDimension.h"
#include "math/MathFontMetrics.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"
#include "math/MathRenderer.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>

#include <functional>
#include <iostream>

#include "MathTestUtils.h"

using namespace muffin;

namespace {

void testRemainingKatexFunctionsPart1() {
  const math::MathFunctionSpec* fracSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\frac"));
  require(fracSpec != nullptr && fracSpec->typeName == QStringLiteral("genfrac") && fracSpec->numArgs == 2,
          QStringLiteral("function registry should expose genfrac metadata"));
  require(fracSpec->handlerKind == math::MathFunctionHandlerKind::Fraction &&
              fracSpec->builderKind == math::MathFunctionBuilderKind::Fraction,
          QStringLiteral("function registry should map frac to parser and builder kinds"));

  const math::MathFunctionSpec* sqrtSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\sqrt"));
  require(sqrtSpec != nullptr && sqrtSpec->numArgs == 1 && sqrtSpec->numOptionalArgs == 1,
          QStringLiteral("function registry should expose sqrt optional argument metadata"));
  const math::MathFunctionSpec* genfracSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\genfrac"));
  require(genfracSpec != nullptr && genfracSpec->numArgs == 6 && genfracSpec->typeName == QStringLiteral("genfrac"),
          QStringLiteral("function registry should expose explicit genfrac metadata"));
  const math::MathFunctionSpec* textbfSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\textbf"));
  require(textbfSpec != nullptr && textbfSpec->typeName == QStringLiteral("text"),
          QStringLiteral("function registry should expose text font commands"));

  const math::MathFunctionSpec* hrefSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\href"));
  require(hrefSpec != nullptr && hrefSpec->requiresTrust && hrefSpec->allowedInText &&
              hrefSpec->argTypes.size() == 2 && hrefSpec->argTypes.first() == math::MathFunctionArgType::Url,
          QStringLiteral("function registry should expose href trust and argTypes metadata"));

  const math::MathFunctionSpec* htmlSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\htmlClass"));
  require(htmlSpec != nullptr && htmlSpec->strictCategory == QStringLiteral("htmlExtension"),
          QStringLiteral("function registry should expose html strict category"));

  const math::MathFunctionSpec* bigrSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\bigr"));
  require(bigrSpec != nullptr && bigrSpec->delimiterSize == 1 && bigrSpec->delimiterNodeType == math::MathNodeType::Close,
          QStringLiteral("function registry should expose delimiter sizing side metadata"));

  math::MathSettings strictSettings;
  strictSettings.throwOnError = true;
  {
    math::MathParser parser(QStringLiteral("\\sqrt[3]{x}+\\genfrac{[}{]}{0.08em}{0}{a}{b}+\\textbf{B}+\\mathit{x}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::Sqrt && !nodes.first().rootIndex.isEmpty(),
            QStringLiteral("sqrt optional root index should parse"));
    bool sawGenfrac = false;
    bool sawTextbf = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Fraction) {
        sawGenfrac = node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]") &&
                     node.lineThickness > 0.0 && node.style == QStringLiteral("\\displaystyle");
      }
      if (node.type == math::MathNodeType::Text && node.fontClass == QStringLiteral("mathbf")) {
        sawTextbf = true;
      }
    }
    require(sawGenfrac, QStringLiteral("genfrac should parse delimiters line thickness style numerator and denominator"));
    require(sawTextbf, QStringLiteral("font text command should preserve font class"));
  }
  {
    math::MathSettings settings;
    math::MathOptions options(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    auto simpleSqrt = math::MathBuilder(options).buildExpression(math::MathParser(QStringLiteral("\\sqrt{x}"), settings).parse());
    auto indexedSqrt = math::MathBuilder(options).buildExpression(math::MathParser(QStringLiteral("\\sqrt[123]{x}"), settings).parse());
    auto tallSqrt = math::MathBuilder(options).buildExpression(
        math::MathParser(QStringLiteral("\\sqrt{\\frac{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}{\\frac{\\frac{e}{f}}{\\frac{g}{h}}}}"), settings).parse());
    require(simpleSqrt != nullptr && indexedSqrt != nullptr && tallSqrt != nullptr, QStringLiteral("sqrt builder should produce render nodes"));
    require(indexedSqrt->width > simpleSqrt->width && indexedSqrt->height >= simpleSqrt->height,
            QStringLiteral("sqrt root index should add KaTeX-style left kern and vertical raise"));
    require(simpleSqrt->kind == math::MathRenderKind::Span && simpleSqrt->children.size() == 1 &&
                simpleSqrt->children.front()->kind == math::MathRenderKind::VList &&
                simpleSqrt->children.front()->children.size() == 2,
            QStringLiteral("sqrt body should render through native KaTeX VList span with radical and argument"));
    require(indexedSqrt->kind == math::MathRenderKind::Span && indexedSqrt->children.size() == 1 &&
                indexedSqrt->children.front()->kind == math::MathRenderKind::Span &&
                indexedSqrt->children.front()->children.size() == 2 &&
                indexedSqrt->children.front()->children.front()->kind == math::MathRenderKind::VList &&
                indexedSqrt->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("indexed sqrt should render root index and body through native KaTeX Span/VList layout"));
    const std::function<bool(const math::MathRenderNode*)> hasTallSqrt = [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) return false;
      if (node->pathName == QStringLiteral("sqrtTall")) return true;
      for (const auto& child : node->children) { if (hasTallSqrt(child.get())) return true; }
      return false;
    };
    require(hasTallSqrt(tallSqrt.get()), QStringLiteral("large sqrt should use KaTeX tall sqrt SVG geometry"));
  }
  {
    math::MathParser parser(QStringLiteral("{a+b\\over c+d}+{n\\choose k}+{x\\atop y}+{p\\brace q}+{r\\brack s}+{u\\above 0.08em v}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    int sawFractions = 0;
    bool sawChooseDelims = false, sawBraceDelims = false, sawBrackDelims = false, sawAtopNoBar = false, sawAboveRule = false;
    const std::function<void(const math::MathParseNode&)> scanInfix = [&](const math::MathParseNode& node) {
      if (node.type == math::MathNodeType::Fraction) {
        ++sawFractions;
        if (node.leftDelim == QStringLiteral("(") && node.rightDelim == QStringLiteral(")")) sawChooseDelims = true;
        if (node.leftDelim == QStringLiteral("\\lbrace") && node.rightDelim == QStringLiteral("\\rbrace")) sawBraceDelims = true;
        if (node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]")) sawBrackDelims = true;
        if (node.lineThickness == 0.0) sawAtopNoBar = true;
        if (node.lineThickness > 0.0) sawAboveRule = true;
      }
      for (const math::MathParseNode& child : node.body) scanInfix(child);
      for (const math::MathParseNode& child : node.base) scanInfix(child);
    };
    for (const math::MathParseNode& node : nodes) scanInfix(node);
    require(sawFractions >= 6 && sawChooseDelims && sawBraceDelims && sawBrackDelims && sawAtopNoBar && sawAboveRule,
            QStringLiteral("infix genfrac commands should rewrite to fractions with KaTeX delimiter and rule semantics"));

    math::MathSettings settings;
    math::MathOptions options(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> noBar = math::MathBuilder(options).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("{x\\atop y}")).parse().first()});
    const std::function<bool(const math::MathRenderNode*)> hasRule = [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) return false;
      if (node->kind == math::MathRenderKind::Rule) return true;
      for (const auto& child : node->children) { if (hasRule(child.get())) return true; }
      return false;
    };
    require(noBar != nullptr && !hasRule(noBar.get()), QStringLiteral("atop fraction should render without a rule child"));
  }
  {
    math::MathParser parser(QStringLiteral("\\@binrel{=}{x}+\\@binrel{+}{y}+\\overset{a}{=}+{ab}+\\boldsymbol{=}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    bool sawRelBinrel = false, sawBinBinrel = false, sawOversetRel = false, sawBoldRel = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathrel")) {
        if (!node.body.isEmpty() && node.body.first().type != math::MathNodeType::SupSub) sawRelBinrel = true;
        if (!node.body.isEmpty() && node.body.first().type == math::MathNodeType::SupSub) sawOversetRel = true;
        if (node.fontClass == QStringLiteral("mathbf")) sawBoldRel = true;
      }
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathbin")) sawBinBinrel = true;
    }
    require(sawRelBinrel && sawBinBinrel && sawOversetRel && sawBoldRel,
            QStringLiteral("mclass binrel and boldsymbol should preserve inferred math classes"));
  }
  {
    math::MathSettings settings;
    math::MathOptions textOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    const auto widthForWithOptions = [&](const QString& tex, math::MathOptions options) {
      return math::MathBuilder(options).buildExpression(math::MathParser(tex, settings).parse())->width;
    };
    const auto widthFor = [&](const QString& tex) { return widthForWithOptions(tex, textOptions); };
    const qreal plusBetweenOrd = widthFor(QStringLiteral("a+b"));
    require(plusBetweenOrd > widthFor(QStringLiteral("+b")) && plusBetweenOrd > widthFor(QStringLiteral("ab")),
            QStringLiteral("mbin should keep medium spacing only between compatible atoms"));
    require(widthFor(QStringLiteral("a=+b")) < widthFor(QStringLiteral("a=b+c")),
            QStringLiteral("mbin after relation should downgrade to ord spacing"));
    require(widthFor(QStringLiteral("a,b")) > widthFor(QStringLiteral("ab")),
            QStringLiteral("mpunct should insert KaTeX thin spacing before following ord"));
    require(widthFor(QStringLiteral("\\left(x\\right)y")) > widthFor(QStringLiteral("xy")),
            QStringLiteral("minner should insert KaTeX thin spacing before following ord"));
    require(widthFor(QStringLiteral("a=b")) > widthFor(QStringLiteral("a{=}b")),
            QStringLiteral("ordgroup should participate as mord in outer spacing"));
    math::MathOptions scriptOptions(math::MathStyle::script(), 16.0, QColor(QStringLiteral("#111111")), settings);
    require(widthForWithOptions(QStringLiteral("a+b"), scriptOptions) < plusBetweenOrd,
            QStringLiteral("tight script style should suppress binary spacing"));
  }
  {
    math::MathSettings fractionSettings;
    math::MathOptions fractionOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), fractionSettings);
    std::unique_ptr<math::MathRenderNode> fraction =
        math::MathBuilder(fractionOptions).buildExpression(math::MathParser(QStringLiteral("\\frac{a}{b}"), fractionSettings).parse());
    require(fraction != nullptr && fraction->kind == math::MathRenderKind::Span && !fraction->children.empty(),
            QStringLiteral("fraction should be built from native KaTeX span/vlist layout"));
    const std::function<const math::MathRenderNode*(const math::MathRenderNode*)> findFractionVList =
        [&](const math::MathRenderNode* node) -> const math::MathRenderNode* {
      if (node == nullptr) return nullptr;
      if (node->kind == math::MathRenderKind::VList && node->children.size() == 3) return node;
      for (const auto& child : node->children) { if (const math::MathRenderNode* found = findFractionVList(child.get())) return found; }
      return nullptr;
    };
    const math::MathRenderNode* vlist = findFractionVList(fraction.get());
    require(vlist != nullptr && vlist->children.size() == 3,
            QStringLiteral("fraction vlist should contain denominator, rule, and numerator children"));
    require(vlist->children.at(0)->shift > 0.0 && vlist->children.at(2)->shift < 0.0,
            QStringLiteral("fraction vlist should use individualShift positions for denominator and numerator"));
  }
  {
    math::MathSettings settings;
    math::MathOptions textOptions(math::MathStyle::textStyle(), 20.0, QColor(QStringLiteral("#111111")), settings);
    auto widthFor = [&](const QString& tex, const math::MathOptions& options) -> qreal {
      return math::MathBuilder(options).buildExpression(math::MathParser(tex, settings).parse())->width;
    };
    require(qAbs(widthFor(QStringLiteral("\\rule{1em}{1em}"), textOptions) - 20.0) < 0.01,
            QStringLiteral("1em rule width should scale with current font size"));
    require(qAbs(math::dimensionToPoints(QStringLiteral("1zz"), 20.0, 20.0)) < 0.01,
            QStringLiteral("unknown dimension units should not be treated as em"));
  }
  {
    RenderTheme theme = RenderTheme::github();
    math::MathSettings settings;
    require(qAbs(math::MathRenderer::katexRootFontPixelSize(theme) - theme.mathFont().pointSizeF() * 96.0 / 72.0 * 1.21) < 0.001,
            QStringLiteral("MathRenderer should convert Qt point size to CSS pixels before applying KaTeX root font scale"));
    const math::MathLayoutResult rendered = math::MathRenderer().render(QStringLiteral("E=mc^2"), theme, false);
    require(rendered.valid() && rendered.size.width() > 0.0,
            QStringLiteral("MathRenderer should apply KaTeX root font scale 1.21 after point-to-pixel conversion"));
  }
  {
    RenderTheme theme = RenderTheme::github();
    const QString wide = QStringLiteral("a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a");
    const math::MathLayoutResult natural = math::MathRenderer().render(wide, theme, true);
    const qreal maxWidth = qMax<qreal>(20.0, natural.size.width() * 0.45);
    const math::MathLayoutResult constrained = math::MathRenderer().render(wide, theme, true, maxWidth);
    require(constrained.valid() && constrained.overflow, QStringLiteral("constrained math layout should report overflow"));
    require(qAbs(constrained.size.width() - maxWidth) < 0.01, QStringLiteral("constrained math layout should expose max width"));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testRemainingKatexFunctionsPart1);
#undef RUN_TEST
  return 0;
}
