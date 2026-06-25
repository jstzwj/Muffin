#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "math/MathRenderer.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "render/InlineLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QString>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// A theme whose links carry a `::before` mask icon (the phycat-style generated
// icon), so InlineLayout::buildLinkBeforeAtoms inserts flow-reserved
// placeholders at every link run. This is the condition that exposed the bug.
RenderTheme linkBeforeIconTheme() {
  const QString css = QStringLiteral(
      "#write { color:#000000; } a { color:#0000ff; }"
      "#write a::before { content:''; background-color:#0000ff;"
      " width:16px; height:16px; margin-right:6px;"
      " -webkit-mask:url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M0 0L10 10'/></svg>\") center/contain; }");
  return RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("t"), QString()));
}

qreal selectionWidthForSourceRange(const InlineLayout& layout, const InlineRange& range) {
  const QVector<QRectF> rects = layout.selectionRectsForSourceOffsets(range.start, range.end);
  qreal total = 0.0;
  for (const QRectF& r : rects) {
    total += r.width();
  }
  return total;
}

// Regression for "inline math covers the 'h' of an adjacent word": a link
// `::before` icon inserts flow-reserved placeholders that run AFTER the
// math/image atoms are built. Those atoms kept stale (pre-insertion) display
// offsets, so the real math placeholder was never force-widthed at its shifted
// position (the force-width format landed on a stale, too-far-left index) and
// the math painted over the preceding text. With the fix the atom offsets are
// shifted to match, the placeholder is widened at the correct index, and a
// selection over the inline-math source range spans the native formula width
// instead of a single narrow placeholder char. Relative (compared against the
// same theme's native render), so robust to offscreen font-metric differences.
void testLinkBeforeIconDoesNotStaleMathAtomOffset() {
  const QString markdown = QStringLiteral("[ab](https://example.com) math $E=mc^2$\n");
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const RenderTheme theme = linkBeforeIconTheme();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(paragraph != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(paragraph->id());
  require(block != nullptr && block->inlineLayout() != nullptr, QStringLiteral("paragraph should have an inline layout"));

  const InlineNode* mathNode = nullptr;
  for (const InlineNode& node : paragraph->inlines()) {
    if (node.type() == InlineType::InlineMath) {
      mathNode = &node;
      break;
    }
  }
  require(mathNode != nullptr, QStringLiteral("fixture should contain inline math"));

  const InlineLayout& inlineLayout = *block->inlineLayout();
  const qreal reserved = selectionWidthForSourceRange(inlineLayout, mathNode->sourceRange());

  const math::MathLayoutResult formula = math::MathRenderer().render(QStringLiteral("E=mc^2"), theme, false);
  require(formula.valid(), QStringLiteral("native formula should render"));
  const qreal nativeWidth = formula.size.width();

  // Before the fix the placeholder at the math's real (shifted) position was
  // narrow, so the selection over the math spanned ~one character; afterwards
  // it spans the formula width. A 0.5x threshold separates the two cleanly
  // even under offscreen metrics.
  require(reserved > nativeWidth * 0.5,
          QStringLiteral("inline math must reserve the native formula width when a link ::before icon precedes it "
                         "(reserved=%1 native=%2)").arg(reserved).arg(nativeWidth));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLinkBeforeIconDoesNotStaleMathAtomOffset);
#undef RUN_TEST
  return 0;
}
