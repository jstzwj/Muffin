#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "math/MathBuilder.h"
#include "math/MathDelimiter.h"
#include "math/MathFontMetrics.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathMacroExpander.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"
#include "math/MathSvgGeometry.h"
#include "math/MathSymbols.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>

#include <functional>

using namespace muffin;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) {
    fail(message);
  }
}

int changedPixelCount(const QImage& image, QColor background);

QString readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("Could not open fixture: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

const MarkdownNode* findFirstBlock(const MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstBlock(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findBlockWithLiteral(const MarkdownNode& node, BlockType type, const QString& literalPart) {
  if (node.type() == type && node.literal().contains(literalPart)) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findBlockWithLiteral(*child, type, literalPart)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findFirstTableCell(const MarkdownNode& node) {
  if (node.type() == BlockType::TableCell) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstTableCell(*child)) {
      return found;
    }
  }
  return nullptr;
}

MarkdownNode* mutableBlockAt(MarkdownDocument& document, qsizetype index) {
  auto& children = document.root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), QStringLiteral("mutable block index out of range"));
  return children.at(static_cast<size_t>(index)).get();
}

void replaceDocumentText(MarkdownDocument& document, const QString& markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("incremental test parse returned null root"));
  document.setMarkdownText(markdown, std::move(parsed.root));
}

void requireUsableRect(const QRectF& rect, const QString& label) {
  require(rect.isValid(), QStringLiteral("%1 rect is invalid").arg(label));
  require(rect.width() > 20.0, QStringLiteral("%1 rect width too small").arg(label));
  require(rect.height() > 10.0, QStringLiteral("%1 rect height too small").arg(label));
}

void testIncrementalBlockRebuildContract() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"), false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const NodeId firstParagraphId = mutableBlockAt(session.document(), 0)->id();
  const NodeId secondParagraphId = mutableBlockAt(session.document(), 1)->id();
  const QRectF firstBefore = layout.block(firstParagraphId)->rect();
  const QRectF secondBefore = layout.block(secondParagraphId)->rect();
  const qreal totalBefore = layout.totalHeight();

  require(session.applyTextDelta(
              5,
              0,
              QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"
                             " alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"),
              true,
              {LocalEditNodeHint{firstParagraphId, 0, BlockType::Paragraph}}),
          QStringLiteral("paragraph local delta should apply"));
  const DocumentLayout::BlockRebuildResult paragraphResult = layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(paragraphResult.rebuilt, QStringLiteral("paragraph block rebuild should succeed"));
  require(paragraphResult.blockId == firstParagraphId, QStringLiteral("paragraph rebuild result id mismatch"));
  require(paragraphResult.oldRect == firstBefore, QStringLiteral("paragraph rebuild old rect mismatch"));
  require(paragraphResult.newRect == layout.block(firstParagraphId)->rect(), QStringLiteral("paragraph rebuild new rect mismatch"));
  require(paragraphResult.heightDelta != 0, QStringLiteral("paragraph rebuild should report height delta"));
  require(!paragraphResult.shiftedRect.isEmpty(), QStringLiteral("paragraph rebuild should report shifted rect"));
  require(layout.block(secondParagraphId)->rect().top() != secondBefore.top(), QStringLiteral("paragraph rebuild should shift following block"));
  require(layout.totalHeight() != totalBefore, QStringLiteral("paragraph rebuild should update total height"));

  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(tableCell != nullptr, QStringLiteral("incremental table cell missing"));
  MarkdownNode* table = mutableBlockAt(session.document(), 2);
  require(layout.block(tableCell->id()) != nullptr, QStringLiteral("layout index should include table cell"));
  const QRectF tableBefore = layout.block(table->id())->rect();
  const DocumentLayout::BlockRebuildResult tableResult = layout.rebuildBlock(tableCell->id(), session.document(), theme, {});
  require(tableResult.rebuilt, QStringLiteral("table cell rebuild should map to top-level table"));
  require(tableResult.blockId == table->id(), QStringLiteral("table cell rebuild should report table block id"));
  require(tableResult.oldRect == tableBefore, QStringLiteral("table cell rebuild old rect mismatch"));
  require(tableResult.newRect == layout.block(table->id())->rect(), QStringLiteral("table cell rebuild new rect mismatch"));
}

void testInlineMarkerExpansion() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), QVector<InlineNode>{InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  RenderTheme theme = RenderTheme::github();
  InlineLayout collapsed;
  collapsed.build(inlines, theme, 400.0, theme.paragraphFont());
  require(!collapsed.displayText().contains(QStringLiteral("**")), QStringLiteral("collapsed inline should hide strong markers"));

  InlineLayout expanded;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorVisibleOffset = 8;
  options.projectionState.cursorSourceOffset = 10;
  expanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), options);
  require(expanded.displayText().contains(QStringLiteral("**")), QStringLiteral("active inline should show strong markers"));
  require(expanded.plainText() == QStringLiteral("before bold after"), QStringLiteral("expanded plain text should stay collapsed"));
  require(expanded.hitTestTextOffset(expanded.cursorRect(8).center()) == 8,
          QStringLiteral("expanded hit test should map display marker offsets back to visible offsets"));
  require(expanded.hitTestSourceOffset(expanded.cursorRectForSourceOffset(9).center()) == 9,
          QStringLiteral("expanded hit test should map opener marker display to source offset"));

  QVector<InlineNode> mathInlines;
  mathInlines.push_back(InlineNode::inlineMath(QStringLiteral("a123")));
  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.projectionState.cursorSourceOffset = 2;
  math.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.hitTestSourceOffset(math.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("math cursor rect should round-trip source offset after first char"));

  InlineLayout selectionExpanded;
  InlineLayout::BuildOptions selectionOptions;
  selectionOptions.projectionState.selectionVisibleStart = 8;
  selectionOptions.projectionState.selectionVisibleEnd = 10;
  selectionExpanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), selectionOptions);
  require(selectionExpanded.displayText().contains(QStringLiteral("**")), QStringLiteral("selection touching inline should show strong markers"));
}

void testInlineProjectionContract() {
  RenderTheme theme = RenderTheme::github();
  InlineLayout::BuildOptions options;
  const QVector<InlineNode> linkInlines{
      InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("label"))})};
  const QString linkMarkdown = QStringLiteral("[label](https://example.com)");

  InlineLayout collapsedLink;
  collapsedLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedLink.displayText() == QStringLiteral("label"), QStringLiteral("inactive link projection text mismatch"));
  require(!collapsedLink.displayText().contains(QStringLiteral("](")), QStringLiteral("inactive link should not render source syntax"));

  InlineLayout::BuildOptions activeLinkOptions = options;
  activeLinkOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeLink;
  activeLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), activeLinkOptions);
  require(activeLink.displayText() == linkMarkdown, QStringLiteral("active link projection text mismatch"));
  require(activeLink.displayText().contains(QStringLiteral("](")), QStringLiteral("active link should render source syntax"));
  require(activeLink.hitTestSourceOffset(activeLink.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active link cursor rect should round-trip source offset"));
  require(!activeLink.selectionRects(0, 5).isEmpty(), QStringLiteral("active link selection rects should remain valid"));

  const QVector<InlineNode> imageInlines{InlineNode::image(QStringLiteral("https://example.com/image.png"), QStringLiteral("alt"), QString())};
  const QString imageMarkdown = QStringLiteral("![alt](https://example.com/image.png)");

  InlineLayout collapsedImage;
  collapsedImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedImage.displayText() == QStringLiteral("alt"), QStringLiteral("inactive image projection text mismatch"));
  require(!collapsedImage.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive image should not render source syntax"));

  InlineLayout::BuildOptions activeImageOptions = options;
  activeImageOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeImage;
  activeImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), activeImageOptions);
  require(activeImage.displayText() == imageMarkdown, QStringLiteral("active image projection text mismatch"));
  require(activeImage.displayText().contains(QStringLiteral("![")), QStringLiteral("active image should render source syntax"));
  require(activeImage.hitTestSourceOffset(activeImage.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active image cursor rect should round-trip source offset"));
  require(!activeImage.selectionRects(0, 3).isEmpty(), QStringLiteral("active image selection rects should remain valid"));
}

void testInlineLayoutGeometryContract() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta")));

  InlineLayout plain;
  plain.build(plainInlines, theme, 130.0, theme.paragraphFont());
  require(plain.height() > 0.0, QStringLiteral("inline layout should measure plain inline height"));
  require(plain.size().height() == plain.height(), QStringLiteral("inline layout size should expose measured height"));

  const QRectF plainCursor = plain.cursorRect(6);
  require(!plainCursor.isEmpty(), QStringLiteral("inline layout cursor rect should be available"));
  require(plain.hitTestTextOffset(QPointF(plainCursor.left(), plainCursor.center().y())) == 6,
          QStringLiteral("inline layout cursor point should round-trip plain offset"));
  require(!plain.selectionRects(1, 18).isEmpty(), QStringLiteral("inline layout selection rects should be available"));

  QVector<InlineNode> styledInlines;
  styledInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  styledInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::code(QStringLiteral("code")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;
  InlineLayout styled;
  styled.build(styledInlines, QStringLiteral("before **bold** [link](u) `code`"), theme, 180.0, theme.paragraphFont(), options);
  require(styled.height() > 0.0, QStringLiteral("inline layout should measure styled inline height"));
  require(styled.displayText() == QStringLiteral("before **bold** [link](u) `code`"), QStringLiteral("styled display text should match projection"));
  const QRectF styledCursor = styled.cursorRect(9);
  require(!styledCursor.isEmpty(), QStringLiteral("inline layout styled cursor rect should be available"));
  const qsizetype styledHit = styled.hitTestTextOffset(QPointF(styledCursor.left(), styledCursor.center().y()));
  require(styledHit >= 0 && styledHit <= styled.plainText().size(), QStringLiteral("inline layout hit-test should return a valid styled offset"));
  require(!styled.selectionRects(0, styled.plainText().size()).isEmpty(), QStringLiteral("inline layout should produce styled selection rects"));
}

void testInlineLayoutPainting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::code(QStringLiteral("code")));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;

  InlineLayout layout;
  layout.build(inlines, QStringLiteral("before **bold** [link](https://example.com) `code` after"), theme, 280.0, theme.paragraphFont(), options);
  require(layout.height() > 0.0, QStringLiteral("inline layout paint fixture should have height"));

  QImage image(QSize(320, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  layout.paint(painter, QPointF(8.0, 8.0));
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
  require(changedPixels > 25, QStringLiteral("inline layout paint should draw visible pixels"));
}

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
  require(mathBlockLayout->height() >= mathBlockLayout->mathLayout()->size.height(), QStringLiteral("math block height should include math layout"));

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

void testStrictMathGeometryFeatures() {
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
  {
    math::MathSettings settings;
    settings.displayMode = true;
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

    math::MathOptions displayOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> displaySum = math::MathBuilder(displayOptions).buildExpression(
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

void testMathMetricsMacrosAndState() {
  require(math::MathFontMetrics::loaded(), QStringLiteral("KaTeX font metrics data should load from resources"));
  require(math::MathFontMetrics::characterMetrics(QStringLiteral("Math-Italic"), QStringLiteral("x")).has_value(),
          QStringLiteral("KaTeX Math-Italic metrics should include x"));
  require(math::MathSvgGeometry::loaded(), QStringLiteral("KaTeX SVG geometry data should load from resources"));
  require(math::MathSvgGeometry::hasPath(QStringLiteral("rightarrow")), QStringLiteral("KaTeX SVG geometry should include rightarrow"));
  require(!math::MathSvgGeometry::painterPath(QStringLiteral("rightarrow"), QRectF(0.0, 0.0, 40.0, 8.0)).isEmpty(),
          QStringLiteral("KaTeX SVG geometry should convert rightarrow path to QPainterPath"));

  {
    math::MathMacroExpander expander;
    require(expander.expand(QStringLiteral("{\\def\\foo{A}\\foo}+\\foo")) == QStringLiteral("{A}+\\foo"),
            QStringLiteral("macro expander should restore local definitions at group end"));
    require(expander.expand(QStringLiteral("{\\global\\def\\bar{B}\\bar}+\\bar")) == QStringLiteral("{B}+B"),
            QStringLiteral("macro expander should preserve global definitions across groups"));
    require(expander.expand(QStringLiteral("\\def\\baz#1#2{#2#1}\\baz{A}{B}")) == QStringLiteral("BA"),
            QStringLiteral("macro expander should substitute primitive macro arguments"));
    require(expander.expand(QStringLiteral("\\let\\copy\\baz\\copy{C}{D}")) == QStringLiteral("DC"),
            QStringLiteral("macro expander should support let aliases"));
    require(expander.expand(QStringLiteral("\\futurelet\\next\\first\\second")) == QStringLiteral("\\first\\second"),
            QStringLiteral("macro expander should keep futurelet following tokens"));
    require(expander.expand(QStringLiteral("\\def\\a{A}\\def\\b{B}\\expandafter\\a\\b")) == QStringLiteral("AB"),
            QStringLiteral("macro expander should support token stack expandafter"));
    require(expander.expand(QStringLiteral("\\def\\a{A}\\noexpand\\a")) == QStringLiteral("\\relax"),
            QStringLiteral("macro expander should support noexpand relax treatment"));
    require(expander.expand(QStringLiteral("\\char`A")) == QStringLiteral("\\text{A}"),
            QStringLiteral("macro expander should lower char primitive to renderable text"));
    require(expander.expand(QStringLiteral("\\pmod{n}+\\iff+\\implies+\\And+\\alef")).contains(QStringLiteral("\\Longleftrightarrow")),
            QStringLiteral("macro expander should include high frequency KaTeX macros"));
  }
  {
    math::MathSettings settings;
    settings.maxExpand = 8;
    bool threw = false;
    try {
      math::MathMacroExpander expander(settings);
      expander.expand(QStringLiteral("\\def\\loop{\\loop}\\loop"));
    } catch (const math::MathParseError& error) {
      threw = true;
      require(error.message().contains(QStringLiteral("Too many expansions")), QStringLiteral("recursive macro should throw maxExpand error"));
      require(error.position() == QStringLiteral("\\def\\loop{\\loop}").size() && error.endPosition() == QStringLiteral("\\def\\loop{\\loop}\\loop").size(),
              QStringLiteral("recursive macro maxExpand error should preserve macro token source range"));
    }
    require(threw, QStringLiteral("macro expander should enforce maxExpand"));
  }

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\newcommand{\\RR}{R}\\RR+\\newcommand{\\pair}[2]{\\left(#1,#2\\right)}\\pair{a}{b}+\\def\\sq#1{#1^2}\\sq{x}+"
      "{\\def\\local{L}\\local}+\\local+{\\global\\def\\globalMacro{G}\\globalMacro}+\\globalMacro+\\let\\same\\sq\\same{y}+"
      "\\pmod{n}+\\bmod x+\\iff+\\implies+\\impliedby+\\And+\\alef+\\char`A+\\hbox{box}+\\html@mathml{H}{M}+"
      "\\mathbb{R}+\\mathcal{C}+\\mathfrak{g}+\\mathscr{S}+\\boldsymbol{x}+\\rm{roman}+"
      "\\textcolor{red}{\\frac{1}{2}}+\\color{blue} x+y"
      "+\\scriptstyle \\frac{a}{b}+\\normalsize \\phantom{abc}+\\hphantom{wide}+\\vphantom{\\frac{1}{2}}"
      "+\\smash[t]{\\frac{1}{2}}+\\rule[0.2em]{1em}{0.1em}+a\\kern1em b+\\mathrel{\\star}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("metrics/macro/state math parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("metrics/macro/state math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("metrics/macro/state block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 40.0, QStringLiteral("metrics/macro/state layout should have measurable width"));
}

void testRemainingKatexFunctionFamilies() {
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
    bool sawGenfracWrapper = false;
    bool sawTextbf = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::LeftRight && !node.body.isEmpty() && node.body.first().type == math::MathNodeType::Fraction) {
        sawGenfracWrapper = node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]") &&
                            node.body.first().lineThickness > 0.0 && node.body.first().style == QStringLiteral("\\displaystyle");
      }
      if (node.type == math::MathNodeType::Text && node.fontClass == QStringLiteral("mathbf")) {
        sawTextbf = true;
      }
    }
    require(sawGenfracWrapper, QStringLiteral("genfrac should parse delimiters line thickness style numerator and denominator"));
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
    const std::function<bool(const math::MathRenderNode*)> hasTallSqrt = [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) {
        return false;
      }
      if (node->pathName == QStringLiteral("sqrtTall")) {
        return true;
      }
      for (const auto& child : node->children) {
        if (hasTallSqrt(child.get())) {
          return true;
        }
      }
      return false;
    };
    require(hasTallSqrt(tallSqrt.get()), QStringLiteral("large sqrt should use KaTeX tall sqrt SVG geometry"));
  }
  {
    math::MathParser parser(QStringLiteral("{a+b\\over c+d}+{n\\choose k}+{x\\atop y}+{p\\brace q}+{r\\brack s}+{u\\above 0.08em v}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    int sawFractions = 0;
    bool sawChooseDelims = false;
    bool sawBraceDelims = false;
    bool sawBrackDelims = false;
    bool sawAtopNoBar = false;
    bool sawAboveRule = false;
    const std::function<void(const math::MathParseNode&)> scanInfix = [&](const math::MathParseNode& node) {
      const math::MathParseNode* candidate = &node;
      if (node.type == math::MathNodeType::LeftRight && !node.body.isEmpty()) {
        candidate = &node.body.first();
        if (node.leftDelim == QStringLiteral("(") && node.rightDelim == QStringLiteral(")")) {
          sawChooseDelims = true;
        }
        if (node.leftDelim == QStringLiteral("\\lbrace") && node.rightDelim == QStringLiteral("\\rbrace")) {
          sawBraceDelims = true;
        }
        if (node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]")) {
          sawBrackDelims = true;
        }
      }
      if (candidate->type == math::MathNodeType::Fraction) {
        ++sawFractions;
        if (candidate->lineThickness == 0.0) {
          sawAtopNoBar = true;
        }
        if (candidate->lineThickness > 0.0) {
          sawAboveRule = true;
        }
      }
      for (const math::MathParseNode& child : node.body) {
        scanInfix(child);
      }
      for (const math::MathParseNode& child : node.base) {
        scanInfix(child);
      }
    };
    for (const math::MathParseNode& node : nodes) {
      scanInfix(node);
    }
    require(sawFractions >= 6 && sawChooseDelims && sawBraceDelims && sawBrackDelims && sawAtopNoBar && sawAboveRule,
            QStringLiteral("infix genfrac commands should rewrite to fractions with KaTeX delimiter and rule semantics"));

    math::MathSettings settings;
    math::MathOptions options(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> noBar = math::MathBuilder(options).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("{x\\atop y}")).parse().first()});
    const std::function<const math::MathRenderNode*(const math::MathRenderNode*)> findFraction =
        [&](const math::MathRenderNode* node) -> const math::MathRenderNode* {
      if (node == nullptr) {
        return nullptr;
      }
      if (node->kind == math::MathRenderKind::Fraction) {
        return node;
      }
      for (const auto& child : node->children) {
        if (const math::MathRenderNode* found = findFraction(child.get())) {
          return found;
        }
      }
      return nullptr;
    };
    const math::MathRenderNode* noBarFraction = findFraction(noBar.get());
    require(noBarFraction != nullptr && noBarFraction->children.size() == 2,
            QStringLiteral("atop fraction should render without a rule child"));
  }
  {
    math::MathParser parser(QStringLiteral("\\@binrel{=}{x}+\\@binrel{+}{y}+\\overset{a}{=}+{ab}+\\boldsymbol{=}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    bool sawRelBinrel = false;
    bool sawBinBinrel = false;
    bool sawOversetRel = false;
    bool sawBoldRel = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathrel")) {
        if (!node.body.isEmpty() && node.body.first().type != math::MathNodeType::SupSub) {
          sawRelBinrel = true;
        }
        if (!node.body.isEmpty() && node.body.first().type == math::MathNodeType::SupSub) {
          sawOversetRel = true;
        }
        if (node.fontClass == QStringLiteral("mathbf")) {
          sawBoldRel = true;
        }
      }
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathbin")) {
        sawBinBinrel = true;
      }
    }
    require(sawRelBinrel && sawBinBinrel && sawOversetRel && sawBoldRel,
            QStringLiteral("mclass binrel and boldsymbol should preserve inferred math classes"));
  }
  {
    math::MathSettings settings;
    math::MathOptions textOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    const auto widthForWithOptions = [&](const QString& tex, math::MathOptions options) {
      math::MathParser parser(tex, settings);
      return math::MathBuilder(options).buildExpression(parser.parse())->width;
    };
    const auto widthFor = [&](const QString& tex) {
      return widthForWithOptions(tex, textOptions);
    };
    const qreal plusBetweenOrd = widthFor(QStringLiteral("a+b"));
    const qreal plusAtStart = widthFor(QStringLiteral("+b"));
    const qreal plainOrdPair = widthFor(QStringLiteral("ab"));
    require(plusBetweenOrd > plusAtStart && plusBetweenOrd > plainOrdPair,
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
    const QStringList symbolCommands{
        QStringLiteral("\\zeta"), QStringLiteral("\\Xi"), QStringLiteral("\\oplus"), QStringLiteral("\\supseteq"),
        QStringLiteral("\\approx"), QStringLiteral("\\Longleftrightarrow"), QStringLiteral("\\aleph"), QStringLiteral("\\clubsuit"),
        QStringLiteral("\\nless"), QStringLiteral("\\Finv"), QStringLiteral("\\dashrightarrow")};
    for (const QString& command : symbolCommands) {
      const math::MathSymbolInfo symbol = math::lookupSymbol(command);
    require(symbol.known, QStringLiteral("symbol table should include %1").arg(command));
    }
  }
  {
    math::MathParser parser(QStringLiteral("\\html@mathml{H}{M}+\\hbox{box}+\\rm roman"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    bool sawHtmlBranch = false;
    bool sawHbox = false;
    bool sawRmDeclaration = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\html@mathml") && !node.body.isEmpty()) {
        sawHtmlBranch = true;
      }
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\hbox")) {
        sawHbox = true;
      }
      if (node.type == math::MathNodeType::Text && node.label == QStringLiteral("\\rm") && !node.body.isEmpty()) {
        sawRmDeclaration = true;
      }
    }
    require(sawHtmlBranch && sawHbox && sawRmDeclaration,
            QStringLiteral("htmlmathml hbox and old font declarations should preserve KaTeX-like parse semantics"));
  }

  bool threw = false;
  try {
    math::MathParser parser(QStringLiteral("a+\\definitelyUnsupported"), strictSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.tokenText() == QStringLiteral("\\definitelyUnsupported"), QStringLiteral("unsupported command error should keep token text"));
    require(error.position() == 2, QStringLiteral("unsupported command error should keep token position"));
    require(error.endPosition() == 24, QStringLiteral("unsupported command error should keep token end position"));
  }
  require(threw, QStringLiteral("throwOnError should raise MathParseError for unsupported commands"));

  math::MathSettings strictHtmlSettings;
  strictHtmlSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\htmlClass{note}{q}"), strictHtmlSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("htmlExtension")), QStringLiteral("strict html extension should report category"));
    require(error.tokenText() == QStringLiteral("\\htmlClass") && error.position() == 0,
            QStringLiteral("strict error should include source token position"));
    require(error.endPosition() == QStringLiteral("\\htmlClass").size(), QStringLiteral("strict error should include source token end position"));
  }
  require(threw, QStringLiteral("strict error should throw on nonstrict html extension"));

  math::MathSettings strictUnknownSettings;
  strictUnknownSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\notInRegistryYet"), strictUnknownSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("unknownSymbol")), QStringLiteral("unknown command should report unknownSymbol"));
    require(error.tokenText() == QStringLiteral("\\notInRegistryYet"), QStringLiteral("unknownSymbol should keep command token"));
  }
  require(threw, QStringLiteral("strict unknown command should throw unknownSymbol"));

  math::MathSettings strictUnicodeSettings;
  strictUnicodeSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\u4e2d"), strictUnicodeSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("unicodeTextInMathMode")),
            QStringLiteral("strict unicode text in math should report category"));
    require(error.position() == 0, QStringLiteral("unicode strict error should keep token position"));
  }
  require(threw, QStringLiteral("strict unicode text in math mode should throw"));

  math::MathSettings strictAccentSettings;
  strictAccentSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\'{e}"), strictAccentSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextAccents")),
            QStringLiteral("text accent in math should report mathVsTextAccents"));
  }
  require(threw, QStringLiteral("strict text accent in math should throw"));

  math::MathSettings strictUnitSettings;
  strictUnitSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\mkern1em"), strictUnitSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextUnits")), QStringLiteral("mkern em should report mathVsTextUnits"));
  }
  require(threw, QStringLiteral("strict mkern with non-mu units should throw"));

  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\kern1mu"), strictUnitSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextUnits")), QStringLiteral("kern mu should report mathVsTextUnits"));
  }
  require(threw, QStringLiteral("strict kern with mu units should throw"));

  math::MathSettings untrustedSettings;
  untrustedSettings.throwOnError = true;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\href{https://example.com}{h}"), untrustedSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("not trusted")), QStringLiteral("untrusted href should throw trust error"));
    require(error.tokenText() == QStringLiteral("\\href") && error.position() == 0,
            QStringLiteral("trust error should include href token position"));
  }
  require(threw, QStringLiteral("untrusted href should be rejected when throwOnError is enabled"));

  math::MathSettings trustedSettings;
  trustedSettings.trustHandler = [](const math::MathTrustContext& context) {
    return context.command == QStringLiteral("\\href") && context.protocol == QStringLiteral("https");
  };
  math::MathParser trustedParser(QStringLiteral("\\href{https://example.com}{h}"), trustedSettings);
  const QVector<math::MathParseNode> trustedTree = trustedParser.parse();
  require(!trustedTree.isEmpty() && trustedTree.first().type == math::MathNodeType::Href,
          QStringLiteral("trusted href should parse to href node"));

  bool sawHtmlDataContext = false;
  math::MathSettings htmlDataTrustSettings;
  htmlDataTrustSettings.trustHandler = [&sawHtmlDataContext](const math::MathTrustContext& context) {
    sawHtmlDataContext = context.command == QStringLiteral("\\htmlData") &&
                         context.attributes.value(QStringLiteral("data-x")).trimmed() == QStringLiteral("1") &&
                         context.attributes.value(QStringLiteral("data-y")).trimmed() == QStringLiteral("two") &&
                         context.attribute == QStringLiteral("data-x") &&
                         context.value.trimmed() == QStringLiteral("1");
    return sawHtmlDataContext;
  };
  math::MathParser htmlDataParser(QStringLiteral("\\htmlData{x=1,y=two}{q}"), htmlDataTrustSettings);
  const QVector<math::MathParseNode> htmlDataTree = htmlDataParser.parse();
  require(sawHtmlDataContext && !htmlDataTree.isEmpty() && htmlDataTree.first().type == math::MathNodeType::Html,
          QStringLiteral("htmlData trust context should expose data attributes"));

  math::MathSettings htmlDataErrorSettings;
  htmlDataErrorSettings.throwOnError = true;
  htmlDataErrorSettings.trust = true;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\htmlData{broken}{q}"), htmlDataErrorSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("missing equals sign")), QStringLiteral("htmlData missing equals should throw"));
  }
  require(threw, QStringLiteral("htmlData missing equals should match KaTeX error behavior"));

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\mathchoice{D}{T}{S}{SS}+\\mathllap{L}x+\\mathrlap{R}y+\\mathclap{C}z"
      "+\\raisebox{0.35em}{a}+\\vcenter{\\frac{a}{b}}"
      "+x_i^2+x^{a}_{b}+\\sqrt[3]{\\frac{a}{b}}+\\genfrac{[}{]}{0.08em}{0}{a}{b}+\\boxed{z}+\\textbf{B}+\\mathit{x}+\\mathtt{code}"
      "+\\@binrel{=}{x}+\\@binrel{+}{y}+\\overset{a}{=}+\\boldsymbol{=}+\\zeta+\\Xi+\\oplus+\\supseteq+\\approx+\\Longleftrightarrow+\\clubsuit"
      "+\\cancel{x}+\\bcancel{y}+\\xcancel{z}+\\sout{text}+\\fbox{box}+\\colorbox{yellow}{cb}+\\fcolorbox{red}{yellow}{fcb}+\\phase{V}+\\angl{n}"
      "+\\href{https://example.com}{h}+\\url{https://example.com}+\\htmlClass{note}{q}"
      "+\\includegraphics[width=1em,height=0.8em,totalheight=1em,alt=img]{missing-file.png}"
      "+\\verb|a b|+\\verb*|c d|+\\tag{1}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("remaining KaTeX function families parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 900.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("remaining function math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("remaining function block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 80.0, QStringLiteral("remaining function layout should have measurable width"));

  QImage image(QSize(880, qCeil(blockLayout->height()) + 50), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 12.0);
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 160, QStringLiteral("remaining function paint should draw visible pixels"));
}

void requireTextLayoutCursorRoundTrip(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF cursor = layout.cursorRect(offset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  require(layout.hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == offset,
          label + QStringLiteral(" cursor-left hit-test should round-trip"));
}

void requireTextLayoutCharacterBias(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF leftCursor = layout.cursorRect(offset);
  const QRectF rightCursor = layout.cursorRect(offset + 1);
  require(!leftCursor.isEmpty() && !rightCursor.isEmpty(), label + QStringLiteral(" bias cursor rects should exist"));
  const qreal left = leftCursor.left();
  const qreal right = rightCursor.left();
  if (qAbs(right - left) < 0.5) {
    return;
  }
  const qreal quarter = left + (right - left) * 0.25;
  const qreal threeQuarter = left + (right - left) * 0.75;
  const qreal y = leftCursor.center().y();
  require(layout.hitTestTextOffset(QPointF(quarter, y)) == offset, label + QStringLiteral(" left half hit-test mismatch"));
  require(layout.hitTestTextOffset(QPointF(threeQuarter, y)) == offset + 1, label + QStringLiteral(" right half hit-test mismatch"));
}

void testInlineLayoutHitTesting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout::BuildOptions probeOptions;
  InlineLayout plain;
  plain.build(plainInlines, theme, 125.0, theme.paragraphFont(), probeOptions);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, plain.plainText().size()};
  for (qsizetype offset : offsets) {
    requireTextLayoutCursorRoundTrip(plain, offset, QStringLiteral("plain offset %1").arg(offset));
  }
  requireTextLayoutCharacterBias(plain, 0, QStringLiteral("plain first char"));
  bool testedWrappedBias = false;
  for (qsizetype offset = 1; offset + 1 < plain.plainText().size(); ++offset) {
    const QRectF leftCursor = plain.cursorRect(offset);
    const QRectF rightCursor = plain.cursorRect(offset + 1);
    if (!leftCursor.isEmpty() && !rightCursor.isEmpty() && qAbs(leftCursor.center().y() - rightCursor.center().y()) < 0.5 &&
        qAbs(leftCursor.left() - rightCursor.left()) >= 0.5) {
      requireTextLayoutCharacterBias(plain, offset, QStringLiteral("plain same-line char"));
      testedWrappedBias = true;
      break;
    }
  }
  require(testedWrappedBias, QStringLiteral("inline hit-test should find a same-line bias fixture"));

  const QVector<QRectF> wrappedSelection = plain.selectionRects(0, plain.plainText().size());
  require(wrappedSelection.size() >= 2, QStringLiteral("inline hit-test fixture should wrap across lines"));
  for (const QRectF& rect : wrappedSelection) {
    require(plain.hitTestTextOffset(QPointF(rect.left(), rect.center().y())) >= 0, QStringLiteral("wrapped line start hit should be valid"));
    require(plain.hitTestTextOffset(rect.center()) >= 0, QStringLiteral("wrapped line middle hit should be valid"));
    require(plain.hitTestTextOffset(QPointF(rect.right(), rect.center().y())) >= 0, QStringLiteral("wrapped line end hit should be valid"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = probeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  requireTextLayoutCursorRoundTrip(active, 7, QStringLiteral("active visible bold start"));
  require(active.hitTestSourceOffset(QPointF(active.cursorRectForSourceOffset(9).left(), active.cursorRectForSourceOffset(9).center().y())) == 9,
          QStringLiteral("active marker source hit-test should not drift"));
}

void testInlineLayoutCursorRects() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout layout;
  InlineLayout::BuildOptions options;
  layout.build(plainInlines, theme, 125.0, theme.paragraphFont(), options);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, layout.plainText().size()};
  for (qsizetype offset : offsets) {
    const QRectF cursor = layout.cursorRect(offset);
    require(!cursor.isEmpty(), QStringLiteral("inline cursor rect should be non-empty"));
  }

  bool testedMonotonic = false;
  for (qsizetype offset = 0; offset + 3 < layout.plainText().size(); ++offset) {
    const QRectF first = layout.cursorRect(offset);
    const QRectF second = layout.cursorRect(offset + 1);
    const QRectF third = layout.cursorRect(offset + 2);
    if (!first.isEmpty() && !second.isEmpty() && !third.isEmpty() &&
        qAbs(first.center().y() - second.center().y()) < 0.5 && qAbs(second.center().y() - third.center().y()) < 0.5) {
      require(first.left() <= second.left() && second.left() <= third.left(), QStringLiteral("inline cursor x should be monotonic on same line"));
      testedMonotonic = true;
      break;
    }
  }
  require(testedMonotonic, QStringLiteral("inline cursor test should find same-line offsets"));

  bool foundWrap = false;
  for (qsizetype offset = 0; offset + 1 < layout.plainText().size(); ++offset) {
    const QRectF previous = layout.cursorRect(offset);
    const QRectF next = layout.cursorRect(offset + 1);
    if (!previous.isEmpty() && !next.isEmpty() && next.center().y() > previous.center().y() + previous.height() * 0.5) {
      require(next.left() < previous.left(), QStringLiteral("inline cursor x should return toward line start after wrap"));
      foundWrap = true;
      break;
    }
  }
  require(foundWrap, QStringLiteral("inline cursor test should observe a wrapped line"));

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = options;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QRectF markerRect = active.cursorRectForSourceOffset(9);
  require(!markerRect.isEmpty(), QStringLiteral("inline source marker cursor rect should be non-empty"));
  require(active.hitTestSourceOffset(QPointF(markerRect.left(), markerRect.center().y())) == 9,
          QStringLiteral("inline source marker cursor rect should hit marker source offset"));
  const QRectF visibleRect = active.cursorRect(7);
  require(!visibleRect.isEmpty(), QStringLiteral("inline visible active cursor rect should be non-empty"));
  require(qAbs(visibleRect.center().y() - markerRect.center().y()) < qMax(visibleRect.height(), markerRect.height()),
          QStringLiteral("inline visible/source active cursor rects should stay on same line"));
}

void requireValidSelectionRects(const QVector<QRectF>& rects, const QString& label) {
  require(!rects.isEmpty(), label + QStringLiteral(" selection rects should not be empty"));
  for (const QRectF& rect : rects) {
    require(rect.width() > 0.0, label + QStringLiteral(" selection rect width should be positive"));
    require(rect.height() > 0.0, label + QStringLiteral(" selection rect height should be positive"));
  }
}

void testInlineLayoutSelectionRects() {
  RenderTheme theme = RenderTheme::github();

  QVector<InlineNode> shortInlines;
  shortInlines.push_back(InlineNode::text(QStringLiteral("alpha beta")));
  InlineLayout singleLine;
  singleLine.build(shortInlines, theme, 400.0, theme.paragraphFont());
  const QVector<QRectF> single = singleLine.selectionRects(1, 6);
  require(single.size() == 1, QStringLiteral("inline single-line selection should produce one rect"));
  requireValidSelectionRects(single, QStringLiteral("inline single-line"));
  const QVector<QRectF> reverseSingle = singleLine.selectionRects(6, 1);
  require(reverseSingle.size() == single.size(), QStringLiteral("inline reverse selection should keep rect count"));
  require(qAbs(reverseSingle.first().left() - single.first().left()) < 0.5 &&
              qAbs(reverseSingle.first().width() - single.first().width()) < 0.5,
          QStringLiteral("inline reverse selection should match forward rect"));
  require(singleLine.selectionRects(3, 3).isEmpty(), QStringLiteral("inline collapsed selection should return no rects"));

  QVector<InlineNode> wrappedInlines;
  wrappedInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));
  InlineLayout wrapped;
  wrapped.build(wrappedInlines, theme, 125.0, theme.paragraphFont());
  const QVector<QRectF> wrappedRects = wrapped.selectionRects(0, wrapped.plainText().size());
  require(wrappedRects.size() >= 2, QStringLiteral("inline wrapped selection should span multiple rects"));
  requireValidSelectionRects(wrappedRects, QStringLiteral("inline wrapped"));
  for (qsizetype i = 1; i < wrappedRects.size(); ++i) {
    require(wrappedRects.at(i).top() > wrappedRects.at(i - 1).top(), QStringLiteral("inline wrapped selection rects should move downward"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QVector<QRectF> visibleContent = active.selectionRects(7, 11);
  require(visibleContent.size() == 1, QStringLiteral("inline active visible content selection should stay single-line"));
  requireValidSelectionRects(visibleContent, QStringLiteral("inline active visible content"));
  const QRectF markerStart = active.cursorRectForSourceOffset(7);
  const QRectF markerEnd = active.cursorRectForSourceOffset(9);
  require(!markerStart.isEmpty() && !markerEnd.isEmpty(), QStringLiteral("inline active marker cursor rects should exist"));
  require(markerEnd.left() > markerStart.left(), QStringLiteral("inline active marker source rect should cover visible marker width"));
}

bool hasRole(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role) {
      return true;
    }
  }
  return false;
}

const BlockLayout::TableCellLayout* findTableCellLayout(const BlockLayout& tableLayout, NodeId cellId) {
  for (const BlockLayout::TableRowLayout& row : tableLayout.tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId == cellId) {
        return &cell;
      }
    }
  }
  return nullptr;
}

int changedPixelCount(const QImage& image, QColor background) {
  int changedPixels = 0;
  const QRgb backgroundRgb = background.rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (backgroundRgb & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  return changedPixels;
}

void testDocumentLayoutInlineLayoutContract() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("alpha `code` beta gamma delta epsilon zeta eta theta\n\n| A | B |\n| --- | --- |\n| `one` | two |"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 360.0);

  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(session.document().root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(paragraph != nullptr && table != nullptr && tableCell != nullptr, QStringLiteral("inline layout document fixture missing blocks"));

  const BlockLayout* paragraphLayout = layout.block(paragraph->id());
  const BlockLayout* tableLayout = layout.block(table->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("inline layout paragraph layout missing"));
  require(tableLayout != nullptr && tableLayout->type() == BlockType::Table, QStringLiteral("inline layout table layout missing"));
  const BlockLayout::TableCellLayout* cellLayout = findTableCellLayout(*tableLayout, tableCell->id());
  require(cellLayout != nullptr, QStringLiteral("inline layout table cell layout missing"));

  require(paragraphLayout->inlineLayout()->height() > 0.0, QStringLiteral("paragraph inline layout should have height"));
  require(paragraphLayout->inlineLayout()->size().height() == paragraphLayout->inlineLayout()->height(),
          QStringLiteral("paragraph inline layout size should expose height"));
  require(cellLayout->text.height() > 0.0, QStringLiteral("table cell inline layout should have height"));
  require(cellLayout->text.size().height() == cellLayout->text.height(), QStringLiteral("table cell inline layout size should expose height"));
  for (const BlockLayout::TableRowLayout& row : tableLayout->tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      require(!cell.text.displayText().contains(QLatin1Char('|')), QStringLiteral("table cell layout should hide markdown pipe separators"));
    }
  }

  QImage image(QSize(420, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  for (const auto& block : layout.blocks()) {
    block->paint(painter, theme, 0.0);
  }
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 100, QStringLiteral("document inline layout paint should draw visible pixels"));

  const HitTestResult paragraphHit = layout.hitTest(paragraphLayout->rect().center(), theme);
  require(paragraphHit.isValid() && paragraphHit.zone == HitTestResult::Zone::Text,
          QStringLiteral("document inline layout paragraph hit-test should hit text"));
  require(!paragraphLayout->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("document inline layout paragraph selection should be available"));

  const HitTestResult tableHit = layout.hitTest(cellLayout->rect.center(), theme);
  require(tableHit.isValid() && tableHit.zone == HitTestResult::Zone::TableCell,
          QStringLiteral("document inline layout table hit-test should hit table cell"));
  require(!tableLayout->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("document inline layout table selection should be available"));
}

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

void testTreeSitterCodeHighlighting() {
  TreeSitterHighlighter highlighter;
  require(highlighter.supportsLanguage(QStringLiteral("python")), QStringLiteral("python highlighting should be registered"));
  const QVector<CodeHighlightSpan> python = highlighter.highlight(
      QStringLiteral("python"),
      QStringLiteral("def greet(name):\n    return \"hello \" + name\n"));
  require(hasRole(python, CodeHighlightRole::Keyword), QStringLiteral("python should highlight keywords"));
  require(hasRole(python, CodeHighlightRole::Function), QStringLiteral("python should highlight functions"));
  require(hasRole(python, CodeHighlightRole::String), QStringLiteral("python should highlight strings"));

  const QVector<CodeHighlightSpan> cpp = highlighter.highlight(
      QStringLiteral("cpp"),
      QStringLiteral("#include <QApplication>\nint main() { return 0; }\n"));
  require(hasRole(cpp, CodeHighlightRole::Preprocessor), QStringLiteral("cpp should highlight preprocessor"));
  require(hasRole(cpp, CodeHighlightRole::Keyword), QStringLiteral("cpp should highlight keywords"));
  require(hasRole(cpp, CodeHighlightRole::Function), QStringLiteral("cpp should highlight functions"));
  const QString cppText = QStringLiteral("#include <QApplication>\nint main() { return 0; }\n");
  const QVector<CodeHighlightSpan> cppExact = highlighter.highlight(QStringLiteral("cpp"), cppText);
  const qsizetype returnStart = cppText.indexOf(QStringLiteral("return"));
  require(hasExactRoleSpan(cppExact, CodeHighlightRole::Keyword, returnStart, returnStart + 6),
          QStringLiteral("cpp keyword span should align exactly with return"));
}

void testLayoutForTheme(const MarkdownDocument& document, const RenderTheme& theme, const QString& themeName) {
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);

  require(layout.pageWidth() > 300.0, QStringLiteral("%1 page width too small").arg(themeName));
  require(layout.totalHeight() > 700.0, QStringLiteral("%1 total height should exceed smoke viewport").arg(themeName));
  require(!layout.blocks().empty(), QStringLiteral("%1 layout produced no blocks").arg(themeName));

  const MarkdownNode* heading = findFirstBlock(document.root(), BlockType::Heading);
  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(document.root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(document.root());
  const MarkdownNode* code = findBlockWithLiteral(document.root(), BlockType::CodeFence, QStringLiteral("return 0"));
  const MarkdownNode* math = findBlockWithLiteral(document.root(), BlockType::MathBlock, QStringLiteral("E = mc"));

  require(heading != nullptr, QStringLiteral("Heading node missing"));
  require(paragraph != nullptr, QStringLiteral("Paragraph node missing"));
  require(table != nullptr, QStringLiteral("Table node missing"));
  require(tableCell != nullptr, QStringLiteral("Table cell node missing"));
  require(code != nullptr, QStringLiteral("Code node missing"));
  require(math != nullptr, QStringLiteral("Math node missing"));

  requireUsableRect(layout.block(heading->id())->rect(), QStringLiteral("%1 heading").arg(themeName));
  requireUsableRect(layout.block(paragraph->id())->rect(), QStringLiteral("%1 paragraph").arg(themeName));
  requireUsableRect(layout.block(table->id())->rect(), QStringLiteral("%1 table").arg(themeName));
  requireUsableRect(layout.block(code->id())->rect(), QStringLiteral("%1 code").arg(themeName));
  requireUsableRect(layout.block(math->id())->rect(), QStringLiteral("%1 math").arg(themeName));
  require(!layout.block(code->id())->literal().endsWith(QLatin1Char('\n')),
          QStringLiteral("%1 code display literal should hide structural trailing newline").arg(themeName));
  require(!layout.block(code->id())->codeHighlightSpans().isEmpty(),
          QStringLiteral("%1 code block should have syntax highlight spans").arg(themeName));
  require(layout.block(math->id())->rect().height() >= 20.0,
          QStringLiteral("%1 math block height should leave room for displayed text").arg(themeName));

  const QRectF paragraphRect = layout.block(paragraph->id())->rect();
  const HitTestResult paragraphHit = layout.hitTest(paragraphRect.center(), theme);
  require(paragraphHit.isValid(), QStringLiteral("%1 paragraph hit invalid").arg(themeName));
  require(paragraphHit.zone == HitTestResult::Zone::Text, QStringLiteral("%1 paragraph hit should be text").arg(themeName));
  require(paragraphHit.blockId == paragraph->id(), QStringLiteral("%1 paragraph hit id mismatch").arg(themeName));

  const QRectF tableRect = layout.block(table->id())->rect();
  const HitTestResult tableHit = layout.hitTest(tableRect.center(), theme);
  require(tableHit.isValid(), QStringLiteral("%1 table hit invalid").arg(themeName));
  require(tableHit.zone == HitTestResult::Zone::TableCell, QStringLiteral("%1 table hit should be cell").arg(themeName));
  require(tableHit.tableRow >= 0 && tableHit.tableColumn >= 0, QStringLiteral("%1 table hit indices missing").arg(themeName));

  const QRectF codeRect = layout.block(code->id())->rect();
  const HitTestResult codeHit = layout.hitTest(codeRect.center(), theme);
  require(codeHit.isValid(), QStringLiteral("%1 code hit invalid").arg(themeName));
  require(codeHit.zone == HitTestResult::Zone::Code, QStringLiteral("%1 code hit should be code").arg(themeName));
  SelectionRange codeSelection;
  codeSelection.anchor.blockId = code->id();
  codeSelection.anchor.text.nodeId = code->id();
  codeSelection.anchor.text.textOffset = 0;
  codeSelection.focus.blockId = code->id();
  codeSelection.focus.text.nodeId = code->id();
  codeSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRects(codeSelection, theme).isEmpty(),
          QStringLiteral("%1 code selection rects should not be empty").arg(themeName));
  SelectionRange crossSelection;
  crossSelection.anchor.blockId = paragraph->id();
  crossSelection.anchor.text.nodeId = paragraph->id();
  crossSelection.anchor.text.textOffset = 0;
  crossSelection.focus.blockId = code->id();
  crossSelection.focus.text.nodeId = code->id();
  crossSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("%1 code cross-block selection rects should not be empty").arg(themeName));
  require(!layout.block(table->id())->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("%1 table cross-block selection rects should not be empty").arg(themeName));

  const QRectF mathRect = layout.block(math->id())->rect();
  const HitTestResult mathHit = layout.hitTest(mathRect.center(), theme);
  require(mathHit.isValid(), QStringLiteral("%1 math hit invalid").arg(themeName));
  require(mathHit.zone == HitTestResult::Zone::Math, QStringLiteral("%1 math hit should be math").arg(themeName));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("offscreen") : qgetenv("QT_QPA_PLATFORM"));
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QString markdown = readFixture(QString::fromLocal8Bit(argv[1]));
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  testInlineMarkerExpansion();
  testInlineProjectionContract();
  testInlineLayoutGeometryContract();
  testInlineLayoutPainting();
  testMathRenderingLayout();
  testExtendedMathFunctionRendering();
  testStrictMathGeometryFeatures();
  testMathMetricsMacrosAndState();
  testRemainingKatexFunctionFamilies();
  testInlineLayoutHitTesting();
  testInlineLayoutCursorRects();
  testInlineLayoutSelectionRects();
  testIncrementalBlockRebuildContract();
  testDocumentLayoutInlineLayoutContract();
  testTreeSitterCodeHighlighting();
  testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github"));
  testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint"));
  testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night"));
  return 0;
}
