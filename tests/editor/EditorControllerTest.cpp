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

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
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

  InlineProjection projection(inlines, QStringLiteral("~~through~~"), 1);
  require(projection.isValid(), "projection should be valid for strikethrough");
  qsizetype displayOffset = -1;
  require(projection.displayOffsetForSourceOffset(1, displayOffset), "projection should map marker source to display");
  require(displayOffset == 1, "projection marker display offset mismatch");
  qsizetype sourceOffset = -1;
  require(projection.sourceOffsetForDisplayOffset(1, sourceOffset), "projection should map marker display to source");
  require(sourceOffset == 1, "projection marker source offset mismatch");
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
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId originalAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "paragraph start enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph start enter should create virtual empty block");
  require(blockAt(session, 1)->id() == originalAlphaId, "paragraph start enter should preserve original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\nxalpha"), "typing after paragraph start enter should not edit empty paragraph");
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
  require(before.markdownText == QStringLiteral("\n\n## Status"), "builder heading-start command text mismatch");
  require(before.preferredCursor.blockId == blockAt(session, 0)->id(), "builder heading-start cursor should prefer original heading");
  require(before.preferredCursor.text.textOffset == 0, "builder heading-start cursor offset mismatch");

  setCursor(selection, blockAt(session, 0), 6);
  require(resolver.current(context), "builder heading-end context should resolve");
  TextBlockCommandBuilder::Command after =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(after.valid && after.handled, "builder should produce heading-end enter command");
  require(after.markdownText == QStringLiteral("## Status\n\n"), "builder heading-end command text mismatch");
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

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.deleteForward(), "delete at paragraph end should merge next paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "delete merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete merge cursor mismatch");
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
  session.applyMarkdownText(undo.before().markdownText, true);
  selection.setCursorPosition(undo.before().cursor);
  require(session.markdownText() == QStringLiteral("alpha"), "undo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "undo snapshot cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyMarkdownText(redo.after().markdownText, true);
  selection.setCursorPosition(redo.after().cursor);
  require(session.markdownText() == QStringLiteral("alpha!"), "redo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "redo snapshot cursor mismatch");
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

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteBackward(), "backspace should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "backspace selection delete mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteForward(), "delete should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "delete selection mismatch");
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

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 2);
  require(input.deleteSelection(), "delete should remove cross block selection");
  require(session.markdownText() == QStringLiteral("# Tiega"), "cross block delete mismatch");
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

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 6);

  require(input.insertParagraphBreak(), "heading end enter should edit document");
  require(session.markdownText() == QStringLiteral("## Status\n\n"), "heading end enter should insert after heading block");
  require(session.document().root().children().size() == 2, "heading end enter should create trailing empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading end enter cursor should move to empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "heading end enter cursor should be at empty paragraph start");
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

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n2. beta"), "ordered list split mismatch");

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

void testStylizeUndoRedoSnapshots() {
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
  session.applyMarkdownText(undo.before().markdownText, true);
  selection.setCursorPosition(undo.before().cursor);
  require(session.markdownText() == QStringLiteral("alpha"), "style undo text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "style undo cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyMarkdownText(redo.after().markdownText, true);
  selection.setCursorPosition(redo.after().cursor);
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
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  testSelectionController();
  testSelectionControllerRange();
  testUndoStack();
  testInputInsertAndBackspace();
  testInputEnterMovesCursorToNewParagraph();
  testInputEnterSplitsComplexInlineParagraphs();
  testInputEditsComplexInlineSourcePositions();
  testCodeAndMathCursorAfterSourceInsert();
  testInlineProjectionMarkerSourcePositions();
  testHorizontalNavigationEntersInlineMarkers();
  testTextHitActivationAddsSourceOffsetForInlineEditing();
  testEditorViewHitTestActivatesInlineSourceEditing();
  testMixedInlineParagraphHitEditingBeforeAutolink();
  testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph();
  testLocalReparsePreservesUntouchedNodeIds();
  testBlockEditContextSeparatesBlockAndContentRanges();
  testTextBlockCommandBuilderCreatesStructuralEnterCommands();
  testInputMergeParagraphs();
  testInputBackspaceAtParagraphStartDeletesStructuralBoundary();
  testInputDeleteAtParagraphEndDeletesStructuralBoundary();
  testInputUndoRedoSnapshots();
  testInputSelectionReplaceAndDelete();
  testInputCrossParagraphSelectionReplaceAndDelete();
  testHeadingInput();
  testHeadingEnterAtStartInsertsParagraphBeforeBlock();
  testListItemInput();
  testListItemEditingCommands();
  testStylizeCollapsedSkeletons();
  testTypingIntoCollapsedStyleSkeletons();
  testStylizeSelectionWrap();
  testStylizeUndoRedoSnapshots();
  testHeadingAndListItemStylize();
  testStylizeCrossParagraphSelectionWrap();
  testClipboardPlainText();
  testClipboardMarkdownPreservesLinePrefix();
  testClipboardCrossMarkdownPreservesStartPrefix();
  testSelectionSerializerFormats();
  testSelectionSerializerCrossComplexInlineEdges();
  testClipboardBlockSelectionFallback();
  testKeyboardNavigationBasics();
  testComplexBlockActivationRoutesInput();
  testCodeActivationKeepsClickedOffsetForTyping();
  testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock();
  testInlineSelectionRects();
  return 0;
}
