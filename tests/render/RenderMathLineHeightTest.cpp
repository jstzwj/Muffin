#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QString>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Height of the first paragraph block under the default theme.
qreal paragraphBlockHeight(const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), RenderTheme::defaultTheme(), 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  return block->rect().height();
}

// A tall inline atom (a fraction is ~2x text height on a real font) must grow its
// line, so the paragraph block is taller than the same prose without the math.
// Before the fix the line was not grown for math atoms, so the painted fraction
// overflowed into the neighbour line. The block-height comparison is relative (same
// offscreen font for plain and withMath), but the growth MAGNITUDE depends on how
// tall that font renders the fraction — so the assertion is proportional, not an
// absolute pixel margin (see offscreen-test-harness-broken-font-metrics).
void testTallInlineMathGrowsLine() {
  const qreal plain = paragraphBlockHeight(QStringLiteral("alpha bravo charlie\n"));
  const qreal withMath = paragraphBlockHeight(QStringLiteral("alpha $\\frac{1}{2}$ bravo\n"));
  require(plain > 8.0, QStringLiteral("plain paragraph should have measurable height (=%1)").arg(plain));
  // The line must grow when it holds a tall inline atom. The growth MAGNITUDE is
  // font-dependent: a real font renders the fraction ~2x text height (exercised on
  // Windows CI), but the offscreen/fontconfig font on Linux CI renders it near-text
  // (plain≈23, withMath≈27 — only ~17% taller). An absolute pixel margin (+6) failed
  // on the smaller Linux metrics, so assert PROPORTIONAL growth (>5%, beyond rounding),
  // which scales with the font. The regression this guards is ZERO growth — the line
  // not being grown for math atoms, so the painted fraction overflowed its neighbour.
  require(withMath > plain * 1.05,
          QStringLiteral("a tall inline fraction must grow its line (plain=%1 withMath=%2)").arg(plain).arg(withMath));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTallInlineMathGrowsLine);
#undef RUN_TEST
  return 0;
}
