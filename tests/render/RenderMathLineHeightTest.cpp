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
  if (markdown.contains(QLatin1Char('$'))) {
    require(block->inlineLayout()->mathAtomCount() == 1, "height regression must exercise a rendered math atom");
    const auto rects = block->inlineLayout()->mathAtomRects(block->inlineTextOrigin());
    require(rects.size() == 1 && rects.front().top() >= block->rect().top() - .01 &&
                rects.front().bottom() <= block->rect().bottom() + .01,
            QString("painted math content must fit within the same measured paragraph box: math=(%1,%2,%3,%4) block=(%5,%6,%7,%8)")
                .arg(rects.front().x()).arg(rects.front().y()).arg(rects.front().width()).arg(rects.front().height())
                .arg(block->rect().x()).arg(block->rect().y()).arg(block->rect().width()).arg(block->rect().height()));
  }
  return block->rect().height();
}

// Actual math content can exceed KaTeX's fixed outer inline strut. Its measured
// range, paint origin and paragraph flow must agree when that happens.
void testTallInlineMathGrowsLine() {
  const qreal plain = paragraphBlockHeight(QStringLiteral("alpha bravo charlie\n"));
  // Fractions can fit inside an authored CSS line-height. A 4em rule gives this
  // regression an explicit tall math box, independent of the installed fonts.
  const qreal withMath = paragraphBlockHeight(QStringLiteral("alpha $\\rule{1em}{4em}$ bravo\n"));
  require(plain > 8.0, QStringLiteral("plain paragraph should have measurable height (=%1)").arg(plain));
  // Compare proportional growth under the same font backend rather than a
  // platform-specific absolute pixel height.
  require(withMath > plain * 1.05,
          QStringLiteral("a tall inline math box must grow its line (plain=%1 withMath=%2)").arg(plain).arg(withMath));
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
