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

// A tall inline atom (a fraction is ~2x text height) must grow its line, so the
// paragraph block is taller than the same prose without the math. Before the
// fix the line was not grown for math atoms, so the painted fraction overflowed
// into the neighbour line. Relative (same offscreen font), so robust to the
// offscreen metric differences that make absolute geometry unreliable.
void testTallInlineMathGrowsLine() {
  const qreal plain = paragraphBlockHeight(QStringLiteral("alpha bravo charlie\n"));
  const qreal withMath = paragraphBlockHeight(QStringLiteral("alpha $\\frac{1}{2}$ bravo\n"));
  require(plain > 8.0, QStringLiteral("plain paragraph should have measurable height (=%1)").arg(plain));
  // A fraction renders ~2x the text height; the line must grow by a clear margin.
  require(withMath > plain + 6.0,
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
