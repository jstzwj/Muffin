#include "commands/StylizeController.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/ClipboardController.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "projection/SelectionSerializer.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>

#include <iostream>
#include <variant>

using namespace muffin;

// testClipboardBlockSelectionFallback (lines 742-760)
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

// testCodeFenceSelectionCopyUsesLiteralOffsets (lines 762-795)
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

// testCodeFenceSelectionCutDeletesLiteralOffsets (lines 797-827)
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
  require(controller.codeFenceController().enterEditMode(), "code fence edit mode should activate before cut");

  SelectionRange range;
  range.anchor.blockId = fence->id();
  range.anchor.text.nodeId = fence->id();
  range.anchor.text.textOffset = QStringLiteral("alpha ").size();
  range.focus.blockId = fence->id();
  range.focus.text.nodeId = fence->id();
  range.focus.text.textOffset = QStringLiteral("alpha beta").size();
  controller.selection().setSelection(range);

  require(controller.clipboardController().cut(), "code fence cut should delete active literal selection");
  require(QApplication::clipboard()->text().contains(QStringLiteral("beta")), "code fence cut clipboard should contain selected literal text");
  require(session.markdownText().contains(QStringLiteral("alpha  gamma")), "code fence cut should remove selected literal text");
  require(!session.markdownText().contains(QStringLiteral("beta")), "code fence cut should not leave selected literal text behind");
}

// testKeyboardNavigationBasics (lines 829-848)
void testKeyboardNavigationBasics() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

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

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testClipboardBlockSelectionFallback);
  RUN_TEST(testCodeFenceSelectionCopyUsesLiteralOffsets);
  RUN_TEST(testCodeFenceSelectionCutDeletesLiteralOffsets);
  RUN_TEST(testKeyboardNavigationBasics);
  RUN_TEST(testSelectAllContextSemantics);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
