#include "document/MarkdownDocument.h"
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

void testMathRenderingLayout() {
  RenderTheme theme = RenderTheme::github();

  CmarkGfmParser parser;
  ParseResult parsed = parser.parseDocument(QStringLiteral("before $x_i^2 + \\frac{a}{b}$ after\n\n$$\n\\sqrt{x} = \\frac{1}{2}\n$$"), {});
  require(parsed.root != nullptr, QStringLiteral("math render parse should produce document"));

  DocumentLayout documentLayout;
  MarkdownDocument document;
  document.setMarkdownText(QStringLiteral("before $x_i^2 + \\frac{a}{b}$ after\n\n$$\n\\sqrt{x} = \\frac{1}{2}\n$$"), std::move(parsed.root));
  documentLayout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("math block should parse"));
  const BlockLayout* mathBlockLayout = documentLayout.block(mathBlock->id());
  require(mathBlockLayout != nullptr, QStringLiteral("math block layout should exist"));
  require(mathBlockLayout->mathLayout() != nullptr && mathBlockLayout->mathLayout()->valid(), QStringLiteral("math block should have native math layout"));
  require(qAbs(mathBlockLayout->height() -
               std::ceil(mathBlockLayout->mathLayout()->size.height() + theme.codePadding().top() + theme.codePadding().bottom())) <= 1.0,
          QStringLiteral("inactive math block height should be native formula height plus block padding"));
  require(!mathBlockLayout->literalEditing(), QStringLiteral("inactive math block should render native formula instead of literal source"));

  const math::MathLayoutResult blockFormulaLayout = math::MathRenderer().render(QStringLiteral("\\sqrt{x} = \\frac{1}{2}"), theme, true);
  require(blockFormulaLayout.valid(), QStringLiteral("display math renderer should render sample formula directly"));
  require(qAbs(blockFormulaLayout.size.width() - blockFormulaLayout.root->width) < 0.01 &&
              qAbs(blockFormulaLayout.size.height() - (blockFormulaLayout.root->height + blockFormulaLayout.root->depth)) < 0.01,
          QStringLiteral("display math renderer should expose pure KaTeX root bbox without block padding"));

  HitTestResult inactiveMathHit = mathBlockLayout->hitTest(mathBlockLayout->rect().center(), theme);
  require(inactiveMathHit.zone == HitTestResult::Zone::Math, QStringLiteral("inactive math block hit should target math zone"));
  require(inactiveMathHit.cursorRect.left() == mathBlockLayout->rect().right() || inactiveMathHit.cursorRect.left() == mathBlockLayout->rect().left(),
          QStringLiteral("inactive math block cursor should be atomic at block edge"));

  SelectionRange mathEditingSelection;
  mathEditingSelection.anchor.blockId = mathBlock->id();
  mathEditingSelection.anchor.text.nodeId = mathBlock->id();
  mathEditingSelection.focus = mathEditingSelection.anchor;
  DocumentLayout editingDocumentLayout;
  editingDocumentLayout.rebuild(document, theme, 800.0, mathEditingSelection);
  const BlockLayout* editingMathBlockLayout = editingDocumentLayout.block(mathBlock->id());
  require(editingMathBlockLayout != nullptr && editingMathBlockLayout->literalEditing(),
          QStringLiteral("focused math block should enter literal editing layout"));
  require(editingMathBlockLayout->height() > mathBlockLayout->height() + theme.codeLineHeight() * 2.0,
          QStringLiteral("focused math block should reserve both TeX source editor and rendered preview"));
  const QPointF editingSourcePoint(editingMathBlockLayout->rect().left() + theme.codePadding().left() + QFontMetricsF(theme.codeFont()).horizontalAdvance(QStringLiteral("\\sqrt")),
                                   editingMathBlockLayout->rect().top() + theme.codePadding().top() + theme.codeLineHeight() * 1.5);
  HitTestResult editingMathHit = editingMathBlockLayout->hitTest(editingSourcePoint, theme);
  require(editingMathHit.zone == HitTestResult::Zone::Math &&
              editingMathHit.cursorRect.left() > editingMathBlockLayout->rect().left() &&
              editingMathHit.cursorRect.left() < editingMathBlockLayout->rect().right(),
          QStringLiteral("editing math block cursor should use literal source coordinates"));
  require(editingMathHit.textOffset > 0 && editingMathHit.textOffset < editingMathBlockLayout->literal().size(),
          QStringLiteral("editing math block hit-test should map into TeX content, not decorative dollar delimiters"));

  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  require(paragraph != nullptr, QStringLiteral("math inline paragraph should parse"));
  const BlockLayout* paragraphLayout = documentLayout.block(paragraph->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("math inline layout should exist"));
  require(!paragraphLayout->inlineLayout()->displayText().contains(QStringLiteral("\\frac")), QStringLiteral("collapsed inline math should render as atom"));
  require(paragraphLayout->inlineLayout()->mathAtomCount() == 1, QStringLiteral("collapsed inline math should create one native math atom"));

  const QString userInlineMathSample =
      QStringLiteral("行内公式使用单美元符号：$E = mc^2$ 和 $a_1 + b_1 = c_1$。");
  ParseResult userParsed = parser.parseDocument(userInlineMathSample, {});
  require(userParsed.root != nullptr, QStringLiteral("user inline math sample should parse"));
  MarkdownDocument userDocument;
  userDocument.setMarkdownText(userInlineMathSample, std::move(userParsed.root));
  DocumentLayout userLayout;
  userLayout.rebuild(userDocument, theme, 800.0);
  const MarkdownNode* userParagraph = findFirstBlock(userDocument.root(), BlockType::Paragraph);
  require(userParagraph != nullptr, QStringLiteral("user inline math sample paragraph should exist"));
  int inlineMathCount = 0;
  for (const InlineNode& inlineNode : userParagraph->inlines()) {
    if (inlineNode.type() == InlineType::InlineMath) {
      ++inlineMathCount;
    }
  }
  require(inlineMathCount == 2, QStringLiteral("single dollar sample should parse two inline math nodes"));
  const BlockLayout* userParagraphLayout = userLayout.block(userParagraph->id());
  require(userParagraphLayout != nullptr && userParagraphLayout->inlineLayout() != nullptr,
          QStringLiteral("user inline math sample should have inline layout"));
  require(userParagraphLayout->inlineLayout()->mathAtomCount() == 2,
          QStringLiteral("single dollar sample should create two native math atoms"));
  require(!userParagraphLayout->inlineLayout()->displayText().contains(QLatin1Char('$')) &&
              !userParagraphLayout->inlineLayout()->displayText().contains(QStringLiteral("mc^2")) &&
              !userParagraphLayout->inlineLayout()->displayText().contains(QStringLiteral("a_1")),
          QStringLiteral("inactive single dollar math should be collapsed to native atoms"));
  {
    const QString inlineMathSample = QStringLiteral("before $E=mc^2$ after");
    ParseResult inlineParsed = parser.parseDocument(inlineMathSample, {});
    require(inlineParsed.root != nullptr, QStringLiteral("inline math exact placeholder sample should parse"));
    MarkdownDocument inlineDocument;
    inlineDocument.setMarkdownText(inlineMathSample, std::move(inlineParsed.root));
    DocumentLayout inlineLayoutDocument;
    inlineLayoutDocument.rebuild(inlineDocument, theme, 800.0);
    const MarkdownNode* inlineParagraph = findFirstBlock(inlineDocument.root(), BlockType::Paragraph);
    require(inlineParagraph != nullptr, QStringLiteral("inline math exact placeholder paragraph should exist"));
    const BlockLayout* inlineParagraphLayout = inlineLayoutDocument.block(inlineParagraph->id());
    require(inlineParagraphLayout != nullptr && inlineParagraphLayout->inlineLayout() != nullptr,
            QStringLiteral("inline math exact placeholder layout should exist"));
    const qsizetype formulaStart = QStringLiteral("before ").size();
    const qsizetype formulaEnd = formulaStart + QStringLiteral("E=mc^2").size();
    const qreal reservedWidth = inlineParagraphLayout->inlineLayout()->cursorRect(formulaEnd).left() -
                                inlineParagraphLayout->inlineLayout()->cursorRect(formulaStart).left();
    const math::MathLayoutResult formulaLayout = math::MathRenderer().render(QStringLiteral("E=mc^2"), theme, false);
    require(formulaLayout.valid() && qAbs(reservedWidth - formulaLayout.size.width()) < 1.5,
            QStringLiteral("collapsed inline math placeholder should reserve the native math box width exactly"));

    const QColor transparent(Qt::transparent);
    QImage directImage(QSize(qCeil(formulaLayout.size.width()) + 32, qCeil(formulaLayout.size.height()) + 32), QImage::Format_ARGB32);
    directImage.fill(transparent);
    {
      QPainter painter(&directImage);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      formulaLayout.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect directInk = imageInkBounds(directImage, transparent);
    require(!directInk.isEmpty(), QStringLiteral("direct inline formula paint should produce ink"));

    QVector<InlineNode> mathOnlyInlines;
    mathOnlyInlines.push_back(InlineNode::inlineMath(QStringLiteral("E=mc^2")));
    InlineLayout mathOnlyLayout;
    mathOnlyLayout.build(mathOnlyInlines, QStringLiteral("$E=mc^2$"), theme, 360.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
    require(mathOnlyLayout.mathAtomCount() == 1, QStringLiteral("isolated inline formula should collapse to one math atom"));
    require(mathOnlyLayout.displayText().size() == 1, QStringLiteral("isolated inline formula should use a single QTextLayout placeholder"));

    QImage inlineImage(QSize(360, qCeil(mathOnlyLayout.height()) + 32), QImage::Format_ARGB32);
    inlineImage.fill(transparent);
    {
      QPainter painter(&inlineImage);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      mathOnlyLayout.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect inlineInk = imageInkBounds(inlineImage, transparent);
    require(!inlineInk.isEmpty(), QStringLiteral("inline layout formula paint should produce ink"));

    const qreal isolatedReservedWidth = mathOnlyLayout.cursorRect(QStringLiteral("E=mc^2").size()).left() - mathOnlyLayout.cursorRect(0).left();
    require(qAbs(isolatedReservedWidth - formulaLayout.size.width()) < 1.5,
            QStringLiteral("isolated inline formula cursor span should match native formula width"));
    require(qAbs(inlineInk.width() - directInk.width()) <= 2,
            QStringLiteral("inline formula painted ink width should match direct native formula paint"));
    require(qAbs((inlineInk.left() - 16) - (directInk.left() - 16)) <= 2,
            QStringLiteral("inline formula painted ink should keep the same left bearing as direct native paint"));
  }
  {
    math::MathRenderNode rule;
    rule.kind = math::MathRenderKind::Rule;
    rule.width = 48.0;
    rule.height = 2.0;
    rule.ruleThickness = 2.0;
    rule.shift = -12.0;
    rule.color = theme.textColor();

    const QColor transparent(Qt::transparent);
    QImage ruleImage(QSize(96, 64), QImage::Format_ARGB32);
    ruleImage.fill(transparent);
    {
      QPainter rulePainter(&ruleImage);
      rule.paint(rulePainter, QPointF(16.0, 32.0));
    }
    const QRect ruleInk = imageInkBounds(ruleImage, transparent);
    require(!ruleInk.isEmpty(), QStringLiteral("horizontal rule paint should produce ink"));
    require(ruleInk.width() >= 46, QStringLiteral("fraction-style horizontal rule should paint across its full width"));
    require(qAbs(ruleInk.top() - 30) <= 1,
            QStringLiteral("horizontal rule paint should not apply node shift a second time"));
  }

  QImage image(QSize(480, qCeil(mathBlockLayout->height()) + 30), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  mathBlockLayout->paint(painter, theme, mathBlockLayout->rect().top() - 10.0);
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
  require(changedPixels > 50, QStringLiteral("math block paint should draw visible pixels"));
}

void testExtendedMathFunctionRendering() {
  RenderTheme theme = RenderTheme::github();

  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n\\hat{x}+\\bar{y}+\\vec{v}+\\Bigl(\\operatorname{rank}_A B\\Bigr)+"
      "\\sum_{i=1}^{n} i+\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("extended math parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("extended math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("extended math block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 120.0, QStringLiteral("extended math layout should include function width"));
  require(blockLayout->mathLayout()->size.height() > theme.mathFont().pointSizeF() * 2.0,
          QStringLiteral("extended math layout should include scripts/array height"));

  QImage image(QSize(760, qCeil(blockLayout->height()) + 30), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 10.0);
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
  require(changedPixels > 100, QStringLiteral("extended math paint should draw visible pixels"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testMathRenderingLayout);
  RUN_TEST(testExtendedMathFunctionRendering);
#undef RUN_TEST
  return 0;
}
