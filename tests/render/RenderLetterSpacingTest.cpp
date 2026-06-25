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

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLetterSpacingWidensText);
#undef RUN_TEST
  return 0;
}

