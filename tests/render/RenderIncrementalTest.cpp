#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

void requireSameTopLevelLayout(const DocumentLayout& incremental, const DocumentLayout& full, const MarkdownDocument& document, const QString& label) {
  require(incremental.blocks().size() == full.blocks().size(), label + QStringLiteral(" block count mismatch"));
  require(qAbs(incremental.totalHeight() - full.totalHeight()) < 0.01, label + QStringLiteral(" total height mismatch"));
  for (const auto& child : document.root().children()) {
    const BlockLayout* incrementalBlock = incremental.block(child->id());
    const BlockLayout* fullBlock = full.block(child->id());
    require(incrementalBlock != nullptr, label + QStringLiteral(" incremental block index missing"));
    require(fullBlock != nullptr, label + QStringLiteral(" full block index missing"));
    const QRectF incrementalRect = incrementalBlock->rect();
    const QRectF fullRect = fullBlock->rect();
    require(qAbs(incrementalRect.left() - fullRect.left()) < 0.01, label + QStringLiteral(" block left mismatch"));
    require(qAbs(incrementalRect.top() - fullRect.top()) < 0.01, label + QStringLiteral(" block top mismatch"));
    require(qAbs(incrementalRect.width() - fullRect.width()) < 0.01, label + QStringLiteral(" block width mismatch"));
    require(qAbs(incrementalRect.height() - fullRect.height()) < 0.01, label + QStringLiteral(" block height mismatch"));
  }
}

void testIncrementalBlockRebuildContract() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"), false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const NodeId firstParagraphId = mutableBlockAt(session.document(), 0)->id();
  const NodeId secondParagraphId = mutableBlockAt(session.document(), 1)->id();
  const QRectF firstBefore = layout.block(firstParagraphId)->rect();
  const QRectF secondBefore = layout.block(secondParagraphId)->rect();
  const qreal totalBefore = layout.totalHeight();

  require(session.applyTextDelta(
              5,
              0,
              QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"
                             " alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"),
              true,
              {LocalEditNodeHint{firstParagraphId, 0, BlockType::Paragraph}}),
          QStringLiteral("paragraph local delta should apply"));
  const DocumentLayout::BlockRebuildResult paragraphResult = layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(paragraphResult.rebuilt, QStringLiteral("paragraph block rebuild should succeed"));
  require(paragraphResult.blockId == firstParagraphId, QStringLiteral("paragraph rebuild result id mismatch"));
  require(paragraphResult.oldRect == firstBefore, QStringLiteral("paragraph rebuild old rect mismatch"));
  require(paragraphResult.newRect == layout.block(firstParagraphId)->rect(), QStringLiteral("paragraph rebuild new rect mismatch"));
  require(paragraphResult.heightDelta != 0, QStringLiteral("paragraph rebuild should report height delta"));
  require(!paragraphResult.shiftedRect.isEmpty(), QStringLiteral("paragraph rebuild should report shifted rect"));
  require(layout.block(secondParagraphId)->rect().top() != secondBefore.top(), QStringLiteral("paragraph rebuild should shift following block"));
  require(layout.totalHeight() != totalBefore, QStringLiteral("paragraph rebuild should update total height"));

  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(tableCell != nullptr, QStringLiteral("incremental table cell missing"));
  MarkdownNode* table = mutableBlockAt(session.document(), 2);
  require(layout.block(tableCell->id()) != nullptr, QStringLiteral("layout index should include table cell"));
  const QRectF tableBefore = layout.block(table->id())->rect();
  const DocumentLayout::BlockRebuildResult tableResult = layout.rebuildBlock(tableCell->id(), session.document(), theme, {});
  require(tableResult.rebuilt, QStringLiteral("table cell rebuild should map to top-level table"));
  require(tableResult.blockId == table->id(), QStringLiteral("table cell rebuild should report table block id"));
  require(tableResult.oldRect == tableBefore, QStringLiteral("table cell rebuild old rect mismatch"));
  require(tableResult.newRect == layout.block(table->id())->rect(), QStringLiteral("table cell rebuild new rect mismatch"));
}

void testIncrementalTopLevelRangeRebuildContract() {
  RenderTheme theme = RenderTheme::github();
  auto verifyRangeEdit = [&](const QString& initial, qsizetype sourceStart, qsizetype removedLength, const QString& insertedText, const QString& label) {
    DocumentSession session;
    session.setMarkdownText(initial, false);
    DocumentLayout incremental;
    incremental.rebuild(session.document(), theme, 800.0);

    require(session.applyTextDelta(sourceStart, removedLength, insertedText, true), label + QStringLiteral(" local edit should apply"));
    const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
    require(range.isValid(), label + QStringLiteral(" range should be valid"));
    const DocumentLayout::RangeRebuildResult result = incremental.rebuildTopLevelRange(range, session.document(), theme, {});
    require(result.rebuilt, label + QStringLiteral(" range rebuild should succeed"));

    DocumentLayout full;
    full.rebuild(session.document(), theme, 800.0);
    requireSameTopLevelLayout(incremental, full, session.document(), label);
  };

  verifyRangeEdit(QStringLiteral("alpha beta\n\ngamma"), 5, 0, QStringLiteral("\n\n"), QStringLiteral("paragraph split"));
  verifyRangeEdit(QStringLiteral("alpha\n\nbeta\n\ngamma"), 5, 2, QStringLiteral(" "), QStringLiteral("paragraph merge"));
  verifyRangeEdit(QStringLiteral("# Heading\n\nalpha\n\nbeta"), 9, 0, QStringLiteral("\n\n"), QStringLiteral("heading boundary insert"));
  verifyRangeEdit(QStringLiteral("alpha\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nomega"), 5, 0, QStringLiteral("\n\ninserted"), QStringLiteral("table suffix shift"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testIncrementalBlockRebuildContract);
  RUN_TEST(testIncrementalTopLevelRangeRebuildContract);
#undef RUN_TEST
  return 0;
}
