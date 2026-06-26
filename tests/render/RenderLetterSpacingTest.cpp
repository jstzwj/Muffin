#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QString>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Natural (unwrapped) text width of the first paragraph under a theme.
qreal paragraphNaturalWidth(const QString& css, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("t"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  const InlineLayout* inlineLayout = block->inlineLayout();
  require(inlineLayout != nullptr, QStringLiteral("paragraph should have an inline layout"));
  return inlineLayout->size().width();
}

// letter-spacing is baked into the theme fonts and re-applied on the per-range
// QTextCharFormat, so the text engine shapes wider text. The assertion uses a
// SHORT line (no wrapping) so size().width() is the true natural width, and
// compares with vs without letter-spacing under the SAME offscreen font —
// robust to the offscreen font-metric differences that make absolute geometry
// unreliable.
void testLetterSpacingWidensText() {
  const QString markdown = QStringLiteral("the quick brown fox\n");  // short → stays one line either way
  const QString base = QStringLiteral("#write { color:#000000; }");
  const QString spaced = QStringLiteral("#write { color:#000000; letter-spacing:6px; }");
  const qreal baseWidth = paragraphNaturalWidth(base, markdown);
  const qreal spacedWidth = paragraphNaturalWidth(spaced, markdown);
  require(baseWidth > 50.0, QStringLiteral("baseline paragraph should have measurable width (=%1)").arg(baseWidth));
  // 19 glyphs × 6px ≈ 114px of extra width.
  require(spacedWidth > baseWidth + 60.0,
          QStringLiteral("letter-spacing must widen the (unwrapped) text (base=%1 spaced=%2)").arg(baseWidth).arg(spacedWidth));
}

void testWordSpacingWidensText() {
  const QString markdown = QStringLiteral("one two three four\n");  // three inter-word spaces, no wrapping
  const QString base = QStringLiteral("#write { color:#000000; }");
  const QString spaced = QStringLiteral("#write { color:#000000; } #write p { word-spacing:10px; }");
  const qreal baseWidth = paragraphNaturalWidth(base, markdown);
  const qreal spacedWidth = paragraphNaturalWidth(spaced, markdown);
  require(baseWidth > 40.0, QStringLiteral("baseline paragraph should have measurable width (=%1)").arg(baseWidth));
  require(spacedWidth > baseWidth + 20.0,
          QStringLiteral("word-spacing must widen the inter-word gaps (base=%1 spaced=%2)").arg(baseWidth).arg(spacedWidth));
}

// CSS text-transform is applied to the projected display text only (length-
// preserving per-code-point case mapping, so source↔display offsets stay exact).
// Asserts on displayText() — deterministic, font-metric independent.
void testTextTransformAppliesToDisplayText() {
  const QString markdown = QStringLiteral("the quick brown fox\n");
  const QString css = QStringLiteral("#write { color:#000000; } #write p { text-transform: uppercase; }");
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("t"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  const InlineLayout* inlineLayout = block->inlineLayout();
  require(inlineLayout != nullptr, QStringLiteral("paragraph should have an inline layout"));
  require(inlineLayout->displayText().contains(QStringLiteral("THE QUICK BROWN FOX")),
          QStringLiteral("text-transform: uppercase should upper-case the display text (got '%1')").arg(inlineLayout->displayText()));
  // Length-preserving: the transformed display text is exactly as long as the
  // un-transformed one (no ß→SS special-casing), so source↔display offsets stay valid.
  const QString lower = QStringLiteral("#write { color:#000000; } #write p { text-transform: lowercase; }");
  const RenderTheme lowerTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(lower, QStringLiteral("tl"), QString()));
  DocumentLayout lowerLayout;
  lowerLayout.rebuild(session.document(), lowerTheme, 800.0);
  const InlineLayout* lowerInline = lowerLayout.block(p->id())->inlineLayout();
  require(lowerInline != nullptr, QStringLiteral("lower paragraph inline layout"));
  require(lowerInline->displayText().contains(QStringLiteral("the quick brown fox")),
          QStringLiteral("text-transform: lowercase should lower-case the display text (got '%1')").arg(lowerInline->displayText()));
  require(inlineLayout->displayText().size() == lowerInline->displayText().size(),
          QStringLiteral("transform is length-preserving (upper=%1 lower=%2)").arg(inlineLayout->displayText().size()).arg(lowerInline->displayText().size()));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLetterSpacingWidensText);
  RUN_TEST(testWordSpacingWidensText);
  RUN_TEST(testTextTransformAppliesToDisplayText);
#undef RUN_TEST
  return 0;
}

