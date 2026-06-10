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

#include <iostream>
#include <variant>

using namespace muffin;

// testInputUndoRedoSnapshots (lines 786-813)
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

// testControllerUndoRedoRemapsCursorAfterReparse (lines 815-841)
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

// testInputSelectionReplaceAndDelete (lines 843-878)
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

// testInputCrossParagraphSelectionReplaceAndDelete (lines 880-910)
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

// testHeadingInput (lines 912-930)
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

// testHeadingEnterAtStartInsertsParagraphBeforeBlock (lines 932-978)
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
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading start enter cursor should stay on original heading");
  require(selection.cursorPosition().text.textOffset == 0, "heading start enter cursor should be at original heading start");
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

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInputUndoRedoSnapshots);
  RUN_TEST(testControllerUndoRedoRemapsCursorAfterReparse);
  RUN_TEST(testInputSelectionReplaceAndDelete);
  RUN_TEST(testInputCrossParagraphSelectionReplaceAndDelete);
  RUN_TEST(testHeadingInput);
  RUN_TEST(testHeadingEnterAtStartInsertsParagraphBeforeBlock);
#undef RUN_TEST
  return 0;
}
