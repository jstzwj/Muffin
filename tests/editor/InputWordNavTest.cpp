#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>

using namespace muffin;

// Ctrl+Left/Right word movement in rendered prose.
void testCtrlArrowsMoveByWord() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta gamma delta"), false);
  MarkdownNode* second = blockAt(session, 1);
  setCursor(selection, second, 0);

  require(pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier), "ctrl+right should be handled");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("beta ").size(),
          "ctrl+right from cell start should stop at next word start");

  pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier);
  require(selection.cursorPosition().text.textOffset == QStringLiteral("beta gamma ").size(),
          "second ctrl+right should reach the last word start");

  pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier);
  require(selection.cursorPosition().text.textOffset == QStringLiteral("beta gamma delta").size(),
          "ctrl+right at last word should reach block end");

  pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier);
  require(selection.cursorPosition().text.textOffset == QStringLiteral("beta gamma delta").size(),
          "ctrl+right at the final block end should stay put");

  pressKey(input, &view, Qt::Key_Left, Qt::ControlModifier);
  require(selection.cursorPosition().text.textOffset == QStringLiteral("beta gamma ").size(),
          "ctrl+left should stop at previous word boundary");

  pressKey(input, &view, Qt::Key_Left, Qt::ControlModifier);
  pressKey(input, &view, Qt::Key_Left, Qt::ControlModifier);
  require(selection.cursorPosition().text.textOffset == 0, "repeated ctrl+left should reach block start");

  pressKey(input, &view, Qt::Key_Left, Qt::ControlModifier);
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(),
          "ctrl+left at block start should cross into the previous block");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("alpha").size(),
          "crossing block start should land at the previous block's end");
}

void testCtrlShiftArrowsExtendByWord() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("beta gamma delta"), false);
  MarkdownNode* block = blockAt(session, 0);
  setCursor(selection, block, 0);

  require(pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier),
          "ctrl+shift+right should be handled");
  const SelectionRange range = selection.selection();
  require(!range.isCollapsed(), "ctrl+shift+right should extend the selection");
  require(selectedPlainText(session, selection) == QStringLiteral("beta"),
          "word extension should select exactly one word");
}

// Ctrl+Backspace / Ctrl+Delete eat one word within the block.
void testCtrlBackspaceDeletesWord() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("beta gamma delta"), false);
  MarkdownNode* block = blockAt(session, 0);
  const NodeId blockId = block->id();  // captured BEFORE the edit: the reparse may replace the node
  setCursor(selection, block, QStringLiteral("beta gamma delta").size());

  require(pressKey(input, &view, Qt::Key_Backspace, Qt::ControlModifier), "ctrl+backspace should be handled");
  const QString after = session.markdownText().toString();
  require(after.contains(QStringLiteral("gamma")), "ctrl+backspace must keep earlier words");
  require(!after.contains(QStringLiteral("delta")), "ctrl+backspace should remove the trailing word");
  require(selection.cursorPosition().blockId == blockId,
          "caret should stay in the edited block");
  require(selection.cursorPosition().text.textOffset <= QStringLiteral("beta gamma ").size(),
          "caret should rest at (or before, after trailing-space reparse) the deletion start");
  require(undoStack.canUndo(), "ctrl+backspace should record an undo transaction");
}

void testCtrlDeleteDeletesWordForward() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("beta gamma delta"), false);
  MarkdownNode* block = blockAt(session, 0);
  setCursor(selection, block, 0);

  require(pressKey(input, &view, Qt::Key_Delete, Qt::ControlModifier), "ctrl+delete should be handled");
  const QString after = session.markdownText().toString();
  require(!after.contains(QStringLiteral("beta")), "ctrl+delete should remove the leading word");
  require(after.contains(QStringLiteral("gamma delta")), "ctrl+delete must keep later words");
}

// At a block edge Ctrl+Backspace falls back to character semantics and merges blocks.
void testCtrlBackspaceAtBlockStartMerges() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta gamma"), false);
  setCursor(selection, blockAt(session, 1), 0);

  require(pressKey(input, &view, Qt::Key_Backspace, Qt::ControlModifier), "ctrl+backspace at block start is still ours");
  require(session.markdownText().toString() == QStringLiteral("alphabeta gamma"),
          "ctrl+backspace at block start should merge with the previous block");
}

// Ctrl+Home/Ctrl+End used to be dead code behind the Ctrl bail-out; the routing change revives
// them for widget contexts without the QAction (and tests).
void testCtrlHomeEndJump() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta gamma\n\ndelta"), false);
  setCursor(selection, blockAt(session, 1), 2);

  require(pressKey(input, &view, Qt::Key_End, Qt::ControlModifier), "ctrl+end should be handled");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(), "ctrl+end should reach the last block");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("delta").size(), "ctrl+end should land at text end");

  require(pressKey(input, &view, Qt::Key_Home, Qt::ControlModifier), "ctrl+home should be handled");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "ctrl+home should reach the first block");
  require(selection.cursorPosition().text.textOffset == 0, "ctrl+home should land at text start");
}

// Non-owned Ctrl combos must keep falling through to QAction shortcuts.
void testCtrlComboPassthrough() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha beta"), false);
  setCursor(selection, blockAt(session, 0), 0);

  require(!pressKey(input, &view, Qt::Key_S, Qt::ControlModifier), "ctrl+s must fall through to the menu action");
  require(!pressKey(input, &view, Qt::Key_F, Qt::ControlModifier), "ctrl+f must fall through");
  require(!pressKey(input, &view, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier),
          "ctrl+shift+backspace must stay with the table delete-row action");
}

// Word movement inside a table cell must stay within the cell (not jump cells).
void testCtrlArrowsInTableCell() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("| left words | right words |\n| --- | --- |\n| alpha beta | gamma delta |"), false);
  view.setDocument(session.document());
  MarkdownNode* table = blockAt(session, 0);

  // Caret at the start of the body cell (2,1) "gamma delta" — the last row's second cell.
  MarkdownNode* lastRow = nullptr;
  for (const auto& child : table->children()) {
    if (child->type() == BlockType::TableRow) {
      lastRow = child.get();
    }
  }
  require(lastRow != nullptr && lastRow->children().size() >= 2, "body row with two cells should be found");
  MarkdownNode* cell = lastRow->children().at(1).get();
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;  // header (0) + body row
  hit.tableColumn = 1;
  hit.textOffset = 0;
  controller.activateHit(hit);

  InputController& input = controller.inputController();
  require(pressKey(input, &view, Qt::Key_Right, Qt::ControlModifier), "ctrl+right in a cell should be handled");
  require(controller.selection().cursorPosition().text.nodeId == cell->id(),
          "word movement must not leave the cell");
  require(controller.selection().cursorPosition().text.textOffset == QStringLiteral("gamma ").size(),
          "ctrl+right in a cell should stop at the next word");
  const QString after = session.markdownText().toString();
  require(after.contains(QStringLiteral("alpha beta")), "no text should change on movement");
}

// Ctrl+Up/Down move by block in document order (Word-like paragraph navigation).
void testCtrlUpDownMoveByParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("first\n\n- item one\n- item two\n\nlast block"), false);
  MarkdownNode* list = blockAt(session, 1);
  setCursor(selection, listItemAt(session, 1, 1), 4);  // middle of "item two"

  require(pressKey(input, &view, Qt::Key_Down, Qt::ControlModifier), "ctrl+down should be handled");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(),
          "ctrl+down should reach the block after the list");
  require(selection.cursorPosition().text.textOffset == 0, "ctrl+down should land at the block start");

  require(pressKey(input, &view, Qt::Key_Up, Qt::ControlModifier), "ctrl+up should be handled");
  // Climbing out of the list: the previous editable block in document order is the last item.
  require(selection.cursorPosition().blockId == listItemAt(session, 1, 1)->id(),
          "ctrl+up should climb into the list's last item");
  require(selection.cursorPosition().text.textOffset == 0, "ctrl+up should land at the item start");

  pressKey(input, &view, Qt::Key_Up, Qt::ControlModifier);
  pressKey(input, &view, Qt::Key_Up, Qt::ControlModifier);
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(),
          "repeated ctrl+up should reach the first block");

  require(pressKey(input, &view, Qt::Key_Up, Qt::ControlModifier), "ctrl+up at the top is still ours");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(),
          "ctrl+up at the document top should stay put");

  // Shift variant extends the selection to the target block start.
  setCursor(selection, listItemAt(session, 1, 0), 2);
  require(pressKey(input, &view, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier),
          "ctrl+shift+up should be handled");
  require(!selection.selection().isCollapsed(), "ctrl+shift+up should extend the selection");
  require(selection.selection().anchor.blockId == listItemAt(session, 1, 0)->id(),
          "selection anchor should stay at the origin");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCtrlArrowsMoveByWord);
  RUN_TEST(testCtrlShiftArrowsExtendByWord);
  RUN_TEST(testCtrlBackspaceDeletesWord);
  RUN_TEST(testCtrlDeleteDeletesWordForward);
  RUN_TEST(testCtrlBackspaceAtBlockStartMerges);
  RUN_TEST(testCtrlHomeEndJump);
  RUN_TEST(testCtrlComboPassthrough);
  RUN_TEST(testCtrlArrowsInTableCell);
  RUN_TEST(testCtrlUpDownMoveByParagraph);
#undef RUN_TEST
  qInfo("All word-navigation tests passed.");
  return 0;
}
