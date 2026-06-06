#include "app/DocumentSession.h"
#include "commands/StylizeController.h"
#include "document/MarkdownNode.h"
#include "document/InlineProjection.h"
#include "document/SelectionSerializer.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/BlockEditContext.h"
#include "editor/ClipboardController.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"
#include "editor/TextBlockCommandBuilder.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>

#include <cstdlib>
#include <iostream>
#include <variant>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void require(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << "\n";
    std::exit(1);
  }
}

void runTest(const char* name, void (*test)()) {
  std::cerr << "RUN " << name << "\n";
  test();
}

EditTransaction requireTextDeltaCommand(UndoStack& stack, const char* message) {
  require(stack.canUndo(), "expected undo command");
  EditTransaction transaction = stack.takeUndo();
  require(transaction.isTextDeltaCommand(), message);
  require(transaction.textDeltaCommand().isValid(), "text delta command should be valid");
  return transaction;
}

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
  require(requests.first().layoutDirtyBlocks.contains(first), "batched refresh should contain first id");
  require(requests.first().layoutDirtyBlocks.contains(second), "batched refresh should contain second id");

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

MarkdownNode* blockAt(const DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* childAt(MarkdownNode* node, qsizetype index) {
  require(node != nullptr, "parent node should exist");
  const auto& children = node->children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "child index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* listItemAt(const DocumentSession& session, qsizetype listIndex, qsizetype itemIndex) {
  return childAt(blockAt(session, listIndex), itemIndex);
}

MarkdownNode* firstChildOfType(MarkdownNode* node, BlockType type) {
  require(node != nullptr, "parent node should exist");
  for (const auto& child : node->children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "child type not found");
  return nullptr;
}

MarkdownNode* maybeFirstChildOfType(MarkdownNode* node, BlockType type) {
  if (!node) {
    return nullptr;
  }
  for (const auto& child : node->children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  return nullptr;
}

MarkdownNode* firstBlockOfType(const DocumentSession& session, BlockType type) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "block type not found");
  return nullptr;
}

int listDepthForItem(const MarkdownNode* item) {
  int depth = 0;
  const MarkdownNode* node = item;
  while (node) {
    if (node->type() == BlockType::ListItem) {
      ++depth;
    }
    node = node->parent();
  }
  return depth;
}

void setCursor(SelectionController& selection, MarkdownNode* block, qsizetype offset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = offset;
  selection.setCursorPosition(cursor);
}

void setSourceCursor(SelectionController& selection, MarkdownNode* block, qsizetype visibleOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = visibleOffset;
  cursor.text.sourceOffset = sourceOffset;
  selection.setCursorPosition(cursor);
}

bool pressKey(InputController& input, QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  return input.eventFilter(target, &event);
}

void wireInput(
    InputController& input,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue) {
  input.setDocumentSession(&session);
  input.setSelectionController(&selection);
  input.setUndoStack(&undoStack);
  input.setBrushQueue(&brushQueue);
}

void wireStyle(
    StylizeController& stylize,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue) {
  stylize.setDocumentSession(&session);
  stylize.setSelectionController(&selection);
  stylize.setUndoStack(&undoStack);
  stylize.setBrushQueue(&brushQueue);
}

CursorPosition inlineCursor(NodeId blockId, qsizetype textOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = textOffset;
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

void setSelection(SelectionController& selection, MarkdownNode* block, qsizetype anchor, qsizetype focus) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

void setSourceSelection(
    SelectionController& selection,
    MarkdownNode* block,
    MarkdownNode* textNode,
    qsizetype anchorTextOffset,
    qsizetype anchorSourceOffset,
    qsizetype focusTextOffset,
    qsizetype focusSourceOffset) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = textNode->id();
  range.anchor.text.textOffset = anchorTextOffset;
  range.anchor.text.sourceOffset = anchorSourceOffset;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = textNode->id();
  range.focus.text.textOffset = focusTextOffset;
  range.focus.text.sourceOffset = focusSourceOffset;
  selection.setSelection(range);
}

void setCrossSelection(
    SelectionController& selection,
    MarkdownNode* anchorBlock,
    qsizetype anchorOffset,
    MarkdownNode* focusBlock,
    qsizetype focusOffset) {
  SelectionRange range;
  range.anchor.blockId = anchorBlock->id();
  range.anchor.text.nodeId = anchorBlock->id();
  range.anchor.text.textOffset = anchorOffset;
  range.focus.blockId = focusBlock->id();
  range.focus.text.nodeId = focusBlock->id();
  range.focus.text.textOffset = focusOffset;
  selection.setSelection(range);
}

QString selectedMarkdown(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportMarkdown(session.document(), selection.selection());
}

QString selectedPlainText(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportPlainText(session.document(), selection.selection());
}

void wireClipboard(ClipboardController& clipboard, DocumentSession& session, SelectionController& selection, InputController& input) {
  clipboard.setDocumentSession(&session);
  clipboard.setSelectionController(&selection);
  clipboard.setInputController(&input);
}




void testSourceEditorPreservesZeroWidthSpaceText() {
  SourceEditorWidget sourceEditor;
  sourceEditor.setText(QStringLiteral("# Title\n\u200balpha"));
  require(sourceEditor.text() == QStringLiteral("# Title\n\u200balpha"), "source editor should preserve U+200B source text");
  sourceEditor.resize(900, 600);
  sourceEditor.setZoomPercent(125);
  require(sourceEditor.text().contains(QChar(0x200b)), "source editor zoom should not rewrite U+200B text");
}

void testStylizeCollapsedSkeletons() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleBold(), "bold skeleton should insert");
  require(session.markdownText() == QStringLiteral("alpha****"), "bold skeleton text mismatch");
  require(selection.cursorPosition().text.textOffset == 7, "bold skeleton cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 7, "bold skeleton source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleItalic(), "italic skeleton should insert");
  require(session.markdownText() == QStringLiteral("alpha**"), "italic skeleton text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "italic skeleton cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleCode(), "code skeleton should insert");
  require(session.markdownText() == QStringLiteral("alpha``"), "code skeleton text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "code skeleton cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 6, "code skeleton source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.insertLink(), "link skeleton should insert");
  require(session.markdownText() == QStringLiteral("alpha[](url)"), "link skeleton text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "link skeleton cursor mismatch");
}

void testTypingIntoCollapsedStyleSkeletons() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  StylizeController stylize;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleBold(), "bold skeleton should insert before typing");
  require(input.insertText(QStringLiteral("B")), "typing into bold skeleton should work");
  require(session.markdownText() == QStringLiteral("alpha**B**"), "bold skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleCode(), "code skeleton should insert before typing");
  require(input.insertText(QStringLiteral("C")), "typing into code skeleton should work");
  require(session.markdownText() == QStringLiteral("alpha`C`"), "code skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleItalic(), "italic skeleton should insert before typing");
  require(input.insertText(QStringLiteral("I")), "typing into italic skeleton should work");
  require(session.markdownText() == QStringLiteral("alpha*I*"), "italic skeleton typing mismatch");
}

void testStylizeSelectionWrap() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleBold(), "bold should wrap selection");
  require(session.markdownText() == QStringLiteral("a**lph**a"), "bold wrap text mismatch");
  require(!selection.selection().isCollapsed(), "bold wrap should preserve selected text range");
  require(selection.selection().startOffset() == 3, "bold wrap selection start mismatch");
  require(selection.selection().endOffset() == 6, "bold wrap selection end mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleItalic(), "italic should wrap selection");
  require(session.markdownText() == QStringLiteral("a*lph*a"), "italic wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleCode(), "code should wrap selection");
  require(session.markdownText() == QStringLiteral("a`lph`a"), "code wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.insertLink(), "link should wrap selection");
  require(session.markdownText() == QStringLiteral("a[lph](url)a"), "link wrap text mismatch");
}

void testStylizeUndoRedoTextDelta() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleBold(), "bold should create transaction");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "style undo should use TextDeltaCommand");
  require(undo.textDeltaCommand().delta.start == 0, "style undo delta start mismatch");
  require(undo.textDeltaCommand().delta.removedText == QStringLiteral("alpha"), "style undo removed text mismatch");
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("alpha****"), "style undo inserted text mismatch");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("alpha"), "style undo text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "style undo cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  require(redo.isTextDeltaCommand(), "style redo should use TextDeltaCommand");
  session.applyTextDelta(
      redo.textDeltaCommand().delta.start,
      redo.textDeltaCommand().delta.removedText.size(),
      redo.textDeltaCommand().delta.insertedText,
      true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText() == QStringLiteral("alpha****"), "style redo text mismatch");
  require(selection.cursorPosition().text.textOffset == 7, "style redo cursor mismatch");
}

void testHeadingAndListItemStylize() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("# Title!"), false);
  setSelection(selection, blockAt(session, 0), 0, 5);
  require(stylize.toggleBold(), "heading selection style should wrap body text");
  require(session.markdownText() == QStringLiteral("# **Title**!"), "heading style should preserve marker");
  require(selection.selection().startOffset() == 2, "heading style selection start mismatch");
  require(selection.selection().endOffset() == 7, "heading style selection end mismatch");

  session.setMarkdownText(QStringLiteral("- alpha!\n- beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleItalic(), "unordered list style should wrap item body");
  require(session.markdownText() == QStringLiteral("- *alpha*!\n- beta"), "unordered list style should preserve marker");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleCode(), "ordered list style should wrap item body");
  require(session.markdownText() == QStringLiteral("1. `alpha`\n2. beta"), "ordered list style should preserve marker");
}

void testStylizeCrossParagraphSelectionWrap() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 3);
  require(stylize.toggleBold(), "bold should wrap cross paragraph selection by block");
  require(session.markdownText() == QStringLiteral("al**pha**\n\n**beta**\n\n**gam**ma"), "cross paragraph bold wrap mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, listItemAt(session, 1, 1), 2);
  require(stylize.toggleItalic(), "italic should wrap cross block selection by block");
  require(session.markdownText() == QStringLiteral("# Ti*tle*\n\n- *alpha*\n- *be*ta\n\nomega"), "cross block italic wrap mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCrossSelection(selection, blockAt(session, 1), 2, blockAt(session, 0), 2);
  require(stylize.toggleCode(), "code should wrap reverse cross paragraph selection by block");
  require(session.markdownText() == QStringLiteral("al`pha`\n\n`be`ta"), "reverse cross paragraph code wrap mismatch");
}

void testClipboardPlainText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(clipboard.copy(), "copy should use editable selection");
  require(QApplication::clipboard()->text() == QStringLiteral("lph"), "clipboard copy text mismatch");

  require(clipboard.cut(), "cut should use editable selection");
  require(QApplication::clipboard()->text() == QStringLiteral("lph"), "clipboard cut text mismatch");
  require(session.markdownText() == QStringLiteral("aa"), "clipboard cut document mismatch");

  QApplication::clipboard()->setText(QStringLiteral("XYZ"));
  require(clipboard.paste(), "paste should insert clipboard text");
  require(session.markdownText() == QStringLiteral("aXYZa"), "clipboard paste document mismatch");
}

void testClipboardMarkdownPreservesLinePrefix() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("## 技术栈\n\n> quoted text\n\n- list item"), false);
  setSelection(selection, blockAt(session, 0), 1, 3);
  require(clipboard.copy(), "heading markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("## 术栈"), "heading markdown copy should preserve heading marker");

  MarkdownNode* quoteParagraph = childAt(blockAt(session, 1), 0);
  setSelection(selection, quoteParagraph, 0, 6);
  require(clipboard.copy(), "blockquote markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("> quoted"), "blockquote markdown copy should preserve quote marker");

  MarkdownNode* listItem = listItemAt(session, 2, 0);
  setSelection(selection, listItem, 0, 4);
  require(clipboard.copy(), "list markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("- list"), "list markdown copy should preserve list marker");
}

void testClipboardCrossMarkdownPreservesStartPrefix() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("## Block Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor surface"), false);
  MarkdownNode* secondQuoteParagraph = childAt(blockAt(session, 1), 1);
  setCrossSelection(selection, blockAt(session, 0), 1, secondQuoteParagraph, 10);
  require(clipboard.copy(), "cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("## lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor"),
          "cross heading quote markdown copy should preserve heading marker");

  setCrossSelection(selection, secondQuoteParagraph, 10, blockAt(session, 0), 1);
  require(clipboard.copy(), "reverse cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("## lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor"),
          "reverse cross heading quote markdown copy should preserve heading marker");
}

void testSelectionSerializerFormats() {
  DocumentSession session;
  SelectionController selection;
  SelectionSerializer serializer;

  session.setMarkdownText(QStringLiteral("## Headings\n\nalpha"), false);
  setCrossSelection(selection, blockAt(session, 0), 1, blockAt(session, 1), 2);

  SelectionExportResult markdown =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Markdown});
  require(markdown.mimeType == QStringLiteral("text/markdown"), "markdown export mime mismatch");
  require(markdown.text == QStringLiteral("## eadings\n\nal"), "markdown export should preserve only structural prefix");

  SelectionExportResult plainText =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::PlainText});
  require(plainText.mimeType == QStringLiteral("text/plain"), "plain text export mime mismatch");
  require(plainText.text == QStringLiteral("eadings\nal"), "plain text export mismatch");

  SelectionExportResult html =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Html});
  require(html.mimeType == QStringLiteral("text/html"), "html export mime mismatch");
  require(html.text.contains(QStringLiteral("<h2>eadings</h2>")), "html export should render markdown to HTML");
}

void testSelectionSerializerCrossComplexInlineEdges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("Plain **bold** and `code`\n\nMiddle\n\nTail $x+y$ and [link](https://example.com)"), false);
  setCrossSelection(selection, blockAt(session, 0), 6, blockAt(session, 2), 8);

  const QString markdown = selectedMarkdown(session, selection);
  require(markdown == QStringLiteral("**bold** and `code`\n\nMiddle\n\nTail $x+y$"),
          "complex inline cross selection should preserve only selected edge text");

  const QString plainText = selectedPlainText(session, selection);
  require(plainText == QStringLiteral("bold and code\nMiddle\nTail x+y"), "complex inline cross selection plain text mismatch");
}

void testSelectAllContextSemantics() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  view.setDocument(session.document());
  controller.selection().setCursorPosition(inlineCursor(blockAt(session, 0)->id(), 2, 2));
  require(controller.selectAll(), "paragraph select all should work");
  SelectionRange range = controller.selection().selection();
  require(range.anchor.blockId == blockAt(session, 0)->id(), "paragraph select all anchor should be first block");
  require(range.focus.blockId == blockAt(session, 1)->id(), "paragraph select all focus should be last block");
  require(range.focus.text.textOffset == 4, "paragraph select all should end at document text end");

  session.setMarkdownText(QStringLiteral("alpha\n\n```cpp\nreturn 0;\n```\n\nbeta"), false);
  view.setDocument(session.document());
  MarkdownNode* code = blockAt(session, 1);
  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 2;
  controller.activateHit(codeHit);
  require(controller.selectAll(), "code block select all should work");
  range = controller.selection().selection();
  require(range.anchor.blockId == code->id() && range.focus.blockId == code->id(), "code select all should stay in code block");
  require(range.anchor.text.textOffset == 0 && range.focus.text.textOffset == code->literal().size(), "code select all should select literal");

  session.setMarkdownText(QStringLiteral("alpha\n\n$$\nx+y\n$$\n\nbeta"), false);
  view.setDocument(session.document());
  MarkdownNode* math = blockAt(session, 1);
  HitTestResult mathHit;
  mathHit.zone = HitTestResult::Zone::Math;
  mathHit.blockId = math->id();
  mathHit.textNodeId = math->id();
  mathHit.textOffset = 1;
  controller.activateHit(mathHit);
  require(controller.selectAll(), "math block select all should work");
  range = controller.selection().selection();
  require(range.anchor.blockId == math->id() && range.focus.blockId == math->id(), "math select all should stay in math block");
  require(range.anchor.text.textOffset == 0 && range.focus.text.textOffset == math->literal().size(), "math select all should select TeX literal");

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| one | two |"), false);
  view.setDocument(session.document());
  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* cell = childAt(childAt(table, 1), 1);
  CursorPosition cellCursor;
  cellCursor.blockId = table->id();
  cellCursor.text.nodeId = cell->id();
  cellCursor.text.textOffset = 1;
  controller.selection().setCursorPosition(cellCursor);
  require(controller.selectAll(), "table cell select all should work");
  range = controller.selection().selection();
  require(range.anchor.blockId == table->id() && range.focus.blockId == table->id(), "table cell select all should keep table block id");
  require(range.anchor.text.nodeId == cell->id() && range.focus.text.nodeId == cell->id(), "table cell select all should target current cell");
  require(range.anchor.text.textOffset == 0 && range.focus.text.textOffset == 3, "table cell select all should select cell text");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  view.setDocument(session.document());
  controller.selection().setCursorPosition(inlineCursor(blockAt(session, 0)->id(), 1, 1));
  QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier);
  QApplication::sendEvent(&view, &shortcut);
  require(shortcut.isAccepted(), "view should reserve ctrl+a shortcut");
  QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
  QApplication::sendEvent(&view, &key);
  range = controller.selection().selection();
  require(range.anchor.blockId == blockAt(session, 0)->id() && range.focus.blockId == blockAt(session, 1)->id(),
          "ctrl+a keypress should select whole document from paragraph");
}

void testClipboardBlockSelectionFallback() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n\n```cpp\nreturn 0;\n```"), false);
  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* code = blockAt(session, 1);
  setCrossSelection(selection, table, 0, code, 5);
  require(input.hasEditableSelection(), "block fallback selection should be copyable");
  require(clipboard.copy(), "copy should support block fallback selection");
  require(QApplication::clipboard()->text().contains(QStringLiteral("A")), "block fallback copy should include table text");
  require(selectedMarkdown(session, selection).contains(QStringLiteral("```cpp")), "block fallback markdown should include code fence");
}

void testCodeFenceSelectionCopyUsesLiteralOffsets() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  const QString code = QStringLiteral(
      "#include <iostream>\n\n"
      "int main() {\n"
      "  const auto message = \"Hello from Muffin\";\n"
      "  std::cout << message << '\\n';\n"
      "  return 0;\n"
      "}\n");
  session.setMarkdownText(QStringLiteral("```cpp\n%1```").arg(code), false);
  MarkdownNode* fence = blockAt(session, 0);
  SelectionRange range;
  range.anchor.blockId = fence->id();
  range.anchor.text.nodeId = fence->id();
  range.anchor.text.textOffset = 0;
  range.focus.blockId = fence->id();
  range.focus.text.nodeId = fence->id();
  range.focus.text.textOffset = code.size() - 1;
  selection.setSelection(range);

  require(clipboard.copy(), "code fence copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("```cpp\n%1").arg(code.left(code.size() - 1)),
          "code fence markdown copy should use literal offsets after fence header");
  require(selectedPlainText(session, selection) == code.left(code.size() - 1),
          "code fence plain text copy should use literal offsets");
}

void testCodeFenceSelectionCutDeletesLiteralOffsets() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("```cpp\nalpha beta gamma\n```"), false);
  view.setDocument(session.document());
  MarkdownNode* fence = blockAt(session, 0);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = fence->id();
  hit.textNodeId = fence->id();
  hit.textOffset = 0;
  controller.activateHit(hit);
  require(controller.enterCodeFenceEditMode(), "code fence edit mode should activate before cut");

  SelectionRange range;
  range.anchor.blockId = fence->id();
  range.anchor.text.nodeId = fence->id();
  range.anchor.text.textOffset = QStringLiteral("alpha ").size();
  range.focus.blockId = fence->id();
  range.focus.text.nodeId = fence->id();
  range.focus.text.textOffset = QStringLiteral("alpha beta").size();
  controller.selection().setSelection(range);

  require(controller.cut(), "code fence cut should delete active literal selection");
  require(QApplication::clipboard()->text().contains(QStringLiteral("beta")), "code fence cut clipboard should contain selected literal text");
  require(session.markdownText().contains(QStringLiteral("alpha  gamma")), "code fence cut should remove selected literal text");
  require(!session.markdownText().contains(QStringLiteral("beta")), "code fence cut should not leave selected literal text behind");
}

void testKeyboardNavigationBasics() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue);
  input.attach(&view);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);
  QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  require(input.eventFilter(&view, &right), "right at block end should move to next block");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "right should move to next block");
  require(selection.cursorPosition().text.textOffset == 0, "right next block offset mismatch");

  QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
  require(input.eventFilter(&view, &shiftRight), "shift-right should extend selection");
  require(!selection.selection().isCollapsed(), "shift-right should create selection");
}


}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSelectionController);
  RUN_TEST(testSelectionControllerRange);
  RUN_TEST(testUndoStack);
  RUN_TEST(testBrushQueueBatchesRefreshRequests);
  RUN_TEST(testSourceEditorPreservesZeroWidthSpaceText);
  RUN_TEST(testStylizeCollapsedSkeletons);
  RUN_TEST(testTypingIntoCollapsedStyleSkeletons);
  RUN_TEST(testStylizeSelectionWrap);
  RUN_TEST(testStylizeUndoRedoTextDelta);
  RUN_TEST(testHeadingAndListItemStylize);
  RUN_TEST(testStylizeCrossParagraphSelectionWrap);
  RUN_TEST(testClipboardPlainText);
  RUN_TEST(testClipboardMarkdownPreservesLinePrefix);
  RUN_TEST(testClipboardCrossMarkdownPreservesStartPrefix);
  RUN_TEST(testSelectionSerializerFormats);
  RUN_TEST(testSelectionSerializerCrossComplexInlineEdges);
  RUN_TEST(testSelectAllContextSemantics);
  RUN_TEST(testClipboardBlockSelectionFallback);
  RUN_TEST(testCodeFenceSelectionCopyUsesLiteralOffsets);
  RUN_TEST(testCodeFenceSelectionCutDeletesLiteralOffsets);
  RUN_TEST(testKeyboardNavigationBasics);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
