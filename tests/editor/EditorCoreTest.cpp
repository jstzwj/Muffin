#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>
#include <variant>

using namespace muffin;

// testSelectionController (lines 61-76)
void testSelectionController() {
  SelectionController controller;
  HitTestResult hit;
  hit.blockId = NodeId::fromString(QStringLiteral("block"));
  hit.textNodeId = hit.blockId;
  hit.textOffset = 4;
  hit.zone = HitTestResult::Zone::Text;

  controller.setHitResult(hit);
  require(controller.hasCursor(), "selection should have cursor");
  require(controller.cursorPosition().blockId == hit.blockId, "cursor block mismatch");
  require(controller.cursorPosition().text.textOffset == 4, "cursor offset mismatch");

  controller.clear();
  require(!controller.hasCursor(), "selection should clear cursor");
}

// testSelectionControllerRange (lines 78-94)
void testSelectionControllerRange() {
  SelectionController controller;
  SelectionRange range;
  range.anchor.blockId = NodeId::fromString(QStringLiteral("block"));
  range.anchor.text.nodeId = range.anchor.blockId;
  range.anchor.text.textOffset = 5;
  range.focus.blockId = range.anchor.blockId;
  range.focus.text.nodeId = range.anchor.blockId;
  range.focus.text.textOffset = 1;

  controller.setSelection(range);
  require(controller.hasCursor(), "range selection should set cursor state");
  require(!controller.selection().isCollapsed(), "range selection should not be collapsed");
  require(controller.selection().isSingleBlock(), "range selection should be single block");
  require(controller.selection().startOffset() == 1, "range start offset mismatch");
  require(controller.selection().endOffset() == 5, "range end offset mismatch");
}

// testUndoStack (lines 96-112)
void testUndoStack() {
  UndoStack stack;
  DocumentSnapshot before{QStringLiteral("alpha"), {}};
  DocumentSnapshot after{QStringLiteral("alphabet"), {}};

  stack.push(EditTransaction(EditTransaction::Kind::InsertText, QStringLiteral("Insert Text"), before, after));
  require(stack.canUndo(), "undo should be available");
  require(!stack.canRedo(), "redo should not be available after push");

  const EditTransaction undo = stack.takeUndo();
  require(undo.before().markdownText == QStringLiteral("alpha"), "undo snapshot mismatch");
  require(stack.canRedo(), "redo should be available after undo");

  const EditTransaction redo = stack.takeRedo();
  require(redo.after().markdownText == QStringLiteral("alphabet"), "redo snapshot mismatch");
  require(stack.canUndo(), "undo should be available after redo");
}

// testNodeAttributeValueContract (lines 114-129)
void testNodeAttributeValueContract() {
  require(nodeAttributeAcceptsValue(NodeAttribute::HeadingLevel, NodeAttributeValue{1}), "heading level should accept int");
  require(nodeAttributeAcceptsValue(NodeAttribute::ListStart, NodeAttributeValue{1}), "list start should accept int");
  require(nodeAttributeAcceptsValue(NodeAttribute::ListKind, NodeAttributeValue{ListKind::Ordered}), "list kind should accept ListKind");
  require(nodeAttributeAcceptsValue(NodeAttribute::ListTight, NodeAttributeValue{true}), "list tight should accept bool");
  require(nodeAttributeAcceptsValue(NodeAttribute::TaskChecked, NodeAttributeValue{false}), "task checked should accept bool");
  require(nodeAttributeAcceptsValue(NodeAttribute::CodeLanguage, NodeAttributeValue{QStringLiteral("cpp")}), "code language should accept QString");
  require(
      nodeAttributeAcceptsValue(NodeAttribute::TableAlignments, NodeAttributeValue{QVector<TableAlignment>{TableAlignment::Left}}),
      "table alignments should accept alignment vector");
  require(nodeAttributeAcceptsValue(NodeAttribute::TableRowIsHeader, NodeAttributeValue{true}), "table row header should accept bool");

  require(!nodeAttributeAcceptsValue(NodeAttribute::HeadingLevel, NodeAttributeValue{true}), "heading level should reject bool");
  require(!nodeAttributeAcceptsValue(NodeAttribute::CodeLanguage, NodeAttributeValue{1}), "code language should reject int");
  require(!nodeAttributeAcceptsValue(NodeAttribute::Unknown, NodeAttributeValue{1}), "unknown attribute should reject values");
}

// testBrushQueueBatchesRefreshRequests (lines 131-182)
void testBrushQueueBatchesRefreshRequests() {
  BrushQueue queue;
  QVector<BrushQueue::RefreshRequest> requests;
  QObject::connect(&queue, &BrushQueue::refreshRequested, [&requests](BrushQueue::RefreshRequest request) {
    requests.push_back(std::move(request));
  });

  const NodeId first = NodeId::fromString(QStringLiteral("first"));
  const NodeId second = NodeId::fromString(QStringLiteral("second"));
  queue.requestBlockRefresh(first);
  queue.requestBlocksRefresh({first, second, second});
  queue.flush();

  require(requests.size() == 1, "brush queue should batch block refresh requests");
  require(!requests.first().fullLayoutDirty, "batched block refresh should not be full dirty");
  require(requests.first().layoutDirtyBlocks.size() == 2, "batched block refresh should deduplicate ids");
  require(requests.first().layoutDirtyBlocks.at(0) == first, "batched refresh should preserve first requested id order");
  require(requests.first().layoutDirtyBlocks.at(1) == second, "batched refresh should append each block id once");

  TopLevelRangeChange range{1, 1, 2, 7};
  queue.requestTopLevelRangeRefresh(range);
  queue.requestBlockRefresh(first);
  queue.flush();

  require(requests.size() == 2, "brush queue should emit top-level range refresh batch");
  require(!requests.last().fullLayoutDirty, "range refresh should not be full dirty");
  require(requests.last().topLevelRangeDirty == range, "range refresh should preserve top-level range");
  require(requests.last().layoutDirtyBlocks.isEmpty(), "range refresh should clear block dirty ids");

  queue.requestTopLevelRangeRefresh(range);
  queue.requestTopLevelRangeRefresh(TopLevelRangeChange{2, 1, 2, 7});
  queue.flush();

  require(requests.size() == 3, "incompatible range refreshes should emit one fallback batch");
  require(requests.last().fullLayoutDirty, "incompatible range refreshes should fall back to full refresh");
  require(!requests.last().topLevelRangeDirty.isValid(), "full refresh should clear range dirty state");

  queue.requestTopLevelRangeRefresh({});
  queue.flush();

  require(requests.size() == 4, "invalid range refresh should emit fallback batch");
  require(requests.last().fullLayoutDirty, "invalid range refresh should become full refresh");

  queue.requestBlockRefresh(first);
  queue.requestFullRefresh();
  queue.requestBlockRefresh(second);
  queue.flush();

  require(requests.size() == 5, "brush queue should emit full refresh batch");
  require(requests.last().fullLayoutDirty, "full refresh should dominate batched block refreshes");
  require(requests.last().layoutDirtyBlocks.isEmpty(), "full refresh should clear block dirty ids");
}

// testSourceEditorPreservesZeroWidthSpaceText (lines 360-367)
void testSourceEditorPreservesZeroWidthSpaceText() {
  SourceEditorWidget sourceEditor;
  sourceEditor.setText(QStringLiteral("# Title\n​alpha"));
  require(sourceEditor.text() == QStringLiteral("# Title\n​alpha"), "source editor should preserve U+200B source text");
  sourceEditor.resize(900, 600);
  sourceEditor.setZoomPercent(125);
  require(sourceEditor.text().contains(QChar(0x200b)), "source editor zoom should not rewrite U+200B text");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSelectionController);
  RUN_TEST(testSelectionControllerRange);
  RUN_TEST(testUndoStack);
  RUN_TEST(testNodeAttributeValueContract);
  RUN_TEST(testBrushQueueBatchesRefreshRequests);
  RUN_TEST(testSourceEditorPreservesZeroWidthSpaceText);
#undef RUN_TEST
  return 0;
}
