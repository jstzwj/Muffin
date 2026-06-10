#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

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
          QStringLiteral("light code keyword color should match Typora-like purple"));
  require(theme.codeHighlightColor(CodeHighlightRole::Function).name() == QStringLiteral("#0000a8"),
          QStringLiteral("light code function color should match Typora-like blue"));
  require(theme.codeHighlightColor(CodeHighlightRole::String).name() == QStringLiteral("#a31515"),
          QStringLiteral("light code string color should match Typora-like red"));
  require(theme.codeHighlightColor(CodeHighlightRole::Preprocessor).name() == theme.textColor().name(),
          QStringLiteral("light code preprocessor color should stay close to plain text"));
  require(theme.codeHighlightColor(CodeHighlightRole::Type).name() == QStringLiteral("#008000"),
          QStringLiteral("light code type color should match Typora-like green"));
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

  require(manager.availableThemes() == expectedThemes, QStringLiteral("Theme manager should expose the five built-in themes"));
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
  runTest("testLayoutForTheme/github", [&] { testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github")); });
  runTest("testLayoutForTheme/newsprint", [&] { testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint")); });
  runTest("testLayoutForTheme/night", [&] { testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night")); });
  runTest("testLayoutForTheme/pixyll", [&] { testLayoutForTheme(document, RenderTheme::pixyll(), QStringLiteral("pixyll")); });
  runTest("testLayoutForTheme/whitey", [&] { testLayoutForTheme(document, RenderTheme::whitey(), QStringLiteral("whitey")); });
#undef RUN_TEST
  return 0;
}
