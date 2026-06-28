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

// Natural (unwrapped) width of the first paragraph under a theme.
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

// Phase 3c: a link ::before icon must reserve real inline flow (Phase 3c-1), not
// just paint into the left margin. A paragraph whose only content is a short
// link stays on one line, so its natural width grows by ~the icon advance when
// the icon theme is applied. Relative (same link text / offscreen font), so the
// assertion is robust to the offscreen font-metric differences that make
// absolute geometry unreliable.
void testLinkBeforeIconReservesFlow() {
  const QString markdown = QStringLiteral("[ab](https://example.com)\n");  // short → one line
  const QString base = QStringLiteral(
      "#write { color:#000000; } a { color:#0000ff; }");
  const QString icon = QStringLiteral(
      "#write { color:#000000; } a { color:#0000ff; }"
      "#write a::before { content:''; background-color:#0000ff;"
      " width:16px; height:16px; margin-right:6px;"
      " -webkit-mask:url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M0 0L10 10'/></svg>\") center/contain; }");
  const qreal baseWidth = paragraphNaturalWidth(base, markdown);
  const qreal iconWidth = paragraphNaturalWidth(icon, markdown);
  // The icon's reserved flow is a DELTA, not an absolute width. The ::before is fixed
  // at width:16px + margin-right:6px = 22 CSS pixels, which advances the line by the
  // same amount on every platform (the "ab" text width cancels: it appears in both
  // baseWidth and iconWidth). The absolute widths themselves vary with the offscreen
  // font (Windows >20px, macOS ~19px), so an absolute floor on baseWidth flakes —
  // assert the cross-platform delta instead. See offscreen-test-harness-broken-font-metrics.
  const qreal reserved = iconWidth - baseWidth;
  require(baseWidth > 1.0,
          QStringLiteral("baseline link paragraph should render (width=%1)").arg(baseWidth));
  require(reserved > 15.0 && reserved < 30.0,
          QStringLiteral("a::before icon must reserve ~22px (16w+6margin) of flow (base=%1 icon=%2 delta=%3)")
              .arg(baseWidth).arg(iconWidth).arg(reserved));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLinkBeforeIconReservesFlow);
#undef RUN_TEST
  return 0;
}
