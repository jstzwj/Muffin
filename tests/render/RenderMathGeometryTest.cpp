#include "document/MarkdownDocument.h"
#include "math/MathBuilder.h"
#include "math/MathDelimiter.h"
#include "math/MathDimension.h"
#include "math/MathFontMetrics.h"
#include "math/MathLayoutTree.h"
#include "math/MathParser.h"
#include "math/MathRenderer.h"
#include "math/MathSvgGeometry.h"
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

const auto requireMathSnippetLayout = [](const QString& tex, const QString& label) {
  math::MathSettings settings;
  settings.displayMode = true;
  math::MathParser mathParser(tex, settings);
  const QVector<math::MathParseNode> mathTree = mathParser.parse();
  math::MathOptions mathOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
  math::MathBuilder mathBuilder{mathOptions};
  std::unique_ptr<math::MathRenderNode> mathRoot = mathBuilder.buildExpression(mathTree);
  require(mathRoot != nullptr && mathRoot->width > 0.0, label + QStringLiteral(" direct math builder should produce layout"));
  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral("$$\n%1\n$$").arg(tex);
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, label + QStringLiteral(" parse should produce document"));
  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 900.0);
  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, label + QStringLiteral(" math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          label + QStringLiteral(" should have native layout"));
};

void testStrictMathGeometryFeatures1() {
  // Delimiters, environments, array preamble
  math::MathSettings delimiterSettings;
  math::MathOptions delimiterOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), delimiterSettings);
  std::unique_ptr<math::MathRenderNode> biggParen =
      math::MathDelimiter::makeSized(QStringLiteral("("), 4, math::MathNodeType::Open, delimiterOptions);
  require(biggParen != nullptr && biggParen->fontClass == QStringLiteral("size4"),
          QStringLiteral("Bigg delimiter should use KaTeX Size4 font"));
  std::unique_ptr<math::MathRenderNode> tallBrace =
      math::MathDelimiter::makeLeftRight(QStringLiteral("{"), 95.0, 55.0, math::MathNodeType::Open, delimiterOptions);
  require(tallBrace != nullptr && !tallBrace->children.empty(), QStringLiteral("tall left brace should use stacked delimiter pieces"));
  require(tallBrace->height + tallBrace->depth > 80.0, QStringLiteral("stacked delimiter should grow to target height"));
  std::unique_ptr<math::MathRenderNode> tallBracket =
      math::MathDelimiter::makeLeftRight(QStringLiteral("["), 95.0, 55.0, math::MathNodeType::Open, delimiterOptions);
  require(tallBracket != nullptr && !tallBracket->svgPath.isEmpty() && tallBracket->viewBox.width() > 0.0,
          QStringLiteral("tall bracket delimiter should use KaTeX SVG geometry"));
  require(!math::MathSvgGeometry::tallDelimiterPath(QStringLiteral("lbrack"), 1000).isEmpty(),
          QStringLiteral("KaTeX tall delimiter SVG path should be generated"));
  require(!math::MathSvgGeometry::sqrtPath(QStringLiteral("sqrtTall"), 0.0, 3200).isEmpty(),
          QStringLiteral("KaTeX sqrt SVG path should be generated"));

  requireMathSnippetLayout(QStringLiteral("\\left\\lbrace\\frac{\\frac{a}{b}}{\\frac{c}{d}}\\right\\rbrace"), QStringLiteral("stacked brace delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\left[\\begin{matrix}a\\\\\\frac{b}{c}\\\\d\\end{matrix}\\right]"), QStringLiteral("stacked bracket matrix delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\left|\\frac{a}{\\frac{b}{c}}\\right|+\\left\\langle\\frac{x}{y}\\right\\rangle"), QStringLiteral("vertical and angle delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\bigl( a \\bigr)+\\Bigl[ b \\Bigr]+\\biggl\\lbrace c \\biggr\\rbrace+\\Biggl| d \\Biggr|"), QStringLiteral("sized delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\begin{cases}x^2,&x>0\\\\0,&x=0\\end{cases}+\\begin{smallmatrix}a&b\\\\c&d\\end{smallmatrix}"),
                           QStringLiteral("cases and smallmatrix environments"));
  requireMathSnippetLayout(QStringLiteral("\\begin{aligned}a&=b+c\\\\d&=e+f\\end{aligned}+\\begin{gathered}x+y\\\\z\\end{gathered}"),
                           QStringLiteral("aligned and gathered environments"));
  requireMathSnippetLayout(QStringLiteral("\\begin{array}{|c:rl@{}c|}\\hline\\hdashline a&b&c&d\\cr e&f&g&h\\\\[0.4em]\\hline\\end{array}"),
                           QStringLiteral("array preamble separators cr and row gap"));

  {
    math::MathParser parser(QStringLiteral("\\begin{array}{|c:rl@{}c|}\\hline\\hdashline a&b&c&d\\cr e&f&g&h\\\\[0.4em]\\hline\\end{array}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::Array, QStringLiteral("array parser should produce array node"));
    const math::MathParseNode& array = nodes.first();
    int separators = 0;
    for (const math::MathArrayColumn& column : array.columns) {
      if (column.type == math::MathArrayColumn::Type::Separator) {
        ++separators;
      }
    }
    require(separators >= 3, QStringLiteral("array preamble should preserve solid and dashed separators"));
    require(array.arrayLines.size() == 3 && array.arrayLines.at(1).dashed, QStringLiteral("array should preserve multiple hline and hdashline entries"));
    require(!array.rowGaps.isEmpty() && array.rowGaps.last() > 0.0, QStringLiteral("array row gap should parse optional newline size"));
  }
}

void testStrictMathGeometryFeatures2() {
  // Operators, VList, supsub, cr, array row geometry, paint
  math::MathSettings settings;
  math::MathOptions displayOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
  {
    math::MathParser parser(QStringLiteral("\\sum\\nolimits_{i=1}^n+\\lim\\limits_{x\\to0}+\\mathop{AB}\\limits^c"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::SupSub,
            QStringLiteral("sum with nolimits should parse as supsub"));
    require(!nodes.first().base.isEmpty() && nodes.first().base.first().type == math::MathNodeType::Operator,
            QStringLiteral("sum supsub base should remain an operator"));
    require(nodes.first().base.first().explicitLimits && !nodes.first().base.first().limits,
            QStringLiteral("nolimits should explicitly disable operator limits"));

    bool sawExplicitLim = false;
    bool sawMathopBody = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::SupSub && !node.base.isEmpty() && node.base.first().type == math::MathNodeType::Operator) {
        const math::MathParseNode& op = node.base.first();
        if (op.label == QStringLiteral("\\lim") && op.explicitLimits && op.limits) {
          sawExplicitLim = true;
        }
        if (op.label == QStringLiteral("\\mathop") && !op.body.isEmpty() && op.explicitLimits && op.limits) {
          sawMathopBody = true;
        }
      }
    }
    require(sawExplicitLim && sawMathopBody, QStringLiteral("limits should apply to lim and mathop operators"));

    math::MathOptions displayOpts(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> displaySum = math::MathBuilder(displayOpts).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("\\sum"), settings).parse().first()});
    math::MathOptions textOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> textSum = math::MathBuilder(textOptions).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("\\sum"), settings).parse().first()});
    require(!displaySum->children.empty() && displaySum->children.front()->fontClass == QStringLiteral("size2"),
            QStringLiteral("display large operator should use KaTeX Size2 font"));
    require(!textSum->children.empty() && textSum->children.front()->fontClass == QStringLiteral("size1"),
            QStringLiteral("text large operator should use KaTeX Size1 font"));
    require(displaySum->height + displaySum->depth > textSum->height + textSum->depth,
            QStringLiteral("display large operator should be taller than text style operator"));

    std::vector<math::MathVListChild> individualChildren;
    individualChildren.push_back(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0), 3.0});
    individualChildren.push_back(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5), -2.0});
    auto individual = math::makeLayoutVListIndividualShift(std::move(individualChildren));
    require(individual->children.size() == 2 &&
                qAbs(individual->children.front()->shift - 3.0) < 0.01 &&
                qAbs(individual->children.back()->shift + 2.0) < 0.01 &&
                qAbs(individual->height - 3.0) < 0.01 &&
                qAbs(individual->depth - 4.0) < 0.01,
            QStringLiteral("native individualShift VList should match KaTeX getVListChildrenAndDepth semantics"));

    std::vector<math::MathVListEntry> shiftEntries;
    shiftEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    auto shifted = math::makeLayoutVListShift(0.5, std::move(shiftEntries));
    require(shifted->children.size() == 1 &&
                qAbs(shifted->children.front()->shift - 0.5) < 0.01 &&
                qAbs(shifted->height - 1.5) < 0.01 &&
                qAbs(shifted->depth - 1.5) < 0.01,
            QStringLiteral("native shift VList should position baseline relative to first child like KaTeX"));

    std::vector<math::MathVListEntry> bottomEntries;
    bottomEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    bottomEntries.push_back(math::makeLayoutVListKern(0.5));
    bottomEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5)}));
    auto bottom = math::makeLayoutVListBottom(2.2, std::move(bottomEntries));
    require(qAbs(bottom->height - 2.8) < 0.01 && qAbs(bottom->depth - 2.2) < 0.01,
            QStringLiteral("native bottom VList should expose KaTeX maxPos/minPos height and depth"));

    std::vector<math::MathVListEntry> topEntries;
    topEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    topEntries.push_back(math::makeLayoutVListKern(0.5));
    topEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5)}));
    auto top = math::makeLayoutVListTop(3.0, std::move(topEntries));
    require(qAbs(top->height - 3.0) < 0.01 && qAbs(top->depth - 2.0) < 0.01,
            QStringLiteral("native top VList should derive bottom from total child stack like KaTeX"));

    const QVector<math::MathParseNode> integralNodes = math::MathParser(QStringLiteral("\\int_0^1"), settings).parse();
    require(!integralNodes.isEmpty() && integralNodes.first().type == math::MathNodeType::SupSub,
            QStringLiteral("integral limits syntax should parse as supsub"));
    require(!integralNodes.first().base.isEmpty() && integralNodes.first().base.first().type == math::MathNodeType::Operator,
            QStringLiteral("integral supsub base should remain an operator"));
    require(integralNodes.first().base.first().label == QStringLiteral("\\int") &&
                integralNodes.first().base.first().opSymbol &&
                !integralNodes.first().base.first().limits,
            QStringLiteral("integral operators should match KaTeX op.ts no-limits symbol category"));

    std::unique_ptr<math::MathRenderNode> integralSupSub = math::MathBuilder(displayOptions).buildExpression(integralNodes);
    std::unique_ptr<math::MathRenderNode> sumLimits =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\sum_0^1"), settings).parse());
    require(integralSupSub != nullptr && !integralSupSub->children.empty() &&
                integralSupSub->children.front()->kind == math::MathRenderKind::Span &&
                integralSupSub->children.front()->children.size() == 2 &&
                integralSupSub->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("integral should render side scripts through KaTeX-style base plus msupsub VList"));
    require(sumLimits != nullptr && !sumLimits->children.empty() &&
                sumLimits->children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("sum should render display limits through native KaTeX VList layout"));
    const math::MathRenderNode& sumVList = *sumLimits->children.front();
    require(sumVList.children.size() == 3, QStringLiteral("sum limits VList should contain base, superscript, and subscript"));
    bool sawLimitBase = false;
    bool sawUpperLimit = false;
    bool sawLowerLimit = false;
    const std::function<bool(const math::MathRenderNode*)> containsSize2 =
        [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) {
        return false;
      }
      if (node->fontClass == QStringLiteral("size2")) {
        return true;
      }
      for (const auto& child : node->children) {
        if (containsSize2(child.get())) {
          return true;
        }
      }
      return false;
    };
    for (const auto& child : sumVList.children) {
      if (containsSize2(child.get())) {
        sawLimitBase = true;
      } else if (child->shift < 0.0) {
        sawUpperLimit = true;
      } else if (child->shift > 0.0) {
        sawLowerLimit = true;
      }
    }
    require(sawLimitBase && sawUpperLimit && sawLowerLimit,
            QStringLiteral("operator limits should preserve KaTeX baseShift and upper/lower individual shifts"));
    const math::MathRenderNode& integralScripts = *integralSupSub->children.front();
    const math::MathRenderNode& integralScriptVList = *integralScripts.children.back();
    require(integralScriptVList.children.size() == 2 &&
                integralScriptVList.children.front()->shift > 0.0 &&
                integralScriptVList.children.back()->shift < 0.0,
            QStringLiteral("ordinary supsub VList should preserve KaTeX sub and sup individual shifts"));
    require(integralScriptVList.children.front()->xOffset < integralScriptVList.children.back()->xOffset,
            QStringLiteral("integral side scripts should use KaTeX side-script italic correction"));
    require(integralScriptVList.width > integralScriptVList.children.back()->xOffset + integralScriptVList.children.back()->width,
            QStringLiteral("ordinary supsub VList width should include KaTeX scriptspace marginRight"));

    std::unique_ptr<math::MathRenderNode> integralThenOrd =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\int_0^1x"), settings).parse());
    require(integralThenOrd != nullptr && integralThenOrd->children.size() >= 2,
            QStringLiteral("integral followed by ord should build adjacent atoms"));
    require(integralThenOrd->children.front()->width >= integralScripts.width,
            QStringLiteral("parent hlist should advance past the ordinary supsub script box"));

    std::unique_ptr<math::MathRenderNode> italicSupSub =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("y_i^2"), settings).parse());
    require(italicSupSub != nullptr && !italicSupSub->children.empty() &&
                italicSupSub->children.front()->kind == math::MathRenderKind::Span &&
                italicSupSub->children.front()->children.size() == 2 &&
                italicSupSub->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("single italic base should render supsub through KaTeX-style msupsub VList"));
    const math::MathRenderNode& italicScripts = *italicSupSub->children.front();
    const math::MathRenderNode& italicScriptVList = *italicScripts.children.back();
    require(italicScriptVList.children.size() == 2 &&
                italicScriptVList.children.front()->xOffset < italicScriptVList.children.back()->xOffset,
            QStringLiteral("superscript should include base italic correction while subscript backs it out"));

    std::unique_ptr<math::MathRenderNode> groupedSup =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("{xy}^2"), settings).parse());
    std::unique_ptr<math::MathRenderNode> charSup =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("x^2"), settings).parse());
    require(groupedSup != nullptr && charSup != nullptr && !groupedSup->children.empty() && !charSup->children.empty(),
            QStringLiteral("character-box supsub comparison should build"));
    require(groupedSup->height >= charSup->height,
            QStringLiteral("non-character-box superscript should apply supDrop-derived shift"));
  }
  {
    math::MathOptions displayOpts(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    QVector<math::MathParseNode> topLevelCrNodes = math::MathParser(QStringLiteral("a\\\\b"), settings).parse();
    require(topLevelCrNodes.size() == 3 && topLevelCrNodes.at(1).type == math::MathNodeType::Cr,
            QStringLiteral("top-level double backslash should parse as KaTeX cr node"));
    std::unique_ptr<math::MathRenderNode> topLevelCr =
        math::MathBuilder(displayOpts).buildExpression(topLevelCrNodes);
    std::unique_ptr<math::MathRenderNode> topLevelCrGap =
        math::MathBuilder(displayOpts).buildExpression(math::MathParser(QStringLiteral("a\\\\[0.4em]b"), settings).parse());
    require(topLevelCr != nullptr && topLevelCr->kind == math::MathRenderKind::VList && topLevelCr->children.size() == 2,
            QStringLiteral("top-level cr should render as native KaTeX newline VList"));
    require(topLevelCrGap->height + topLevelCrGap->depth > topLevelCr->height + topLevelCr->depth + displayOpts.fontPointSize() * 0.3,
            QStringLiteral("top-level cr optional size should increase native newline gap"));

    std::unique_ptr<math::MathRenderNode> defaultArray =
        math::MathBuilder(displayOpts).buildExpression(math::MathParser(QStringLiteral("\\begin{array}{c}a\\\\b\\end{array}"), settings).parse());
    std::unique_ptr<math::MathRenderNode> gapArray =
        math::MathBuilder(displayOpts).buildExpression(math::MathParser(QStringLiteral("\\begin{array}{c}a\\\\[0.4em]b\\end{array}"), settings).parse());
    require(defaultArray != nullptr && gapArray != nullptr,
            QStringLiteral("array row gap comparison should build"));
    require(gapArray->height + gapArray->depth > defaultArray->height + defaultArray->depth + displayOpts.fontPointSize() * 0.3,
            QStringLiteral("positive array row gap should include KaTeX arstrutDepth plus requested gap"));
    require(!defaultArray->children.empty() && defaultArray->children.front()->kind == math::MathRenderKind::Array,
            QStringLiteral("array individualShift comparison should unwrap root hlist"));
    const math::MathRenderNode& arrayNode = *defaultArray->children.front();
    require(!arrayNode.children.empty() && arrayNode.children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("array should build a KaTeX-style table body VList"));
    require(!arrayNode.children.front()->children.empty() &&
                arrayNode.children.front()->children.front()->kind == math::MathRenderKind::Span &&
                !arrayNode.children.front()->children.front()->children.empty() &&
                arrayNode.children.front()->children.front()->children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("array columns should be native KaTeX column VLists"));
    const math::MathRenderNode& firstColumn = *arrayNode.children.front()->children.front()->children.front();
    require(firstColumn.children.size() >= 2,
            QStringLiteral("array individualShift comparison should find row wrapper cells"));
    const qreal baselineDelta = firstColumn.children.at(1)->shift - firstColumn.children.at(0)->shift;
    require(qAbs(baselineDelta - (firstColumn.children.at(0)->depth + firstColumn.children.at(1)->height)) < displayOpts.fontPointSize() * 0.2,
            QStringLiteral("array row baselines should follow KaTeX makeVList individualShift row.pos-offset semantics"));
    require(!firstColumn.children.at(0)->children.empty() &&
                firstColumn.children.at(0)->height > firstColumn.children.at(0)->children.front()->height,
            QStringLiteral("array row wrapper should keep KaTeX arstrut height separate from real cell content"));
  }

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\left(\\frac{a}{b}+\\sqrt{\\frac{c}{d}}+\\sqrt{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}\\right)"
      "+\\widehat{abcdef}+\\widetilde{abcdef}+\\overleftrightarrow{AB}"
      "+\\begin{array}{rcl}\\hline x&=&1\\\\longname&=&\\frac{a}{b}\\\\\\hline\\end{array}"
      "+\\begin{cases}x,&x>0\\\\0,&x=0\\end{cases}+\\begin{aligned}a&=b\\\\c&=d\\end{aligned}"
      "+\\binom{n}{k}+\\underline{text}+\\overline{abc}+\\text{hello}"
      "+\\underbrace{x+y}_{n}+\\overbrace{a+b}^{m}+\\underleftrightarrow{AB}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("strict geometry parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 900.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("strict geometry math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("strict geometry math block should have native layout"));
  require(blockLayout->mathLayout()->size.height() > theme.mathFont().pointSizeF() * 3.0,
          QStringLiteral("left/right and array should increase native math height"));

  QImage image(QSize(860, qCeil(blockLayout->height()) + 40), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 12.0);
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 120, QStringLiteral("strict geometry math paint should draw visible pixels"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testStrictMathGeometryFeatures1);
  RUN_TEST(testStrictMathGeometryFeatures2);
#undef RUN_TEST
  return 0;
}
