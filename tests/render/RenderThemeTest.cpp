#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
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
  }
}

// Regression guard for the "narrow window squeezes content into a thin column
// with huge side margins" symptom reported on `#write { max-width; margin: 0
// auto; padding }` themes (e.g. phycat). Such a theme is a CSS page box whose
// horizontalInset is max(margin.left, margin.right) = 0, so the column must FILL
// the viewport at every width (matching Typora) — never float as a thin centred
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
