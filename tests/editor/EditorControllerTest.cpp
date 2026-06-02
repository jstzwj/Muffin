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
#include "editor/TextBlockCommandBuilder.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>

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

  queue.requestBlockRefresh(first);
  queue.requestFullRefresh();
  queue.requestBlockRefresh(second);
  queue.flush();

  require(requests.size() == 2, "brush queue should emit full refresh batch");
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

MarkdownNode* firstBlockOfType(const DocumentSession& session, BlockType type) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "block type not found");
  return nullptr;
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

void testInputInsertAndBackspace() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertText(QStringLiteral("!")), "insert text should edit paragraph");
  require(session.markdownText() == QStringLiteral("alpha!"), "insert text result mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "insert text cursor mismatch");

  require(input.deleteBackward(), "backspace should edit paragraph");
  require(session.markdownText() == QStringLiteral("alpha"), "backspace result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace cursor mismatch");
  require(undoStack.canUndo(), "input should push undo transaction");
}

void testInputEnterMovesCursorToNewParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha beta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertParagraphBreak(), "enter should split paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta"), "enter split text mismatch");
  require(session.document().root().children().size() == 2, "enter should create two paragraphs");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "enter cursor should move to new paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "enter cursor offset should be start of new paragraph");
}

void testInputEnterSplitsComplexInlineParagraphs() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.insertParagraphBreak(), "enter should split strong inline paragraph");
  require(session.markdownText() == QStringLiteral("before **bold**\n\nafter"), "strong inline split mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "strong inline split cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "strong inline split cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.insertParagraphBreak(), "enter should split code inline paragraph");
  require(session.markdownText() == QStringLiteral("before `code`\n\nafter"), "code inline split mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setCursor(selection, blockAt(session, 0), 10);
  require(input.insertParagraphBreak(), "enter should split inline math paragraph");
  require(session.markdownText() == QStringLiteral("before $x+y$\n\nafter"), "inline math split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix **bold**"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix b").size(), QStringLiteral("Plain text can mix **b").size());
  require(input.insertParagraphBreak(), "enter inside strong inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix **b**\n\n**old**"), "strong inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix *italic*"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix ita").size(), QStringLiteral("Plain text can mix *ita").size());
  require(input.insertParagraphBreak(), "enter inside emphasis inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix *ita*\n\n*lic*"), "emphasis inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix ~~through~~"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix thr").size(), QStringLiteral("Plain text can mix ~~thr").size());
  require(input.insertParagraphBreak(), "enter inside strike inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix ~~thr~~\n\n~~ough~~"), "strike inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("before co").size(), QStringLiteral("before `co").size());
  require(input.insertParagraphBreak(), "enter inside code inline should split wrapper");
  require(session.markdownText() == QStringLiteral("before `co`\n\n`de` after"), "code inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("before x").size(), QStringLiteral("before $x").size());
  require(input.insertParagraphBreak(), "enter inside math inline should split wrapper");
  require(session.markdownText() == QStringLiteral("before $x$\n\n$+y$ after"), "math inline wrapper split mismatch");
}

void testInputEditsComplexInlineSourcePositions() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 11);
  require(input.insertText(QStringLiteral("X")), "typing inside strong inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "strong inline source insert mismatch");
  require(selection.cursorPosition().text.sourceOffset == 12, "strong inline source cursor mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.insertText(QStringLiteral("X")), "typing inside code inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before `cXode` after"), "code inline source insert mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.insertText(QStringLiteral("0")), "typing inside inline math should edit markdown source");
  require(session.markdownText() == QStringLiteral("before $x0+y$ after"), "inline math source insert mismatch");

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 10, 12);
  require(input.deleteBackward(), "backspace inside strong inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before **bod** after"), "strong inline source backspace mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.deleteForward(), "delete inside code inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before `cde` after"), "code inline source delete mismatch");
}

void testCodeAndMathCursorAfterSourceInsert() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("$123$"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 1);
  require(input.insertText(QStringLiteral("a")), "typing before first math char should work");
  require(session.markdownText() == QStringLiteral("$a123$"), "math insert before first char mismatch");
  require(selection.cursorPosition().text.sourceOffset == 2, "math cursor source should stay after inserted char");
  require(selection.cursorPosition().text.textOffset == 1, "math cursor visible offset should stay after inserted char");

  session.setMarkdownText(QStringLiteral("`123`"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 1);
  require(input.insertText(QStringLiteral("a")), "typing before first code char should work");
  require(session.markdownText() == QStringLiteral("`a123`"), "code insert before first char mismatch");
  require(selection.cursorPosition().text.sourceOffset == 2, "code cursor source should stay after inserted char");
  require(selection.cursorPosition().text.textOffset == 1, "code cursor visible offset should stay after inserted char");
}

void testInlineProjectionMarkerSourcePositions() {
  QVector<InlineNode> strikeChildren;
  strikeChildren.push_back(InlineNode::text(QStringLiteral("through")));
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::strikethrough(QStringLiteral("~~"), strikeChildren));

  InlineProjectionState state;
  state.cursorSourceOffset = 1;
  InlineProjection projection(inlines, QStringLiteral("~~through~~"), state);
  require(projection.isValid(), "projection should be valid for strikethrough");
  qsizetype displayOffset = -1;
  require(projection.displayOffsetForSourceOffset(1, displayOffset), "projection should map marker source to display");
  require(displayOffset == 1, "projection marker display offset mismatch");
  qsizetype sourceOffset = -1;
  require(projection.sourceOffsetForDisplayOffset(1, sourceOffset), "projection should map marker display to source");
  require(sourceOffset == 1, "projection marker source offset mismatch");
}

struct ExpectedProjectionSpan {
  InlineType type = InlineType::Unknown;
  InlineSpanKind kind = InlineSpanKind::Text;
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  qsizetype contentSourceStart = 0;
  qsizetype contentSourceEnd = 0;
  qsizetype displayStart = 0;
  qsizetype displayEnd = 0;
  qsizetype visibleStart = 0;
  qsizetype visibleEnd = 0;
};

void requireSpanContract(const InlineProjectionSpan& span, const ExpectedProjectionSpan& expected, qsizetype index, const char* label) {
  const QString prefix = QStringLiteral("%1 span %2").arg(QString::fromUtf8(label)).arg(index);
  require(span.type == expected.type, prefix + QStringLiteral(" type mismatch"));
  require(span.kind == expected.kind, prefix + QStringLiteral(" kind mismatch"));
  require(span.sourceStart == expected.sourceStart, prefix + QStringLiteral(" sourceStart mismatch"));
  require(span.sourceEnd == expected.sourceEnd, prefix + QStringLiteral(" sourceEnd mismatch"));
  require(span.contentSourceStart == expected.contentSourceStart, prefix + QStringLiteral(" contentSourceStart mismatch"));
  require(span.contentSourceEnd == expected.contentSourceEnd, prefix + QStringLiteral(" contentSourceEnd mismatch"));
  require(span.displayStart == expected.displayStart, prefix + QStringLiteral(" displayStart mismatch"));
  require(span.displayEnd == expected.displayEnd, prefix + QStringLiteral(" displayEnd mismatch"));
  require(span.visibleStart == expected.visibleStart, prefix + QStringLiteral(" visibleStart mismatch"));
  require(span.visibleEnd == expected.visibleEnd, prefix + QStringLiteral(" visibleEnd mismatch"));
}

void requireProjectionSpans(
    const QVector<InlineNode>& inlines,
    const QString& markdown,
    InlineProjectionState state,
    const QVector<ExpectedProjectionSpan>& expected,
    const QString& displayText,
    const QString& visibleText,
    const char* label) {
  InlineProjection projection(inlines, markdown, state);
  require(projection.isValid(), label);
  require(projection.displayText() == displayText,
          QStringLiteral("%1 display text mismatch: %2").arg(QString::fromUtf8(label), projection.displayText()));
  require(projection.visibleText() == visibleText,
          QStringLiteral("%1 visible text mismatch: %2").arg(QString::fromUtf8(label), projection.visibleText()));
  require(projection.spans().size() == expected.size(),
          QStringLiteral("%1 span count mismatch: %2").arg(QString::fromUtf8(label)).arg(projection.spans().size()));
  for (qsizetype i = 0; i < expected.size(); ++i) {
    requireSpanContract(projection.spans().at(i), expected.at(i), i, label);
  }
}

void testInlineProjectionSpanContracts() {
  InlineProjectionState inactive;
  InlineProjectionState activeStrong;
  activeStrong.cursorSourceOffset = 2;
  requireProjectionSpans(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      activeStrong,
      {
          {InlineType::Strong, InlineSpanKind::OpenMarker, 0, 2, 0, 2, 0, 2, 0, 0},
          {InlineType::Text, InlineSpanKind::Text, 2, 3, 2, 3, 2, 3, 0, 1},
          {InlineType::Strong, InlineSpanKind::CloseMarker, 3, 5, 3, 5, 3, 5, 1, 1},
      },
      QStringLiteral("**x**"),
      QStringLiteral("x"),
      "active strong span contract");
  requireProjectionSpans(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      inactive,
      {
          {InlineType::Text, InlineSpanKind::Text, 0, 5, 2, 3, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive strong span contract");

  InlineProjectionState activeLink;
  activeLink.cursorSourceOffset = 1;
  requireProjectionSpans(
      {InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("[x](u)"),
      activeLink,
      {
          {InlineType::Link, InlineSpanKind::OpenMarker, 0, 1, 0, 1, 0, 1, 0, 0},
          {InlineType::Link, InlineSpanKind::Text, 1, 2, 1, 2, 1, 2, 0, 1},
          {InlineType::Link, InlineSpanKind::HiddenSyntax, 2, 6, 2, 6, 2, 6, 1, 1},
      },
      QStringLiteral("[x](u)"),
      QStringLiteral("x"),
      "active link span contract");
  requireProjectionSpans(
      {InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("[x](u)"),
      inactive,
      {
          {InlineType::Link, InlineSpanKind::Text, 1, 2, 1, 2, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive link span contract");

  InlineProjectionState activeImage;
  activeImage.cursorSourceOffset = 2;
  requireProjectionSpans(
      {InlineNode::image(QStringLiteral("u"), QStringLiteral("x"), QString())},
      QStringLiteral("![x](u)"),
      activeImage,
      {
          {InlineType::Image, InlineSpanKind::OpenMarker, 0, 2, 0, 2, 0, 2, 0, 0},
          {InlineType::Image, InlineSpanKind::Atom, 0, 7, 2, 3, 2, 3, 0, 1},
          {InlineType::Image, InlineSpanKind::HiddenSyntax, 3, 7, 3, 7, 3, 7, 1, 1},
      },
      QStringLiteral("![x](u)"),
      QStringLiteral("x"),
      "active image span contract");
  requireProjectionSpans(
      {InlineNode::image(QStringLiteral("u"), QStringLiteral("x"), QString())},
      QStringLiteral("![x](u)"),
      inactive,
      {
          {InlineType::Image, InlineSpanKind::Atom, 0, 7, 0, 7, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive image span contract");
}

void requireProjectionRoundTrip(
    const QVector<InlineNode>& inlines,
    const QString& markdown,
    qsizetype cursorSourceOffset,
    const QString& visibleText,
    const QString& displayText,
    const char* label) {
  InlineProjectionState state;
  state.cursorSourceOffset = cursorSourceOffset;
  InlineProjection projection(inlines, markdown, state);
  require(projection.isValid(), label);
  require(projection.visibleText() == visibleText, QStringLiteral("%1 visible text mismatch: %2").arg(QString::fromUtf8(label), projection.visibleText()));
  require(projection.displayText() == displayText, QStringLiteral("%1 display text mismatch: %2").arg(QString::fromUtf8(label), projection.displayText()));

  for (qsizetype sourceOffset = 0; sourceOffset <= markdown.size(); ++sourceOffset) {
    qsizetype displayOffset = -1;
    require(projection.displayOffsetForSourceOffset(sourceOffset, displayOffset), "projection source->display should succeed");
    qsizetype mappedSourceOffset = -1;
    require(projection.sourceOffsetForDisplayOffset(displayOffset, mappedSourceOffset), "projection display->source should succeed");
    require(mappedSourceOffset >= 0 && mappedSourceOffset <= markdown.size(), "projection display round-trip should stay in source");
  }

  for (qsizetype visibleOffset = 0; visibleOffset <= visibleText.size(); ++visibleOffset) {
    qsizetype sourceOffset = -1;
    require(projection.sourceOffsetForVisibleOffset(visibleOffset, sourceOffset), "projection visible->source should succeed");
    qsizetype mappedVisibleOffset = -1;
    require(projection.visibleOffsetForSourceOffset(sourceOffset, mappedVisibleOffset), "projection source->visible should succeed");
    require(mappedVisibleOffset >= 0 && mappedVisibleOffset <= visibleText.size(), "projection visible round-trip should stay in visible text");
  }
}

void testInlineProjectionMappingMatrix() {
  requireProjectionRoundTrip(
      {InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("*x*"),
      1,
      QStringLiteral("x"),
      QStringLiteral("*x*"),
      "emphasis projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      2,
      QStringLiteral("x"),
      QStringLiteral("**x**"),
      "strong projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strikethrough(QStringLiteral("~~"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("~~x~~"),
      2,
      QStringLiteral("x"),
      QStringLiteral("~~x~~"),
      "strikethrough projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::code(QStringLiteral("x"))},
      QStringLiteral("`x`"),
      1,
      QStringLiteral("x"),
      QStringLiteral("`x`"),
      "code projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::inlineMath(QStringLiteral("x"))},
      QStringLiteral("$x$"),
      1,
      QStringLiteral("x"),
      QStringLiteral("$x$"),
      "inline math projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("label"))})},
      QStringLiteral("[label](https://example.com)"),
      2,
      QStringLiteral("label"),
      QStringLiteral("[label](https://example.com)"),
      "link projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::image(QStringLiteral("https://example.com/image.png"), QStringLiteral("alt"), QString())},
      QStringLiteral("![alt](https://example.com/image.png)"),
      2,
      QStringLiteral("alt"),
      QStringLiteral("![alt](https://example.com/image.png)"),
      "image projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strong(
          QStringLiteral("**"),
          {InlineNode::text(QStringLiteral("bold ")),
           InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("em"))})})},
      QStringLiteral("**bold *em***"),
      9,
      QStringLiteral("bold em"),
      QStringLiteral("**bold *em***"),
      "nested inline projection should be valid");
}

void testHorizontalNavigationEntersInlineMarkers() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);
  EditorView view;
  input.attach(&view);

  session.setMarkdownText(QStringLiteral("**bold**"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into strong opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "strong opener marker source offset mismatch");
  require(input.insertText(QStringLiteral("X")), "typing inside strong opener marker should edit source");
  require(session.markdownText() == QStringLiteral("*X*bold**"), "strong opener marker edit mismatch");

  session.setMarkdownText(QStringLiteral("*italic*"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into italic opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "italic opener marker source offset mismatch");

  session.setMarkdownText(QStringLiteral("~~through~~"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into strike opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "strike opener marker source offset mismatch");
  require(input.deleteBackward(), "backspace inside strike opener should edit source");
  require(session.markdownText() == QStringLiteral("~through~~"), "strike opener backspace mismatch");

  session.setMarkdownText(QStringLiteral("`code`"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move after code opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "code opener source offset mismatch");

  session.setMarkdownText(QStringLiteral("$x+y$"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move after math opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "math opener source offset mismatch");
}

void testTextHitActivationAddsSourceOffsetForInlineEditing() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = blockAt(session, 0)->id();
  hit.textNodeId = hit.blockId;
  hit.textOffset = 9;
  controller.activateHit(hit);

  require(controller.selection().cursorPosition().text.sourceOffset == 11, "text hit should resolve strong source offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing after text hit should edit strong inline");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "text hit inline insert mismatch");
}

void testEditorViewHitTestActivatesInlineSourceEditing() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());
  const QRectF blockRect = view.nodeRect(blockAt(session, 0)->id());
  require(!blockRect.isEmpty(), "view should layout inline paragraph");
  const InlineLayout* inlineLayout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(inlineLayout != nullptr, "view hit test should find inline layout");

  const QPointF documentPos = blockRect.topLeft() + inlineLayout->cursorRectForSourceOffset(11).center();
  HitTestResult hit = view.hitTest(documentPos);
  require(hit.isValid() && hit.zone == HitTestResult::Zone::Text, "view hit test should return text hit");
  require(hit.sourceOffset == 11, "view hit test source offset mismatch");

  controller.activateHit(hit);
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "view hit should resolve source offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing after view hit should edit inline");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "view hit inline insert mismatch");
}

void testEditorViewInlineProjectionStateChanges() {
  DocumentSession session;
  EditorView view;
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const QRectF collapsedRect = view.nodeRect(block->id());
  const InlineLayout* collapsedLayout = view.blockAtViewportPos(collapsedRect.center())->inlineLayout();
  require(collapsedLayout != nullptr, "collapsed inline layout should exist");
  const QRectF collapsedCursor = collapsedLayout->cursorRectForSourceOffset(9);

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const InlineLayout* expandedLayout = view.blockAtViewportPos(expandedRect.center())->inlineLayout();
  require(expandedLayout != nullptr, "expanded inline layout should exist");
  const QRectF expandedCursor = expandedLayout->cursorRectForSourceOffset(9);
  require(expandedCursor.left() != collapsedCursor.left(), "cursor entering inline should expand marker layout");

  const QPointF expandedDocumentPos = expandedRect.topLeft() + expandedCursor.center();
  const HitTestResult expandedHit = view.hitTest(expandedDocumentPos);
  require(expandedHit.isValid() && expandedHit.sourceOffset == 9, "expanded inline hit-test should round-trip source offset");

  CursorPosition outside;
  outside.blockId = block->id();
  outside.text.nodeId = block->id();
  outside.text.textOffset = 0;
  outside.text.sourceOffset = 0;
  view.setCursorPosition(outside);
  const QRectF recollapsedRect = view.nodeRect(block->id());
  const InlineLayout* recollapsedLayout = view.blockAtViewportPos(recollapsedRect.center())->inlineLayout();
  require(recollapsedLayout != nullptr, "recollapsed inline layout should exist");
  require(recollapsedLayout->cursorRectForSourceOffset(9).left() == collapsedCursor.left(), "cursor leaving inline should collapse marker layout");

  SelectionRange selection;
  selection.anchor = inside;
  selection.focus = inside;
  selection.focus.text.textOffset = 3;
  selection.focus.text.sourceOffset = 11;
  view.setSelectionRange(selection);
  const QRectF selectedRect = view.nodeRect(block->id());
  const InlineLayout* selectedLayout = view.blockAtViewportPos(selectedRect.center())->inlineLayout();
  require(selectedLayout != nullptr, "selection inline layout should exist");
  require(selectedLayout->cursorRectForSourceOffset(9).left() != collapsedCursor.left(), "selection touching inline should expand marker layout");
}

void testEditorViewInlineLayoutSmoke() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  require(!blockRect.isEmpty(), "probe view should layout inline paragraph");
  const BlockLayout* blockLayout = view.blockAtViewportPos(blockRect.center());
  require(blockLayout != nullptr, "probe view should find block layout");
  const InlineLayout* inlineLayout = blockLayout->inlineLayout();
  require(inlineLayout != nullptr, "probe view should build inline layout");

  const QPointF textPoint = blockRect.topLeft() + inlineLayout->cursorRectForSourceOffset(11).center();
  const HitTestResult textHit = view.hitTest(textPoint);
  require(textHit.isValid() && textHit.zone == HitTestResult::Zone::Text, "probe view hit-test should hit text");
  require(textHit.sourceOffset == 11, "probe view hit-test source offset mismatch");

  view.setCursorHit(textHit);
  controller.activateHit(textHit);
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "probe view activation should keep source offset");
  const QRectF activatedRect = view.nodeRect(block->id());
  const BlockLayout* activatedBlock = view.blockAtViewportPos(activatedRect.center());
  require(activatedBlock != nullptr && activatedBlock->inlineLayout() != nullptr, "probe activated inline layout should exist");
  const QPointF activatedPoint = activatedRect.topLeft() + activatedBlock->inlineLayout()->cursorRectForSourceOffset(11).center();
  require(view.hitTest(activatedPoint).sourceOffset == 11, "probe view cursor rect should round-trip through hit-test");

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const BlockLayout* expandedBlock = view.blockAtViewportPos(expandedRect.center());
  require(expandedBlock != nullptr && expandedBlock->inlineLayout() != nullptr, "probe expanded inline layout should exist");
  const InlineLayout* expandedLayout = expandedBlock->inlineLayout();

  const QPointF markerPoint = expandedRect.topLeft() + expandedLayout->cursorRectForSourceOffset(8).center();
  const HitTestResult markerHit = view.hitTest(markerPoint);
  require(markerHit.isValid(), "probe marker hit should be valid");
  require(markerHit.sourceOffset == 8, "probe active marker source offset should round-trip");

  SelectionRange selection;
  selection.anchor = inside;
  selection.focus = inside;
  selection.focus.text.textOffset = 4;
  selection.focus.text.sourceOffset = 13;
  view.setSelectionRange(selection);
  const QRectF selectedRect = view.nodeRect(block->id());
  const BlockLayout* selectedBlock = view.blockAtViewportPos(selectedRect.center());
  require(selectedBlock != nullptr, "probe selected block layout should exist");
  const QVector<QRectF> selectionRects = selectedBlock->selectionRects(selection, RenderTheme::typoraLike());
  require(!selectionRects.isEmpty(), "probe view selection rects should be drawable");
  for (const QRectF& rect : selectionRects) {
    require(rect.width() > 0 && rect.height() > 0, "probe view selection rect should have area");
  }
}

const BlockLayout* requireViewBlock(EditorView& view, NodeId blockId, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  require(!blockRect.isEmpty(), label + QStringLiteral(" block rect should exist"));
  const BlockLayout* block = view.blockAtViewportPos(blockRect.center());
  require(block != nullptr, label + QStringLiteral(" block layout should exist"));
  return block;
}

const InlineLayout* requireViewInlineLayout(EditorView& view, NodeId blockId, const QString& label) {
  const BlockLayout* block = requireViewBlock(view, blockId, label);
  require(block->inlineLayout() != nullptr, label + QStringLiteral(" inline layout should exist"));
  return block->inlineLayout();
}

HitTestResult hitAtTextOffset(EditorView& view, NodeId blockId, qsizetype textOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRect(textOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

HitTestResult hitAtSourceOffset(EditorView& view, NodeId blockId, qsizetype sourceOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRectForSourceOffset(sourceOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" source cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

CursorPosition inlineCursor(NodeId blockId, qsizetype textOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = textOffset;
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

SelectionRange inlineSelection(NodeId blockId, qsizetype anchorOffset, qsizetype focusOffset) {
  SelectionRange selection;
  selection.anchor = inlineCursor(blockId, anchorOffset, anchorOffset);
  selection.focus = inlineCursor(blockId, focusOffset, focusOffset);
  return selection;
}

struct CursorLineRange {
  qsizetype start = 0;
  qsizetype end = 0;
  qreal y = 0;
};

QVector<CursorLineRange> cursorLineRanges(const InlineLayout& layout) {
  QVector<CursorLineRange> ranges;
  const qsizetype length = layout.plainText().size();
  for (qsizetype offset = 0; offset <= length; ++offset) {
    const QRectF cursor = layout.cursorRect(offset);
    if (cursor.isEmpty()) {
      continue;
    }
    const qreal y = cursor.center().y();
    if (ranges.isEmpty() || qAbs(ranges.last().y - y) > 0.5) {
      CursorLineRange range;
      range.start = offset;
      range.end = offset;
      range.y = y;
      ranges.push_back(range);
    } else {
      ranges.last().end = offset;
    }
  }
  return ranges;
}

void testEditorViewPlainInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("alpha beta gamma delta epsilon"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("plain inline"));
  const QVector<qsizetype> offsets{0, 1, 6, 12, inlineLayout->plainText().size()};
  for (qsizetype offset : offsets) {
    const HitTestResult hit = hitAtTextOffset(view, blockId, offset, QStringLiteral("plain inline %1").arg(offset));
    require(hit.textOffset == offset, QStringLiteral("plain inline hit offset mismatch"));
    require(hit.sourceOffset == offset, QStringLiteral("plain inline source offset mismatch"));
    require(!inlineLayout->cursorRect(offset).isEmpty(), QStringLiteral("plain inline cursor rect should exist"));
  }

  const SelectionRange selection = inlineSelection(blockId, 1, inlineLayout->plainText().size() - 1);
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("plain inline"))->selectionRects(selection, RenderTheme::typoraLike());
  require(!rects.isEmpty(), QStringLiteral("plain inline selection rects should exist"));
}

void testEditorViewStyledInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("before **bold** [link](u) `code` after"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const CursorPosition activeCursor = inlineCursor(blockId, 9, 11);
  view.setCursorPosition(activeCursor);

  const HitTestResult hit = hitAtSourceOffset(view, blockId, 11, QStringLiteral("styled inline"));
  require(hit.sourceOffset == 11, QStringLiteral("styled active source offset mismatch"));

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("styled inline"));
  const QRectF cursor = inlineLayout->cursorRectForSourceOffset(11);
  require(!cursor.isEmpty(), QStringLiteral("styled active cursor rect should exist"));

  SelectionRange selection;
  selection.anchor = inlineCursor(blockId, 7, 9);
  selection.focus = inlineCursor(blockId, 11, 13);
  view.setSelectionRange(selection);
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("styled inline"))->selectionRects(selection, RenderTheme::typoraLike());
  require(!rects.isEmpty(), QStringLiteral("styled inline selection rects should exist"));
}

void testEditorViewWrappedInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(
      QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau"),
      false);
  view.resize(360, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("wrapped inline"));
  const QVector<CursorLineRange> lines = cursorLineRanges(*inlineLayout);
  require(lines.size() >= 2, QStringLiteral("wrapped inline view should create multiple lines"));

  for (const CursorLineRange& line : lines) {
    QVector<qsizetype> offsets{line.start, (line.start + line.end) / 2, line.end};
    for (qsizetype offset : offsets) {
      const HitTestResult hit = hitAtTextOffset(view, blockId, offset, QStringLiteral("wrapped inline %1").arg(offset));
      require(hit.textOffset == offset, QStringLiteral("wrapped inline hit-test should round-trip line offset"));
      require(hit.sourceOffset == offset, QStringLiteral("wrapped inline source offset should round-trip line offset"));
    }
  }
  for (qsizetype i = 1; i < lines.size(); ++i) {
    require(lines.at(i).y > lines.at(i - 1).y, QStringLiteral("wrapped inline cursor y should increase by line"));
    const QRectF previousEnd = inlineLayout->cursorRect(lines.at(i - 1).end);
    const QRectF nextStart = inlineLayout->cursorRect(lines.at(i).start);
    require(nextStart.left() <= previousEnd.left(), QStringLiteral("wrapped inline line start should return toward x origin"));
  }

  const SelectionRange selection = inlineSelection(blockId, 0, inlineLayout->plainText().size());
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("wrapped inline"))->selectionRects(selection, RenderTheme::typoraLike());
  require(rects.size() == lines.size(), QStringLiteral("wrapped inline selection rect count should match line count"));
}

void testEditorViewTableCellInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("| Name | Value |\n| --- | --- |\n| one | two |"), false);
  view.resize(900, 500);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* firstBodyCell = childAt(childAt(table, 1), 0);
  const BlockLayout* tableLayout = requireViewBlock(view, table->id(), QStringLiteral("table inline"));
  require(tableLayout->tableRows().size() >= 2, QStringLiteral("table rows should exist"));
  require(!tableLayout->tableRows().at(1).cells.empty(), QStringLiteral("table cells should exist"));

  const auto& cell = tableLayout->tableRows().at(1).cells.at(0);
  require(cell.nodeId == firstBodyCell->id(), QStringLiteral("table cell node id mismatch"));
  require(!cell.text.cursorRect(1).isEmpty(), QStringLiteral("table cell cursor rect should exist"));
  require(!cell.text.selectionRects(0, 3).isEmpty(), QStringLiteral("table cell selection rects should exist"));

  const QMarginsF padding = RenderTheme::typoraLike().tableCellPadding();
  const QPointF point = cell.rect.marginsRemoved(padding).topLeft() + QPointF(cell.text.cursorRect(1).left(), cell.text.cursorRect(1).center().y());
  const HitTestResult hit = view.hitTest(point);
  require(hit.zone == HitTestResult::Zone::TableCell, QStringLiteral("table cell hit should use table cell zone"));
  require(hit.textNodeId == firstBodyCell->id(), QStringLiteral("table cell hit should return cell node id"));
}

void testMixedInlineParagraphHitEditingBeforeAutolink() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  const QString markdown = QStringLiteral(
      "Plain text can mix **bold**, *italic*, ~~strikethrough~~, `inline code`, inline HTML <kbd>Ctrl</kbd> + "
      "<kbd>S</kbd>, links such as [Muffin](https://example.com), autolinks like https://example.com, and inline math $E = mc^2$.");
  session.setMarkdownText(markdown, false);

  HitTestResult boldHit;
  boldHit.zone = HitTestResult::Zone::Text;
  boldHit.blockId = blockAt(session, 0)->id();
  boldHit.textNodeId = boldHit.blockId;
  boldHit.textOffset = QStringLiteral("Plain text can mix bo").size();
  controller.activateHit(boldHit);
  require(controller.inputController().insertText(QStringLiteral("X")), "typing in mixed paragraph bold should work");
  require(session.markdownText().contains(QStringLiteral("**boXld**")), "mixed paragraph bold insert mismatch");

  session.setMarkdownText(markdown, false);
  HitTestResult italicHit;
  italicHit.zone = HitTestResult::Zone::Text;
  italicHit.blockId = blockAt(session, 0)->id();
  italicHit.textNodeId = italicHit.blockId;
  italicHit.textOffset = QStringLiteral("Plain text can mix bold, ita").size();
  controller.activateHit(italicHit);
  require(controller.inputController().insertText(QStringLiteral("Y")), "typing in mixed paragraph italic should work");
  require(session.markdownText().contains(QStringLiteral("*itaYlic*")), "mixed paragraph italic insert mismatch");

  session.setMarkdownText(markdown, false);
  HitTestResult strikeHit;
  strikeHit.zone = HitTestResult::Zone::Text;
  strikeHit.blockId = blockAt(session, 0)->id();
  strikeHit.textNodeId = strikeHit.blockId;
  strikeHit.textOffset = QStringLiteral("Plain text can mix bold, italic, strike").size();
  controller.activateHit(strikeHit);
  require(controller.inputController().insertText(QStringLiteral("Z")), "typing in mixed paragraph strike should work");
  require(session.markdownText().contains(QStringLiteral("~~strikeZthrough~~")), "mixed paragraph strike insert mismatch");
}

void testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  QVector<BrushQueue::RefreshRequest> requests;
  QObject::connect(&brushQueue, &BrushQueue::refreshRequested, [&requests](BrushQueue::RefreshRequest request) {
    requests.push_back(std::move(request));
  });
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId originalAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "paragraph start enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph start enter should create virtual empty block");
  require(session.lastLocalEditChangedTopLevelStructure(), "paragraph start enter should mark top-level structure changed");
  brushQueue.flush();
  require(requests.size() == 1, "paragraph start enter should request one refresh");
  require(requests.last().fullLayoutDirty, "paragraph start enter should request full layout refresh");
  require(requests.last().layoutDirtyBlocks.isEmpty(), "paragraph start enter full refresh should not keep block dirty ids");
  require(blockAt(session, 1)->id() == originalAlphaId, "paragraph start enter should preserve original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\nxalpha"), "typing after paragraph start enter should not edit empty paragraph");
  require(!session.lastLocalEditChangedTopLevelStructure(), "typing after paragraph start enter should keep top-level structure stable");
  brushQueue.flush();
  require(requests.size() == 2, "typing after paragraph start enter should request another refresh");
  require(!requests.last().fullLayoutDirty, "typing after paragraph start enter should request block refresh");
  require(requests.last().layoutDirtyBlocks.size() == 1, "typing after paragraph start enter should dirty one block");
  require(requests.last().layoutDirtyBlocks.contains(blockAt(session, 1)->id()), "typing after paragraph start enter should dirty original paragraph");
  require(blockAt(session, 1)->id() == originalAlphaId, "typing after paragraph start enter should keep original paragraph id");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId repeatedAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph for repeat case");
  require(input.insertParagraphBreak(), "repeated enter at original paragraph start should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\nalpha"), "leading empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 3, "leading empty paragraph repeated enter should create another virtual block");
  require(blockAt(session, 2)->id() == repeatedAlphaId, "repeated paragraph start enter should keep original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(), "repeated paragraph start enter cursor block mismatch");
  require(input.insertText(QStringLiteral("before")), "typing after repeated paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\nbeforealpha"), "typing after repeated paragraph start enter text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "enter at paragraph end should create empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n"), "paragraph end enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph end enter should create virtual empty block");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph end enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph end enter cursor offset mismatch");
  require(input.insertParagraphBreak(), "enter in trailing empty paragraph should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\n"), "trailing empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 3, "trailing empty paragraph repeated enter should create another virtual block");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(), "trailing empty paragraph repeated enter cursor block mismatch");
  require(input.insertText(QStringLiteral("after")), "typing into trailing empty paragraph should work");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\nafter"), "typing into trailing empty paragraph text mismatch");
}

void testLocalReparsePreservesUntouchedNodeIds() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  const NodeId secondId = blockAt(session, 1)->id();
  const NodeId thirdId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertText(QStringLiteral("!")), "local text input should work");
  require(session.markdownText() == QStringLiteral("alpha!\n\nbeta\n\ngamma"), "local input text mismatch");
  require(blockAt(session, 1)->id() == secondId, "local input should preserve untouched second paragraph id");
  require(blockAt(session, 2)->id() == thirdId, "local input should preserve untouched third paragraph id");
}

void testBlockEditContextSeparatesBlockAndContentRanges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "block edit context should resolve heading");
  require(context.blockRange.byteStart == 0, "heading block range should start at marker");
  require(context.blockRange.byteEnd == 9, "heading block range end mismatch");
  require(context.contentRange.byteStart == 3, "heading content range should skip marker");
  require(context.contentRange.byteEnd == 9, "heading content range end mismatch");
  require(context.contentText == QStringLiteral("Status"), "heading content text mismatch");
}

void testTextBlockCommandBuilderCreatesStructuralEnterCommands() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "builder test context should resolve heading");

  TextBlockCommandBuilder builder(&session, &resolver);
  TextBlockCommandBuilder::Command before =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(before.valid && before.handled, "builder should produce heading-start enter command");
  require(before.hasLocalEdit(), "builder heading-start command should be local edit");
  require(before.sourceStart == 0, "builder heading-start source start mismatch");
  require(before.removedLength == 0, "builder heading-start removed length mismatch");
  require(before.insertedText == QStringLiteral("\n\n"), "builder heading-start inserted text mismatch");
  require(before.preferredCursor.blockId == blockAt(session, 0)->id(), "builder heading-start cursor should prefer original heading");
  require(before.preferredCursor.text.textOffset == 0, "builder heading-start cursor offset mismatch");

  setCursor(selection, blockAt(session, 0), 6);
  require(resolver.current(context), "builder heading-end context should resolve");
  TextBlockCommandBuilder::Command after =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(after.valid && after.handled, "builder should produce heading-end enter command");
  require(after.hasLocalEdit(), "builder heading-end command should be local edit");
  require(after.sourceStart == 9, "builder heading-end source start mismatch");
  require(after.removedLength == 0, "builder heading-end removed length mismatch");
  require(after.insertedText == QStringLiteral("\n\n"), "builder heading-end inserted text mismatch");
  require(!after.preferredCursor.isValid(), "builder heading-end should use fallback cursor for new block");
}

void testInputMergeParagraphs() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 1), 0);

  require(input.deleteBackward(), "backspace at paragraph start should merge previous paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "backspace merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace merge cursor mismatch");
  EditTransaction mergeUndo = requireTextDeltaCommand(undoStack, "backspace merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "backspace merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "backspace merge inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.deleteForward(), "delete at paragraph end should merge next paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "delete merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete merge cursor mismatch");
  mergeUndo = requireTextDeltaCommand(undoStack, "delete merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "delete merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "delete merge inserted text mismatch");
}

void testInputBackspaceAtParagraphStartDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "backspace at document start should be handled as no-op");
  require(session.markdownText() == QStringLiteral("alpha"), "backspace at document start should not change text");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace at document start cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace at document start cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\n\n\nbeta"), false);
  setCursor(selection, blockAt(session, 2), 0);
  require(input.deleteBackward(), "backspace after empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta"), "backspace empty paragraph boundary mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "backspace empty boundary cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace empty boundary cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace from heading after empty paragraph should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "backspace should preserve current heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "backspace should keep current block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\nbody"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after heading should merge into heading");
  require(session.markdownText() == QStringLiteral("# Title body"), "backspace heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold** after"), "backspace complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "backspace complex inline cursor offset mismatch");
}

void testInputDeleteAtParagraphEndDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\n# Title"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.deleteForward(), "delete before heading should merge heading into paragraph");
  require(session.markdownText() == QStringLiteral("alpha Title"), "delete heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteForward(), "delete from empty paragraph before heading should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "delete should preserve next heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "delete should keep next block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "delete preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.deleteForward(), "delete after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold** after"), "delete complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "delete complex inline cursor offset mismatch");
}

void testInputUndoRedoSnapshots() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertText(QStringLiteral("!")), "insert should create transaction");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "plain insert undo should use text delta");
  require(undo.textDeltaCommand().delta.start == 5, "plain insert delta start mismatch");
  require(undo.textDeltaCommand().delta.removedText.isEmpty(), "plain insert delta removed text mismatch");
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("!"), "plain insert delta inserted text mismatch");
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(), undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("alpha"), "undo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "undo snapshot cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(), redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText() == QStringLiteral("alpha!"), "redo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "redo snapshot cursor mismatch");
}

void testControllerUndoRedoRemapsCursorAfterReparse() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("$123$"), false);
  view.setDocument(session.document());
  setSourceCursor(controller.selection(), blockAt(session, 0), 0, 1);
  require(controller.inputController().insertText(QStringLiteral("a")), "controller input should edit inline math");
  require(session.markdownText() == QStringLiteral("$a123$"), "controller inline math insert mismatch");

  controller.undo();
  require(session.markdownText() == QStringLiteral("$123$"), "controller undo text mismatch");
  require(controller.selection().hasCursor(), "controller undo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(), "controller undo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 1, "controller undo cursor source mismatch");
  view.setCursorPosition(controller.selection().cursorPosition());
  require(view.hitTest(view.nodeRect(blockAt(session, 0)->id()).center()).isValid(), "controller undo view should keep valid layout hit");

  controller.redo();
  require(session.markdownText() == QStringLiteral("$a123$"), "controller redo text mismatch");
  require(controller.selection().hasCursor(), "controller redo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(), "controller redo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 2, "controller redo cursor source mismatch");
}

void testInputSelectionReplaceAndDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.insertText(QStringLiteral("X")), "typing should replace selection");
  require(session.markdownText() == QStringLiteral("aXa"), "selection replace text mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "selection replace cursor mismatch");
  EditTransaction replaceUndo = requireTextDeltaCommand(undoStack, "selection replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 1, "selection replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "selection replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteBackward(), "backspace should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "backspace selection delete mismatch");
  EditTransaction backspaceUndo = requireTextDeltaCommand(undoStack, "selection backspace should use text delta command");
  require(backspaceUndo.textDeltaCommand().delta.start == 1, "selection backspace delta start mismatch");
  require(backspaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection backspace removed text mismatch");
  require(backspaceUndo.textDeltaCommand().delta.insertedText.isEmpty(), "selection backspace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteForward(), "delete should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "delete selection mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "selection delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 1, "selection delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection delete removed text mismatch");
  require(deleteUndo.textDeltaCommand().delta.insertedText.isEmpty(), "selection delete inserted text mismatch");
}

void testInputCrossParagraphSelectionReplaceAndDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 3);
  require(selectedMarkdown(session, selection) == QStringLiteral("pha\n\nbeta\n\ngam"), "cross selection markdown mismatch");
  require(selectedPlainText(session, selection) == QStringLiteral("pha\nbeta\ngam"), "cross selection plain text mismatch");

  require(input.insertText(QStringLiteral("X")), "typing should replace cross paragraph selection");
  require(session.markdownText() == QStringLiteral("alXma"), "cross paragraph replace mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "cross paragraph replace cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 3, "cross paragraph replace cursor offset mismatch");
  EditTransaction replaceUndo = requireTextDeltaCommand(undoStack, "cross paragraph replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 2, "cross paragraph replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("pha\n\nbeta\n\ngam"), "cross paragraph replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "cross paragraph replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 2);
  require(input.deleteSelection(), "delete should remove cross block selection");
  require(session.markdownText() == QStringLiteral("# Tiega"), "cross block delete mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "cross block delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 4, "cross block delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("tle\n\n- alpha\n- beta\n\nom"), "cross block delete removed text mismatch");
  require(deleteUndo.textDeltaCommand().delta.insertedText.isEmpty(), "cross block delete inserted text mismatch");
}

void testHeadingInput() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("# Title"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertText(QStringLiteral("!")), "heading text insert should edit heading body");
  require(session.markdownText() == QStringLiteral("# Title!"), "heading insert should preserve marker");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "heading insert cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "heading insert cursor offset mismatch");

  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "heading start backspace should be handled");
  require(session.markdownText() == QStringLiteral("# Title!"), "heading start backspace should not remove marker");
}

void testHeadingEnterAtStartInsertsParagraphBeforeBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("## Status"), false);
  const NodeId headingId = blockAt(session, 0)->id();
  setCursor(selection, blockAt(session, 0), 0);

  require(input.insertParagraphBreak(), "heading start enter should edit document");
  require(session.markdownText() == QStringLiteral("\n\n## Status"), "heading start enter should insert before heading marker");
  require(session.document().root().children().size() == 2, "heading start enter should create leading empty paragraph");
  require(blockAt(session, 1)->id() == headingId, "heading start enter should preserve heading id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading start enter cursor should stay on heading");
  require(selection.cursorPosition().text.textOffset == 0, "heading start enter cursor should stay at heading text start");
  EditTransaction enterUndo = requireTextDeltaCommand(undoStack, "heading start enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.start == 0, "heading start enter delta start mismatch");
  require(enterUndo.textDeltaCommand().delta.removedText.isEmpty(), "heading start enter removed text mismatch");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"), "heading start enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 6);

  require(input.insertParagraphBreak(), "heading end enter should edit document");
  require(session.markdownText() == QStringLiteral("## Status\n\n"), "heading end enter should insert after heading block");
  require(session.document().root().children().size() == 2, "heading end enter should create trailing empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading end enter cursor should move to empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "heading end enter cursor should be at empty paragraph start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading end enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"), "heading end enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Headings"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(input.insertParagraphBreak(), "heading middle enter should split heading");
  require(session.markdownText() == QStringLiteral("## Hea\n\n## dings"), "heading middle enter should preserve heading marker on second half");
  require(session.document().root().children().size() == 2, "heading middle enter should create two headings");
  require(blockAt(session, 0)->type() == BlockType::Heading, "heading middle enter first block should remain heading");
  require(blockAt(session, 1)->type() == BlockType::Heading, "heading middle enter second block should be heading");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading middle enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "heading middle enter cursor should be at second heading start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading middle enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n\n## ")), "heading middle enter delta should insert split marker");
}

void testListItemInput() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "unordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("- alpha!\n- beta"), "unordered list insert should preserve marker");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 0)->id(), "unordered list cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "unordered list cursor offset mismatch");

  setSelection(selection, listItemAt(session, 0, 0), 1, 5);
  require(input.insertText(QStringLiteral("X")), "unordered list selection replace should edit item body");
  require(session.markdownText() == QStringLiteral("- aX!\n- beta"), "unordered list selection replace mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "ordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("1. alpha!\n2. beta"), "ordered list insert should preserve marker");
}

void testListItemEditingCommands() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split unordered list item");
  require(session.markdownText() == QStringLiteral("- al\n- pha\n- beta"), "unordered list split mismatch");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 1)->id(), "unordered split cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "unordered split cursor offset mismatch");
  EditTransaction listUndo = requireTextDeltaCommand(undoStack, "unordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n- ")), "unordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n2. beta"), "ordered list split mismatch");
  listUndo = requireTextDeltaCommand(undoStack, "ordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n2. ")), "ordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix **bold**\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix b").size(),
                  QStringLiteral("- Plain text can mix **b").size());
  require(input.insertParagraphBreak(), "enter inside strong inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix **b**\n- **old**\n- beta"),
          "strong inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix *italic*\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix ita").size(),
                  QStringLiteral("- Plain text can mix *ita").size());
  require(input.insertParagraphBreak(), "enter inside emphasis inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix *ita*\n- *lic*\n- beta"),
          "emphasis inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix ~~through~~\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix thr").size(),
                  QStringLiteral("- Plain text can mix ~~thr").size());
  require(input.insertParagraphBreak(), "enter inside strike inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix ~~thr~~\n- ~~ough~~\n- beta"),
          "strike inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before `code` after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before co").size(), QStringLiteral("- before `co").size());
  require(input.insertParagraphBreak(), "enter inside code inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before `co`\n- `de` after\n- beta"), "code inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before $x+y$ after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before x").size(), QStringLiteral("- before $x").size());
  require(input.insertParagraphBreak(), "enter inside math inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before $x$\n- $+y$ after\n- beta"), "math inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- \n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.insertParagraphBreak(), "enter on empty list item should exit list");
  require(session.markdownText() == QStringLiteral("\n\n- beta"), "empty list enter exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.deleteBackward(), "backspace at list start should exit top-level list item");
  require(session.markdownText() == QStringLiteral("alpha\n- beta"), "top-level list backspace exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "tab should indent list item");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "list item indent mismatch");
  listUndo = requireTextDeltaCommand(undoStack, "list indent should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText == QStringLiteral("  "), "list indent delta inserted text mismatch");

  MarkdownNode* nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.outdentListItem(), "shift-tab should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "list item outdent mismatch");

  EditorView view;
  input.attach(&view);
  session.setMarkdownText(QStringLiteral("- alpha\n  - beta"), false);
  nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  require(input.eventFilter(&view, &backtab), "backtab key should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "backtab key list outdent mismatch");
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
  require(html.text.contains(QStringLiteral("## eadings")), "html export should be based on markdown fragment");
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

void testComplexBlockActivationRoutesInput() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| Name | Value |\n| --- | --- |\n| one | two |\n\n```cpp\nx\n```"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 0);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 0;
  controller.activateHit(codeHit);
  require(controller.codeFenceController().isEditing(), "code click should enter code edit mode");
  require(controller.selection().cursorPosition().text.textOffset == 0, "code click should preserve hit offset");

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 0;
  cellHit.textOffset = 3;
  controller.activateHit(cellHit);
  require(!controller.codeFenceController().isEditing(), "table click should leave code edit mode");
  require(controller.tableController().currentCell().isValid(), "table click should activate current cell");

  require(controller.inputController().insertText(QStringLiteral("Z")), "table input should edit the active cell");
  require(session.markdownText().contains(QStringLiteral("oneZ")), "table input should update table cell text");
  require(!session.markdownText().contains(QStringLiteral("xZ")), "table input should not continue editing code fence");
  require(!session.markdownText().contains(QStringLiteral("x\n\n```")), "table input should not grow code fence trailing blank lines");
}

void testTableStructureUndoUsesTableCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 1);

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 1;
  controller.activateHit(cellHit);

  require(controller.tableController().insertColumnAfter(), "table insert column should work");
  require(session.lastParseWasLocalEdit(), "table insert column should use local table apply");
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "table insert column text mismatch");
  require(controller.undoStack().canUndo(), "table insert column should push undo");
  require(controller.undoStack().undoText() == QStringLiteral("Insert Table Column After"), "table command undo label mismatch");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isTableCommand(), "table structure undo should use TableCommand");
  require(transaction.tableCommand().beforeTable != nullptr && transaction.tableCommand().afterTable != nullptr,
          "table command snapshots should exist");
  require(transaction.tableCommand().tableId.isValid() || transaction.tableCommand().tableIndex >= 0, "table command target missing");
  require(TableModelOps::columnCount(*transaction.tableCommand().beforeTable) == 2, "table command before column count mismatch");
  require(TableModelOps::columnCount(*transaction.tableCommand().afterTable) == 3, "table command after column count mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "table command undo text mismatch");
  require(controller.selection().hasCursor(), "table command undo should keep cursor");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "table command redo text mismatch");
  require(controller.selection().hasCursor(), "table command redo should keep cursor");
}

void testTableCellTextUndoUsesTextDeltaCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 1);

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 1;
  cellHit.textOffset = 1;
  controller.activateHit(cellHit);

  require(controller.inputController().insertText(QStringLiteral("|X")), "table cell input should work");
  require(session.markdownText().contains(QStringLiteral("| 1 | 2\\|X |")), "table cell input should escape pipe");
  require(controller.undoStack().canUndo(), "table cell input should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isTextDeltaCommand(), "table cell text undo should use TextDeltaCommand");
  require(transaction.textDeltaCommand().affectedNodes.contains(table->id()), "table cell text delta should refresh table");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "table cell text undo mismatch");
  require(controller.selection().hasCursor(), "table cell text undo should keep cursor");
  require(controller.selection().cursorPosition().text.nodeId.isValid(), "table cell text undo should keep text node");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("| 1 | 2\\|X |")), "table cell text redo mismatch");
  require(controller.selection().hasCursor(), "table cell text redo should keep cursor");
}

void testInsertTableUndoUsesInsertNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  MarkdownNode* paragraph = blockAt(session, 0);
  setCursor(controller.selection(), paragraph, 5);

  require(controller.insertTable(), "insert table should work");
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n| Header | Header |")), "insert table text mismatch");
  require(controller.undoStack().canUndo(), "insert table should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isInsertNodeCommand(), "insert table undo should use InsertNodeCommand");
  require(transaction.insertNodeCommand().nodeType == BlockType::Table, "insert table node type mismatch");
  require(transaction.insertNodeCommand().insertedNode != nullptr, "insert table node snapshot missing");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha"), "insert table undo text mismatch");

  controller.redo();
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n| Header | Header |")), "insert table redo text mismatch");
  require(controller.selection().hasCursor(), "insert table redo should keep cursor");
  require(controller.tableController().currentCell().isValid(), "insert table redo should restore table cursor");
}

void testCodeActivationKeepsClickedOffsetForTyping() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nabcd\n```"), false);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 2;
  controller.activateHit(codeHit);

  require(controller.codeFenceController().isEditing(), "code activation should enter edit mode");
  require(controller.selection().cursorPosition().text.textOffset == 2, "code activation should keep clicked offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing in active code block should work");
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code typing should use clicked offset");
}

void testCodeFenceUndoUsesReplaceNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nabcd\n```"), false);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 2;
  controller.activateHit(codeHit);

  require(controller.inputController().insertText(QStringLiteral("X")), "code input should work");
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code input text mismatch");
  require(controller.undoStack().canUndo(), "code input should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isReplaceNodeCommand(), "code input undo should use ReplaceNodeCommand");
  require(transaction.replaceNodeCommand().nodeType == BlockType::CodeFence, "code replace command type mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("```cpp\nabcd\n```"), "code replace undo text mismatch");
  require(controller.selection().hasCursor(), "code replace undo should keep cursor");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code replace redo text mismatch");
  require(controller.selection().hasCursor(), "code replace redo should keep cursor");
}

void testWholeBlockDeleteUsesRemoveNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  MarkdownNode* middle = blockAt(session, 1);
  setSelection(controller.selection(), middle, 0, 4);

  require(controller.inputController().deleteSelection(), "whole block delete should work");
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete markdown mismatch");
  require(controller.undoStack().canUndo(), "whole block delete should push undo");
  EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isRemoveNodeCommand(), "whole block delete should use RemoveNodeCommand");
  require(transaction.removeNodeCommand().nodeType == BlockType::Paragraph, "remove node command type mismatch");
  require(transaction.removeNodeCommand().removedNode != nullptr, "remove node command snapshot missing");
  require(transaction.removeNodeCommand().delta.removedText.contains(QStringLiteral("beta")), "remove node command removed text mismatch");
  require(transaction.removeNodeCommand().delta.insertedText.isEmpty(), "remove node command inserted text should be empty");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta\n\ngamma"), "whole block delete undo mismatch");
  controller.redo();
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete redo mismatch");
  require(controller.selection().hasCursor(), "whole block delete redo should keep cursor");
}

void testStructuredNodeCommandModels() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("# Title"), false);
  MarkdownNode* heading = firstBlockOfType(session, BlockType::Heading);
  require(heading != nullptr, "heading command target missing");
  setCursor(controller.selection(), heading, 0);
  const CursorPosition cursor = controller.selection().cursorPosition();

  SetNodeAttrCommand attrCommand{
      heading->id(),
      BlockType::Heading,
      0,
      NodeAttribute::HeadingLevel,
      1,
      2,
      cursor,
      cursor,
      QVector<NodeId>{heading->id()}};
  require(attrCommand.isValid(), "set node attr command should validate");
  EditTransaction attrTransaction(EditTransaction::Kind::ReplaceDocumentText, QStringLiteral("Set Heading Level"), attrCommand);
  require(attrTransaction.isSetNodeAttrCommand(), "set node attr transaction storage mismatch");
  auto afterHeading = heading->clone(CloneMode::PreserveIds);
  afterHeading->setHeadingLevel(2);
  session.applyNodeSnapshot(heading->id(), BlockType::Heading, 0, *afterHeading, true);
  controller.undoStack().push(attrTransaction);
  controller.undo();
  require(session.markdownText() == QStringLiteral("# Title"), "set node attr undo mismatch");
  controller.redo();
  require(session.markdownText() == QStringLiteral("## Title"), "set node attr redo mismatch");
  controller.undo();
  require(session.markdownText() == QStringLiteral("# Title"), "set node attr second undo mismatch");

  heading = firstBlockOfType(session, BlockType::Heading);
  const QString removedText = session.markdownText();
  RemoveNodeCommand removeCommand{
      heading->id(),
      BlockType::Heading,
      0,
      TextDelta{0, removedText, QString()},
      0,
      heading->clone(CloneMode::PreserveIds),
      cursor,
      CursorPosition(),
      QVector<NodeId>{heading->id()}};
  require(removeCommand.isValid(), "remove node command should validate");
  EditTransaction removeTransaction(EditTransaction::Kind::DeleteText, QStringLiteral("Remove Heading"), removeCommand);
  require(removeTransaction.isRemoveNodeCommand(), "remove node transaction storage mismatch");
}

void testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nfirst\n```\n\n```cpp\nsecond\n```"), false);
  MarkdownNode* firstCode = blockAt(session, 0);
  MarkdownNode* secondCode = blockAt(session, 1);
  require(firstCode->type() == BlockType::CodeFence, "first block should be code fence");
  require(secondCode->type() == BlockType::CodeFence, "second block should be code fence");

  HitTestResult firstHit;
  firstHit.zone = HitTestResult::Zone::Code;
  firstHit.blockId = firstCode->id();
  firstHit.textNodeId = firstCode->id();
  firstHit.textOffset = 0;
  controller.activateHit(firstHit);
  require(controller.inputController().insertText(QStringLiteral("A")), "first code input should work");

  HitTestResult secondHit;
  secondHit.zone = HitTestResult::Zone::Code;
  secondHit.blockId = blockAt(session, 1)->id();
  secondHit.textNodeId = blockAt(session, 1)->id();
  secondHit.textOffset = 0;
  controller.activateHit(secondHit);
  require(controller.inputController().insertText(QStringLiteral("B")), "second code input should work");

  require(session.markdownText().contains(QStringLiteral("Afirst")), "first code edit should remain in first code block");
  require(session.markdownText().contains(QStringLiteral("Bsecond")), "second code edit should target second code block");
  require(!session.markdownText().contains(QStringLiteral("BAfirst")), "second code input should not write into first code block");
}

void testInlineSelectionRects() {
  RenderTheme theme = RenderTheme::typoraLike();
  InlineLayout layout;
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma")));
  layout.build(inlines, theme, 180, theme.paragraphFont());

  const QVector<QRectF> rects = layout.selectionRects(1, 8);
  require(!rects.isEmpty(), "inline selection rects should not be empty");
  qreal totalWidth = 0;
  for (const QRectF& rect : rects) {
    require(rect.width() > 0, "selection rect should have positive width");
    require(rect.height() > 0, "selection rect should have positive height");
    totalWidth += rect.width();
  }
  require(totalWidth > 0, "selection rect total width should be positive");
  require(layout.selectionRects(3, 3).isEmpty(), "collapsed selection should not create rects");
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(Q_OS_MACOS)
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  QApplication app(argc, argv);
  testSelectionController();
  testSelectionControllerRange();
  testUndoStack();
  testBrushQueueBatchesRefreshRequests();
  testInputInsertAndBackspace();
  testInputEnterMovesCursorToNewParagraph();
  testInputEnterSplitsComplexInlineParagraphs();
  testInputEditsComplexInlineSourcePositions();
  testCodeAndMathCursorAfterSourceInsert();
  testInlineProjectionMarkerSourcePositions();
  testInlineProjectionSpanContracts();
  testInlineProjectionMappingMatrix();
  testHorizontalNavigationEntersInlineMarkers();
  testTextHitActivationAddsSourceOffsetForInlineEditing();
  testEditorViewHitTestActivatesInlineSourceEditing();
  testEditorViewInlineProjectionStateChanges();
  testEditorViewInlineLayoutSmoke();
  testEditorViewPlainInlineLayout();
  testEditorViewStyledInlineLayout();
  testEditorViewWrappedInlineLayout();
  testEditorViewTableCellInlineLayout();
  testMixedInlineParagraphHitEditingBeforeAutolink();
  testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph();
  testLocalReparsePreservesUntouchedNodeIds();
  testBlockEditContextSeparatesBlockAndContentRanges();
  testTextBlockCommandBuilderCreatesStructuralEnterCommands();
  testInputMergeParagraphs();
  testInputBackspaceAtParagraphStartDeletesStructuralBoundary();
  testInputDeleteAtParagraphEndDeletesStructuralBoundary();
  testInputUndoRedoSnapshots();
  testControllerUndoRedoRemapsCursorAfterReparse();
  testInputSelectionReplaceAndDelete();
  testInputCrossParagraphSelectionReplaceAndDelete();
  testHeadingInput();
  testHeadingEnterAtStartInsertsParagraphBeforeBlock();
  testListItemInput();
  testListItemEditingCommands();
  testStylizeCollapsedSkeletons();
  testTypingIntoCollapsedStyleSkeletons();
  testStylizeSelectionWrap();
  testStylizeUndoRedoTextDelta();
  testHeadingAndListItemStylize();
  testStylizeCrossParagraphSelectionWrap();
  testClipboardPlainText();
  testClipboardMarkdownPreservesLinePrefix();
  testClipboardCrossMarkdownPreservesStartPrefix();
  testSelectionSerializerFormats();
  testSelectionSerializerCrossComplexInlineEdges();
  testClipboardBlockSelectionFallback();
  testCodeFenceSelectionCopyUsesLiteralOffsets();
  testKeyboardNavigationBasics();
  testComplexBlockActivationRoutesInput();
  testTableStructureUndoUsesTableCommand();
  testTableCellTextUndoUsesTextDeltaCommand();
  testInsertTableUndoUsesInsertNodeCommand();
  testCodeActivationKeepsClickedOffsetForTyping();
  testCodeFenceUndoUsesReplaceNodeCommand();
  testWholeBlockDeleteUsesRemoveNodeCommand();
  testStructuredNodeCommandModels();
  testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock();
  testInlineSelectionRects();
  QApplication::clipboard()->clear();
  return 0;
}
