#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

void testThemeCodeFontFallbackOrder() {
  const RenderTheme theme = RenderTheme::github();
  const QFont codeFont = theme.codeFont();
  require(!codeFont.family().isEmpty(), QStringLiteral("code font should resolve to an available family"));
  require(codeFont.styleHint() == QFont::Monospace, QStringLiteral("code font should keep monospace style hint"));
  require(qAbs(codeFont.pointSizeF() * 96.0 / 72.0 - 14.4) < 0.01, QStringLiteral("code font should be 14.4 CSS px at 100% zoom"));
  require(qAbs(theme.codeLineHeight() - 23.04) < 0.01, QStringLiteral("code line height should be 23.04 px at 100% zoom"));
}

void testThemeCodeHighlightPalette() {
  const RenderTheme theme = RenderTheme::github();
  require(theme.codeHighlightColor(CodeHighlightRole::Keyword).name() == QStringLiteral("#9b008b"),
          QStringLiteral("light code keyword color should match default purple"));
  require(theme.codeHighlightColor(CodeHighlightRole::Function).name() == QStringLiteral("#0000a8"),
          QStringLiteral("light code function color should match default blue"));
  require(theme.codeHighlightColor(CodeHighlightRole::String).name() == QStringLiteral("#a31515"),
          QStringLiteral("light code string color should match default red"));
  require(theme.codeHighlightColor(CodeHighlightRole::Preprocessor).name() == theme.textColor().name(),
          QStringLiteral("light code preprocessor color should stay close to plain text"));
  require(theme.codeHighlightColor(CodeHighlightRole::Type).name() == QStringLiteral("#008000"),
          QStringLiteral("light code type color should match default green"));
}

void testThemeManagerSupportsBuiltInThemes() {
  ThemeManager manager;
  const QStringList expectedThemes{
      QStringLiteral("github"),
      QStringLiteral("newsprint"),
      QStringLiteral("night"),
      QStringLiteral("pixyll"),
      QStringLiteral("whitey"),
  };

  // Built-ins must be present and in canonical order at the front, but custom
  // (user-loaded) themes may follow — the list is no longer required to be
  // exactly the five built-ins once JSON themes can be imported.
  const QStringList available = manager.availableThemes();
  require(available.size() >= expectedThemes.size(),
          QStringLiteral("Theme manager should expose at least the five built-in themes"));
  for (int i = 0; i < expectedThemes.size(); ++i) {
    require(available.at(i) == expectedThemes.at(i),
            QStringLiteral("Built-in %1 should remain at position %2").arg(expectedThemes.at(i)).arg(i));
  }
  for (const QString& name : expectedThemes) {
    require(manager.setTheme(name), QStringLiteral("Theme manager should accept %1").arg(name));
    require(manager.currentThemeName() == name, QStringLiteral("Theme manager should activate %1").arg(name));
    require(manager.currentTheme().zoomPercent() == 100, QStringLiteral("%1 theme should be constructible").arg(name));
  }
}

void testLayoutForTheme(const MarkdownDocument& document, const RenderTheme& theme, const QString& themeName) {
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);

  require(layout.pageWidth() > 300.0, QStringLiteral("%1 page width too small").arg(themeName));
  require(layout.totalHeight() > 700.0, QStringLiteral("%1 total height should exceed smoke viewport").arg(themeName));
  require(layout.slotCount() > 0, QStringLiteral("%1 layout produced no blocks").arg(themeName));

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
  require(layout.block(code->id())->literal() == code->literal(),
          QStringLiteral("%1 code display literal should preserve editable source text").arg(themeName));
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

void testThemeTypographyWeightStyleAndAlignment() {
  ThemeDefinition def;
  def.id = QStringLiteral("typography");
  def.colors.text = QColor(QStringLiteral("#333333"));
  def.colors.background = QColor(QStringLiteral("#ffffff"));
  def.typography.bodyAlignment = Qt::AlignJustify;
  def.typography.headingAlignment[0] = Qt::AlignHCenter;
  def.typography.headingFontWeight[0] = QFont::Normal;
  def.typography.headingFontWeightSet[0] = true;
  def.typography.headingItalic[2] = true;
  def.typography.headingItalicSet[2] = true;

  const RenderTheme theme = RenderTheme::fromDefinition(def);
  require(theme.textAlignment(BlockType::Paragraph) == Qt::AlignJustify,
          QStringLiteral("paragraphs should use body text alignment"));
  require(theme.textAlignment(BlockType::Heading, 1) == Qt::AlignHCenter,
          QStringLiteral("explicit heading alignment should override body alignment"));
  require(theme.textAlignment(BlockType::Heading, 2) == Qt::AlignJustify,
          QStringLiteral("unset heading alignment should inherit body alignment"));
  require(theme.headingFont(1).weight() == QFont::Normal,
          QStringLiteral("explicit heading font-weight: normal should suppress bold fallback"));
  require(!theme.headingFont(1).italic(), QStringLiteral("h1 should not be italic by default"));
  require(theme.headingFont(2).weight() >= QFont::Bold,
          QStringLiteral("unset heading font-weight should keep legacy bold fallback"));
  require(theme.headingFont(3).italic(), QStringLiteral("explicit heading italic should apply"));
}

void testListMarkerGapFloor() {
  ThemeDefinition def;
  def.colors.text = QColor(QStringLiteral("#333333"));
  def.colors.background = QColor(QStringLiteral("#ffffff"));
  ThemeElementStyle ul;
  ul.key = QStringLiteral("ul");
  ul.box.present = true;
  def.elementStyles.push_back(ul);
  const auto setUlIndent = [&](qreal left) { def.elementStyles.back().box.padding.setLeft(left); };
  // Small indent (phycat-style 13px): 0.2*13 = 2.6 is below the 4.5 floor.
  setUlIndent(13.0);
  const RenderTheme small = RenderTheme::fromDefinition(def);
  require(qAbs(small.listMarkerGap() - 4.5) < 0.01,
          QStringLiteral("small-indent gap floors at 4.5px, not 2.6"));
  // Large indent (github-style 30px): 0.2*30 = 6px beats the floor → unchanged.
  setUlIndent(30.0);
  def.spacing.listMarkerGap = 0.0;
  const RenderTheme large = RenderTheme::fromDefinition(def);
  require(qAbs(large.listMarkerGap() - 6.0) < 0.01,
          QStringLiteral("large-indent gap stays 6px (0.2*30), no regression"));
  // Explicit override wins regardless of indent.
  def.spacing.listMarkerGap = 12.0;
  const RenderTheme over = RenderTheme::fromDefinition(def);
  require(qAbs(over.listMarkerGap() - 12.0) < 0.01,
          QStringLiteral("explicit listMarkerGap overrides the auto value"));
}

// Two consecutive paragraphs: the inter-paragraph gap must reflect the theme's
// CSS `p { margin }` (collapsed to the bottom margin), not the legacy tight floor.
// Regression guard for phycat's `#write p { margin: 10px 10px }`.
qreal interParagraphGap(const QString& css) {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("first paragraph\n\nsecond paragraph\n"), false);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("gap"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> paras;
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::Paragraph) { paras.push_back(child.get()); }
  }
  require(paras.size() >= 2, QStringLiteral("fixture should contain two top-level paragraphs"));
  const BlockLayout* a = layout.block(paras.at(0)->id());
  const BlockLayout* b = layout.block(paras.at(1)->id());
  require(a != nullptr && b != nullptr, QStringLiteral("both paragraphs should be promoted"));
  return b->rect().top() - a->rect().bottom();
}

void testParagraphSpacingHonoursCssMargin() {
  const qreal tight = interParagraphGap(QStringLiteral("#write { color:#000000; }"));
  const qreal spaced = interParagraphGap(QStringLiteral("#write { color:#000000; } #write p { margin: 30px 10px; }"));
  require(tight > 0.0, QStringLiteral("baseline paragraph gap should be positive (=%1)").arg(tight));
  // CSS bottom margin 30px → the gap must grow well beyond the legacy tight floor.
  require(spaced > tight + 20.0,
          QStringLiteral("declared p margin-bottom must drive the inter-paragraph gap (tight=%1 spaced=%2)").arg(tight).arg(spaced));
  require(qAbs(spaced - 30.0) < 1.5,
          QStringLiteral("collapsed paragraph gap should equal the CSS bottom margin (spaced=%1)").arg(spaced));
}

void testAdjacentCssMarginsCollapseAcrossBlockTypes() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Heading\n\nParagraph\n"), false);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      QStringLiteral("#write { color:#000000; } h1 { margin: 20px 0 30px 0; } p { margin: 40px 0; }"),
      QStringLiteral("collapse"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(heading != nullptr && paragraph != nullptr, QStringLiteral("heading/paragraph fixture parsed"));
  const BlockLayout* h = layout.block(heading->id());
  const BlockLayout* p = layout.block(paragraph->id());
  require(h != nullptr && p != nullptr, QStringLiteral("heading/paragraph layout blocks exist"));
  const qreal gap = p->rect().top() - h->rect().bottom();
  require(qAbs(gap - 40.0) < 1.5,
          QStringLiteral("adjacent CSS margins should collapse to max(30,40), not sum to 70 (gap=%1)").arg(gap));
}

// Structural selectors reach the MARGIN path, not just the engine. `p + p` raises
// the SECOND paragraph's top margin (it follows a p) while the first is unchanged —
// the gap grows from the base `p { margin }` collapse to the larger `p + p` value.
// Without the node-aware margin wiring, the structural rule would silently not apply.
void testStructuralMarginSelectorAffectsLayout() {
  const qreal base = interParagraphGap(QStringLiteral("#write { color:#000000; } #write p { margin: 10px; }"));
  const qreal adj = interParagraphGap(QStringLiteral(
      "#write { color:#000000; } #write p { margin: 10px; } #write p + p { margin-top: 60px; }"));
  require(base > 0.0, QStringLiteral("base gap should be positive (=%1)").arg(base));
  require(adj > base + 30.0,
          QStringLiteral("p + p margin-top must grow the inter-paragraph gap (base=%1 adj=%2)").arg(base).arg(adj));
  require(qAbs(adj - 60.0) < 2.0,
          QStringLiteral("collapsed gap should equal the p+p margin-top 60 (adj=%1)").arg(adj));
}

void testBlockquoteCssBoxUsesPerSideBorderAndCompactNestedFlow() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      ">\n"
      ">\n"
      "> It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      ">\n"
      "> > Nested quote.\n"
      "\n"
      "After\n"), false);
  const QString css = QStringLiteral(
      "body { color:#333333; font-size:16px; }"
      "#write { color:#333333; }"
      "p, blockquote { margin: 0.8em 0; }"
      "blockquote { border-left: 4px solid #dfe2e5; padding: 0 15px; color: #777777; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("quote"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  QVector<const MarkdownNode*> quotes;
  QVector<const MarkdownNode*> quoteVeps;
  int quoteDepth = 0;
  std::function<void(const MarkdownNode&)> collectQuotes = [&](const MarkdownNode& node) {
    const bool isQuote = node.type() == BlockType::BlockQuote;
    if (isQuote) { ++quoteDepth; quotes.push_back(&node); }
    const SourceRange range = node.sourceRange();
    if (quoteDepth > 0 && node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart) {
      quoteVeps.push_back(&node);
    }
    for (const auto& child : node.children()) { collectQuotes(*child); }
    if (isQuote) { --quoteDepth; }
  };
  collectQuotes(session.document().root());
  require(quotes.size() == 2, QStringLiteral("fixture should contain outer and nested blockquotes"));
  require(!quoteVeps.isEmpty(), QStringLiteral("fixture should contain quote-internal virtual empty paragraphs"));
  const BlockLayout* outer = layout.block(quotes.at(0)->id());
  const BlockLayout* nested = layout.block(quotes.at(1)->id());
  require(outer != nullptr && nested != nullptr, QStringLiteral("blockquote layout blocks exist"));
  for (const MarkdownNode* vep : quoteVeps) {
    require(layout.block(vep->id()) == nullptr,
            QStringLiteral("quote virtual empty paragraph should not participate in normal render flow"));
  }

  const ThemeElementBoxStyle box = theme.elementBoxStyle(QStringLiteral("blockquote"));
  require(qAbs(box.borderLeftWidth - 4.0) < 0.01, QStringLiteral("border-left width captured"));
  require(box.borderTopWidth == 0.0 && box.borderRightWidth == 0.0 && box.borderBottomWidth == 0.0,
          QStringLiteral("border-left must not become a four-sided border"));

  const BlockLayout::CssBoxGeometry outerBox = outer->cssBoxGeometry(theme);
  require(qAbs(outerBox.contentBox.left() - (outer->rect().left() + 4.0 + 15.0)) < 0.5,
          QStringLiteral("blockquote content should be inset by left border + padding"));
  require(qAbs(outerBox.contentBox.right() - (outer->rect().right() - 15.0)) < 0.5,
          QStringLiteral("blockquote content should be inset by right padding only"));
  require(nested->rect().height() < 60.0,
          QStringLiteral("nested blockquote should stay compact, not reserve a large empty box (height=%1)").arg(nested->rect().height()));
  require(outer->rect().height() < 110.0,
          QStringLiteral("outer blockquote should omit virtual blank paragraphs from paint flow (height=%1)").arg(outer->rect().height()));

  SelectionRange focusedVep;
  focusedVep.anchor.blockId = quotes.at(0)->id();
  focusedVep.anchor.text.nodeId = quoteVeps.first()->id();
  focusedVep.anchor.text.sourceOffset = quoteVeps.first()->sourceRange().byteStart;
  focusedVep.focus = focusedVep.anchor;
  DocumentLayout focusedLayout;
  focusedLayout.rebuild(session.document(), theme, 800.0, focusedVep, QString());
  const BlockLayout* focusedOuter = focusedLayout.block(quotes.at(0)->id());
  require(focusedOuter != nullptr, QStringLiteral("focused blockquote layout exists"));
  require(qAbs(focusedOuter->cssBorderBox(theme).height() - outer->cssBorderBox(theme).height()) < 0.5,
          QStringLiteral("quote border height should not depend on focused virtual blank paragraph (unfocused=%1 focused=%2)")
              .arg(outer->cssBorderBox(theme).height()).arg(focusedOuter->cssBorderBox(theme).height()));
  require(focusedLayout.block(quoteVeps.first()->id()) == nullptr,
          QStringLiteral("focused quote virtual empty paragraph remains editor-only, not render-flow content"));

  QImage img(QSize(800, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  img.fill(QColor(QStringLiteral("#ffffff")).rgba());
  QPainter painter(&img);
  for (const BlockLayout* block : layout.promotedBlocks()) { block->paint(painter, theme, 0.0, nullptr); }
  painter.end();
  const QRectF r = outer->cssBorderBox(theme);
  const QColor leftPixel = QColor::fromRgba(img.pixel(qRound(r.left() + 2.0), qRound(r.center().y())));
  const QColor rightPixel = QColor::fromRgba(img.pixel(qRound(r.right() - 2.0), qRound(r.center().y())));
  require(leftPixel.name(QColor::HexRgb) == QStringLiteral("#dfe2e5"),
          QStringLiteral("left border should paint the declared blockquote border-left (got %1)").arg(leftPixel.name(QColor::HexRgb)));
  require(rightPixel.name(QColor::HexRgb) != QStringLiteral("#dfe2e5"),
          QStringLiteral("right edge must not paint a phantom border from border-left"));
}

void testBlockquoteListFlowStaysCompactUnderLazyPromotion() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral(
      "> Intro\n"
      ">\n"
      "> - one\n"
      ">   - nested\n"
      ">\n"
      "> Outro\n"
      "\n"
      "After\n"), false);
  const QString css = QStringLiteral(
      "body { color:#333333; font-size:16px; line-height:1.6; }"
      "#write { color:#333333; }"
      "p, blockquote, ul, ol { margin: 0.8em 0; }"
      "li>ol, li>ul { margin: 0 0; }"
      "ul, ol { padding-left: 30px; }"
      "blockquote { border-left: 4px solid #dfe2e5; padding: 0 15px; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("quote-list"), QString()));

  DocumentLayout eager;
  eager.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* quote = findFirstBlock(session.document().root(), BlockType::BlockQuote);
  require(quote != nullptr, QStringLiteral("fixture should contain a blockquote"));
  const BlockLayout* eagerQuote = eager.block(quote->id());
  require(eagerQuote != nullptr, QStringLiteral("eager quote layout exists"));
  const qreal eagerQuoteHeight = eagerQuote->rect().height();
  require(eagerQuoteHeight < 150.0,
          QStringLiteral("blockquote containing a list should stay compact, not include blank quote VEPs (height=%1)").arg(eagerQuoteHeight));

  QVector<const MarkdownNode*> listItems;
  collectListItems(session.document().root(), listItems);
  require(listItems.size() >= 2, QStringLiteral("fixture should contain parent and nested list items"));
  const BlockLayout* parentItem = eager.block(listItems.at(0)->id());
  const BlockLayout* nestedItem = eager.block(listItems.at(1)->id());
  require(parentItem != nullptr && nestedItem != nullptr, QStringLiteral("list item layouts exist"));
  require(nestedItem->rect().top() - parentItem->rect().bottom() < 1.0,
          QStringLiteral("tight nested list should not add extra vertical spacing inside blockquote (gap=%1)")
              .arg(nestedItem->rect().top() - parentItem->rect().bottom()));
  require(nestedItem->rect().left() > parentItem->rect().left(), QStringLiteral("nested list item should be indented"));

  DocumentLayout lazy;
  lazy.rebuild(session.document(), theme, 800.0, SelectionRange(), QString(), DocumentLayout::BuildPolicy::Lazy);
  require(lazy.slotCount() == eager.slotCount(), QStringLiteral("lazy/eager slot count should match"));
  const qreal estimatedHeight = lazy.slotHeight(lazy.topLevelIndexFor(quote->id()));
  lazy.buildAll(theme);
  const BlockLayout* promotedQuote = lazy.block(quote->id());
  require(promotedQuote != nullptr, QStringLiteral("lazy quote should promote"));
  require(qAbs(estimatedHeight - promotedQuote->rect().height()) < 1.5,
          QStringLiteral("lazy list/quote estimate should match promoted height (estimated=%1 promoted=%2)")
              .arg(estimatedHeight).arg(promotedQuote->rect().height()));
}

// Regression for the CSS cascade leak where `#write { padding: 30px;
// padding-bottom: 100px }` (github.css) injected padding-bottom:100px into
// every descendant. blockquote only declares the `padding` shorthand, so the
// leaked `padding-bottom` longhand survived and grew every quote ~112px (a
// border bar far taller than its text). Uses the REAL built-in github theme,
// which carries the offending `#write` rule — a synthetic CSS fixture would
// miss it entirely.
void testGithubBlockquoteNotPaddedByWriteLeak() {
  const RenderTheme theme = RenderTheme::github();
  const ThemeElementBoxStyle quoteBox = theme.elementBoxStyle(QStringLiteral("blockquote"));
  require(quoteBox.borderLeftWidth > 0.0, QStringLiteral("github blockquote declares border-left"));
  require(qAbs(quoteBox.padding.bottom()) < 1.0,
          QStringLiteral("github blockquote padding-bottom must not leak #write's 100px (got %1)").arg(quoteBox.padding.bottom()));
  require(qAbs(quoteBox.padding.top()) < 1.0,
          QStringLiteral("github blockquote padding-top should be 0 (got %1)").arg(quoteBox.padding.top()));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      ">\n"
      "> It can also contain formatting and nested quotes.\n"
      ">\n"
      "> > Nested quote.\n"
      "\n"
      "## Lists\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1000.0);
  const MarkdownNode* quote = findFirstBlock(session.document().root(), BlockType::BlockQuote);
  require(quote != nullptr, QStringLiteral("fixture should contain a blockquote"));
  const BlockLayout* outer = layout.block(quote->id());
  require(outer != nullptr, QStringLiteral("outer quote layout exists"));
  // Three short text lines + two collapsed 0.8em gaps + the nested quote must
  // stay well under the ~355px the leak produced; compact browser/Typora rhythm
  // for this content is roughly 100–130px.
  require(outer->rect().height() < 180.0,
          QStringLiteral("github blockquote must stay compact without the #write padding leak (height=%1)").arg(outer->rect().height()));
}

// Structural selectors resolve against the LIVE document tree, not the load-time
// prototype. `li:first-child`, `li + li` (adjacent sibling) and `p:has(img)`
// (descendant probe) each pick out nodes by position/contents — impossible on
// the prototype, which has no siblings or descendants. The node-aware getters
// (textColorForElement with a node) drive the real-tree cascade.
void testStructuralSelectorsResolveAgainstLiveTree() {
  const QString css = QStringLiteral(
      "#write { color:#222222; }"
      "#write p, #write li { color:#222222; }"
      "#write li:first-child { color:#ff0000; }"   // first list item → red
      "#write li + li { color:#0000ff; }"            // adjacent sibling li → blue
      "#write p:has(img) { color:#00aa00; }");       // paragraph containing an image → green
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("struct"), QString()));
  require(theme.hasStructuralRules(), QStringLiteral("li:first-child / li+li / p:has(img) ⇒ hasStructuralRules"));

  // List-item position selectors.
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("- one\n- two\n- three\n"), false);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800.0);
    QVector<const MarkdownNode*> items;
    collectListItems(session.document().root(), items);
    require(items.size() == 3, QStringLiteral("fixture should parse three list items"));
    require(theme.textColorForElement(QStringLiteral("li"), items.at(0)) == QColor(QStringLiteral("#ff0000")),
            QStringLiteral("first li should be red via :first-child"));
    require(theme.textColorForElement(QStringLiteral("li"), items.at(1)) == QColor(QStringLiteral("#0000ff")),
            QStringLiteral("second li should be blue via li + li"));
    require(theme.textColorForElement(QStringLiteral("li"), items.at(2)) == QColor(QStringLiteral("#0000ff")),
            QStringLiteral("third li should be blue via li + li"));
  }

  // :has(img) — a paragraph with an image vs a plain one.
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("![alt](x.png)\n\nplain text\n"), false);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800.0);
    const MarkdownNode* withImg = findFirstBlock(session.document().root(), BlockType::Paragraph);
    require(withImg != nullptr, QStringLiteral("image paragraph should exist"));
    const MarkdownNode* plain = withImg ? withImg->nextSibling() : nullptr;
    while (plain && plain->type() != BlockType::Paragraph) { plain = plain->nextSibling(); }
    require(plain != nullptr, QStringLiteral("plain paragraph should exist"));
    require(theme.textColorForElement(QStringLiteral("p"), withImg) == QColor(QStringLiteral("#00aa00")),
            QStringLiteral("paragraph with an image should be green via p:has(img)"));
    require(theme.textColorForElement(QStringLiteral("p"), plain) == QColor(QStringLiteral("#222222")),
            QStringLiteral("plain paragraph should stay the base colour"));
  }
}

void testFromDefinitionReproducesBuiltIns() {
  // The whole theme-unification design hinges on fromDefinition(definition(id))
  // reproducing the matching built-in factory exactly — otherwise switching the
  // editor to go through the definition registry would silently change colours.
  ThemeManager manager;
  struct Entry {
    QString id;
    RenderTheme factory;
  };
  const Entry entries[] = {
      {QStringLiteral("github"), RenderTheme::github()},
      {QStringLiteral("newsprint"), RenderTheme::newsprint()},
      {QStringLiteral("night"), RenderTheme::night()},
      {QStringLiteral("pixyll"), RenderTheme::pixyll()},
      {QStringLiteral("whitey"), RenderTheme::whitey()},
  };
  for (const Entry& e : entries) {
    const RenderTheme viaDef = RenderTheme::fromDefinition(manager.definition(e.id));
    require(viaDef.backgroundColor().name() == e.factory.backgroundColor().name(),
            QStringLiteral("%1 background via fromDefinition should match factory").arg(e.id));
    require(viaDef.textColor().name() == e.factory.textColor().name(),
            QStringLiteral("%1 text via fromDefinition should match factory").arg(e.id));
    require(viaDef.mutedTextColor().name() == e.factory.mutedTextColor().name(),
            QStringLiteral("%1 muted via fromDefinition should match factory").arg(e.id));
    require(viaDef.linkColor().name() == e.factory.linkColor().name(),
            QStringLiteral("%1 link via fromDefinition should match factory").arg(e.id));
    require(viaDef.codeBackgroundColor().name() == e.factory.codeBackgroundColor().name(),
            QStringLiteral("%1 code bg via fromDefinition should match factory").arg(e.id));
    require(viaDef.highlightBackgroundColor().name() == e.factory.highlightBackgroundColor().name(),
            QStringLiteral("%1 highlight via fromDefinition should match factory").arg(e.id));
    require(viaDef.codeBorderColor().name() == e.factory.codeBorderColor().name(),
            QStringLiteral("%1 code border via fromDefinition should match factory").arg(e.id));
    require(viaDef.quoteBorderColor().name() == e.factory.quoteBorderColor().name(),
            QStringLiteral("%1 quote border via fromDefinition should match factory").arg(e.id));
    require(viaDef.tableBorderColor().name() == e.factory.tableBorderColor().name(),
            QStringLiteral("%1 table border via fromDefinition should match factory").arg(e.id));
    require(viaDef.tableHeaderBackgroundColor().name() == e.factory.tableHeaderBackgroundColor().name(),
            QStringLiteral("%1 table header bg via fromDefinition should match factory").arg(e.id));
    require(viaDef.tableAlternateBackgroundColor().name() == e.factory.tableAlternateBackgroundColor().name(),
            QStringLiteral("%1 table alt bg via fromDefinition should match factory").arg(e.id));
    require(viaDef.selectionColor().name() == e.factory.selectionColor().name(),
            QStringLiteral("%1 selection via fromDefinition should match factory").arg(e.id));
    require(viaDef.spellCheckColor().name() == e.factory.spellCheckColor().name(),
            QStringLiteral("%1 spell-check via fromDefinition should match factory").arg(e.id));
    // Declared-only inline-code border: github (declares `border` on code) → 1px;
    // the other four declare none → 0. Both paths load the same CSS, so they match.
    require(qAbs(viaDef.inlineCodeBorderWidth() - e.factory.inlineCodeBorderWidth()) < 0.01,
            QStringLiteral("%1 inline-code border width via fromDefinition should match factory").arg(e.id));
  }
}

// Regression guard for the "narrow window squeezes content into a thin column
// with huge side margins" symptom reported on `#write { max-width; margin: 0
// auto; padding }` themes. Such a theme is a CSS page box whose
// horizontalInset is max(margin.left, margin.right) = 0, so the column must FILL
// the viewport at every width (matching the theme's intent) — never float as a thin centred
// column. Asserts the real DocumentLayout page-width math at 1000/600/400 px.
void testNarrowViewportFillsForCardTheme(const MarkdownDocument& document) {
  ThemeDefinition def;
  def.id = QStringLiteral("cardlike");
  def.label = QStringLiteral("Card-like");
  def.colors.text = QColor(QStringLiteral("#d6deeb"));
  def.colors.background = QColor(QStringLiteral("#0f111a"));
  def.page.viewportBackground = QColor(QStringLiteral("#0f111a"));
  def.page.pageMaxWidth = 950.0;                       // #write max-width: 950px
  def.page.pagePadding = QMarginsF(15, 15, 15, 15);    // #write padding: 15px  → cssPageBox
  def.page.pageMargin = QMarginsF(0, 0, 0, 0);         // margin: 0 auto → zero box
  def.page.pageMarginExplicit = true;
  const RenderTheme theme = RenderTheme::fromDefinition(def);

  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);
  require(layout.pageWidth() > 900.0, QStringLiteral("1000px viewport should fill (got %1)").arg(layout.pageWidth()));
  layout.rebuild(document, theme, 600.0);
  require(layout.pageWidth() > 500.0,
          QStringLiteral("600px viewport should fill to ~570, not squeeze (got %1)").arg(layout.pageWidth()));
  layout.rebuild(document, theme, 400.0);
  require(layout.pageWidth() > 300.0,
          QStringLiteral("400px viewport should fill to ~370, not squeeze (got %1)").arg(layout.pageWidth()));
}

// Paint a theme carrying decorations (h2 element bg gradient, h1::after underline,
// #write::before texture) end-to-end to an image. Guards the decoration paint
// hooks + GradientPainter + DecorationPainter against crashes when decorations
// are actually present (built-in themes carry none, so the other render tests
// only exercise the empty-decoration early-return paths).
void testDecoratedThemePaints(const MarkdownDocument& document) {
  ThemeDefinition def;
  def.id = QStringLiteral("decorated");
  def.colors.text = QColor(QStringLiteral("#d6deeb"));
  def.colors.background = QColor(QStringLiteral("#0f111a"));
  def.page.viewportBackground = QColor(QStringLiteral("#0f111a"));
  ElementBackground h2bg;
  h2bg.host = QStringLiteral("h2");
  h2bg.present = true;
  h2bg.gradient.kind = GradientSpec::Kind::Radial;
  h2bg.gradient.radialCenter = QPointF(0.5, 1.0);
  h2bg.gradient.stops = {{0.0, QColor(QStringLiteral("#00f3ff"))}, {1.0, QColor(Qt::transparent)}};
  def.decorations.backgrounds.push_back(h2bg);
  PseudoElementRule h1after;
  h1after.host = QStringLiteral("h1");
  h1after.pseudo = QStringLiteral("after");
  h1after.present = true;
  h1after.backgroundColor = QColor(QStringLiteral("#00f3ff"));
  h1after.size = QSizeF(40.0, 4.0);
  def.decorations.pseudos.push_back(h1after);
  PseudoElementRule wbefore;
  wbefore.host = QStringLiteral("#write");
  wbefore.pseudo = QStringLiteral("before");
  wbefore.present = true;
  wbefore.maskTint = QColor(QStringLiteral("#bd93f9"));
  wbefore.opacity = 0.05;
  wbefore.maskTile = QSizeF(20.0, 20.0);
  wbefore.maskPattern.kind = GradientSpec::Kind::Radial;
  wbefore.maskPattern.stops = {{0.0, QColor(QStringLiteral("#ffffff"))}, {1.0, QColor(Qt::transparent)}};
  def.decorations.pseudos.push_back(wbefore);
  const RenderTheme theme = RenderTheme::fromDefinition(def);

  DocumentLayout layout;
  layout.rebuild(document, theme, 800.0);
  QImage img(QSize(800, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  img.fill(QColor(QStringLiteral("#0f111a")).rgba());
  QPainter p(&img);
  DecorationPainter::paintWriteTexture(p, theme, QRectF(0, 0, 800, layout.totalHeight()));
  for (const BlockLayout* blk : layout.promotedBlocks()) {
    blk->paint(p, theme, 0.0, nullptr);
  }
  p.end();
  require(img.width() > 0 && img.height() > 0, QStringLiteral("decorated paint produced an image"));
  // A decoration drew something: at least one pixel must differ from the plain
  // viewport fill (the h2 glow / h1 underline inject non-background colour).
  const QRgb bg = QColor(QStringLiteral("#0f111a")).rgba();
  bool drew = false;
  for (int y = 0; y < img.height() && !drew; ++y) {
    for (int x = 0; x < img.width(); ++x) {
      if (img.pixel(x, y) != bg) { drew = true; break; }
    }
  }
  require(drew, QStringLiteral("decorations should paint non-background pixels"));
}

void testCodeBorderNeverRendersBlack() {
  // Regression: themes that declare no `border` on `code`/`.md-fences`
  // (Night, Pixyll, Newsprint, Whitey) left codeBorderColor invalid, and Qt
  // paints an unset QPen/QBrush as solid black — a black box around every
  // inline code span. deriveChromeDefaults now derives a soft edge off the code
  // background so the colour is always valid on every theme.
  const RenderTheme themes[] = {
      RenderTheme::github(), RenderTheme::newsprint(), RenderTheme::night(),
      RenderTheme::pixyll(), RenderTheme::whitey()};
  for (const RenderTheme& t : themes) {
    const QColor c = t.codeBorderColor();
    require(c.isValid(),
            QStringLiteral("codeBorderColor must be valid (an invalid colour paints as solid black)"));
    const bool pureBlack = c.red() == 0 && c.green() == 0 && c.blue() == 0;
    require(!pureBlack,
            QStringLiteral("codeBorderColor must not be pure black (got %1)").arg(c.name()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QString markdown = readFixture(QString::fromLocal8Bit(argv[1]));
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testThemeCodeFontFallbackOrder);
  RUN_TEST(testThemeCodeHighlightPalette);
  RUN_TEST(testThemeManagerSupportsBuiltInThemes);
  RUN_TEST(testThemeTypographyWeightStyleAndAlignment);
  RUN_TEST(testFromDefinitionReproducesBuiltIns);
  RUN_TEST(testListMarkerGapFloor);
  RUN_TEST(testParagraphSpacingHonoursCssMargin);
  RUN_TEST(testAdjacentCssMarginsCollapseAcrossBlockTypes);
  RUN_TEST(testStructuralMarginSelectorAffectsLayout);
  RUN_TEST(testBlockquoteCssBoxUsesPerSideBorderAndCompactNestedFlow);
  RUN_TEST(testBlockquoteListFlowStaysCompactUnderLazyPromotion);
  RUN_TEST(testGithubBlockquoteNotPaddedByWriteLeak);
  RUN_TEST(testStructuralSelectorsResolveAgainstLiveTree);
  RUN_TEST(testCodeBorderNeverRendersBlack);
  runTest("testLayoutForTheme/github", [&] { testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github")); });
  runTest("testLayoutForTheme/newsprint", [&] { testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint")); });
  runTest("testLayoutForTheme/night", [&] { testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night")); });
  runTest("testLayoutForTheme/pixyll", [&] { testLayoutForTheme(document, RenderTheme::pixyll(), QStringLiteral("pixyll")); });
  runTest("testLayoutForTheme/whitey", [&] { testLayoutForTheme(document, RenderTheme::whitey(), QStringLiteral("whitey")); });
  runTest("testNarrowViewportFillsForCardTheme", [&] { testNarrowViewportFillsForCardTheme(document); });
  runTest("testDecoratedThemePaints", [&] { testDecoratedThemePaints(document); });
#undef RUN_TEST
  return 0;
}
