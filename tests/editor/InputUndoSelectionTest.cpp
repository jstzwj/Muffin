#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "io/FileController.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QInputMethodEvent>
#include <QTemporaryFile>

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
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("!"),
          "plain insert delta inserted text mismatch");
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(),
                         undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText().toString() == QStringLiteral("alpha"), "undo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "undo snapshot cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(),
                         redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText().toString() == QStringLiteral("alpha!"), "redo snapshot text mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("$a123$"), "controller inline math insert mismatch");

  controller.undo();
  require(session.markdownText().toString() == QStringLiteral("$123$"), "controller undo text mismatch");
  require(controller.selection().hasCursor(), "controller undo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(),
          "controller undo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 1, "controller undo cursor source mismatch");
  view.setCursorPosition(controller.selection().cursorPosition());
  require(view.hitTest(view.nodeRect(blockAt(session, 0)->id()).center()).isValid(),
          "controller undo view should keep valid layout hit");

  controller.redo();
  require(session.markdownText().toString() == QStringLiteral("$a123$"), "controller redo text mismatch");
  require(controller.selection().hasCursor(), "controller redo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(),
          "controller redo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 2, "controller redo cursor source mismatch");
}

// Coalesced Backspaces must replay at the left edge of the complete removed span.
void testCoalescedBackspaceUndoRedo() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText(QStringLiteral("abcde"), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 3);

  require(controller.inputController().deleteBackward(), "first backspace should edit");
  require(controller.inputController().deleteBackward(), "second backspace should edit");
  require(session.markdownText().toString() == QStringLiteral("ade"), "backspace result mismatch");
  controller.undo();
  require(session.markdownText().toString() == QStringLiteral("abcde"), "coalesced undo must restore at the left edge");
  require(controller.selection().cursorPosition().text.textOffset == 3, "undo must restore original cursor");
  controller.redo();
  require(session.markdownText().toString() == QStringLiteral("ade"), "coalesced redo must remove the same span");
  require(controller.selection().cursorPosition().text.textOffset == 1, "redo must restore final cursor");

  // A third deletion must still merge relative to the updated left edge.
  require(controller.inputController().deleteBackward(), "third backspace should edit");
  require(session.markdownText().toString() == QStringLiteral("de"), "third backspace result mismatch");
  controller.undo();
  require(session.markdownText().toString() == QStringLiteral("abcde"), "three coalesced deletions must undo together");
}

void testExternalReloadResetsHistoryAndSelection() {
  QTemporaryFile file;
  require(file.open(), "reload fixture should open");
  require(file.write("12345") == 5, "reload fixture should write");
  file.flush();
  DocumentSession session;
  EditorView view;
  EditorController controller;
  FileController files;
  controller.attach(&session, &view);
  session.setFilePath(file.fileName());
  session.setMarkdownText(QStringLiteral("abc"), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);
  require(controller.inputController().insertText(QStringLiteral("X")), "pre-reload edit should succeed");
  require(controller.canUndo(), "pre-reload edit should be undoable");
  bool recoveryCleared = false;
  QObject::connect(&files, &FileController::documentBecameClean, &session,
                   [&](const QString& path) { recoveryCleared = path == file.fileName(); });
  require(files.reload(session, nullptr), "external reload should succeed");
  require(!controller.canUndo() && !controller.canRedo(), "reload must retire both histories");
  require(!controller.selection().hasCursor(), "reload must retire stale node selection");
  require(recoveryCleared, "successful reload must retire the discarded draft");
  controller.undo();
  require(session.markdownText().toString() == QStringLiteral("12345"),
          "undo after reload must not edit replacement text");
  require(!session.document().isModified(), "reload must remain clean");
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
  require(session.markdownText().toString() == QStringLiteral("aXa"), "selection replace text mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "selection replace cursor mismatch");
  EditTransaction replaceUndo = requireTextDeltaCommand(undoStack, "selection replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 1, "selection replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"),
          "selection replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"),
          "selection replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteBackward(), "backspace should delete selection");
  require(session.markdownText().toString() == QStringLiteral("aa"), "backspace selection delete mismatch");
  EditTransaction backspaceUndo =
      requireTextDeltaCommand(undoStack, "selection backspace should use text delta command");
  require(backspaceUndo.textDeltaCommand().delta.start == 1, "selection backspace delta start mismatch");
  require(backspaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"),
          "selection backspace removed text mismatch");
  require(backspaceUndo.textDeltaCommand().delta.insertedText.isEmpty(), "selection backspace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteForward(), "delete should delete selection");
  require(session.markdownText().toString() == QStringLiteral("aa"), "delete selection mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "selection delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 1, "selection delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"),
          "selection delete removed text mismatch");
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
  require(selectedMarkdown(session, selection) == QStringLiteral("pha\n\nbeta\n\ngam"),
          "cross selection markdown mismatch");
  require(selectedPlainText(session, selection) == QStringLiteral("pha\nbeta\ngam"),
          "cross selection plain text mismatch");

  require(input.insertText(QStringLiteral("X")), "typing should replace cross paragraph selection");
  require(session.markdownText().toString() == QStringLiteral("alXma"), "cross paragraph replace mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(),
          "cross paragraph replace cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 3, "cross paragraph replace cursor offset mismatch");
  EditTransaction replaceUndo =
      requireTextDeltaCommand(undoStack, "cross paragraph replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 2, "cross paragraph replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("pha\n\nbeta\n\ngam"),
          "cross paragraph replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"),
          "cross paragraph replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 2);
  require(input.deleteSelection(), "delete should remove cross block selection");
  require(session.markdownText().toString() == QStringLiteral("# Tiega"), "cross block delete mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "cross block delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 4, "cross block delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("tle\n\n- alpha\n- beta\n\nom"),
          "cross block delete removed text mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("# Title!"), "heading insert should preserve marker");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "heading insert cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "heading insert cursor offset mismatch");

  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "heading start backspace should be handled");
  require(session.markdownText().toString() == QStringLiteral("# Title!"),
          "heading start backspace should not remove marker");
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
  require(session.markdownText().toString() == QStringLiteral("\n\n## Status"),
          "heading start enter should insert before heading marker");
  require(session.document().root().children().size() == 2,
          "heading start enter should create leading empty paragraph");
  require(blockAt(session, 1)->id() == headingId, "heading start enter should preserve heading id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(),
          "heading start enter cursor should stay on original heading");
  require(selection.cursorPosition().text.textOffset == 0,
          "heading start enter cursor should be at original heading start");
  EditTransaction enterUndo = requireTextDeltaCommand(undoStack, "heading start enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.start == 0, "heading start enter delta start mismatch");
  require(enterUndo.textDeltaCommand().delta.removedText.isEmpty(), "heading start enter removed text mismatch");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"),
          "heading start enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 6);

  require(input.insertParagraphBreak(), "heading end enter should edit document");
  require(session.markdownText().toString() == QStringLiteral("## Status\n\n"),
          "heading end enter should insert after heading block");
  require(session.document().root().children().size() == 2, "heading end enter should create trailing empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(),
          "heading end enter cursor should move to empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0,
          "heading end enter cursor should be at empty paragraph start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading end enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"),
          "heading end enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Headings"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(input.insertParagraphBreak(), "heading middle enter should split heading");
  require(session.markdownText().toString() == QStringLiteral("## Hea\n\n## dings"),
          "heading middle enter should preserve heading marker on second half");
  require(session.document().root().children().size() == 2, "heading middle enter should create two headings");
  require(blockAt(session, 0)->type() == BlockType::Heading, "heading middle enter first block should remain heading");
  require(blockAt(session, 1)->type() == BlockType::Heading, "heading middle enter second block should be heading");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(),
          "heading middle enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0,
          "heading middle enter cursor should be at second heading start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading middle enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n\n## ")),
          "heading middle enter delta should insert split marker");
}

void testUndoRedoAfterBackingCompaction() {
  DocumentSession session;
  SelectionController selection;
  UndoStack stack;
  BrushQueue brushes;
  InputController input;
  wireInput(input, session, selection, stack, brushes);
  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  const QString pasted(600 * 1024, QLatin1Char('x'));
  require(input.insertText(pasted), "large paste creates an undo transaction");
  setSelection(selection, blockAt(session, 0), 5, 5 + pasted.size());
  require(input.deleteBackward(), "large selection deletion succeeds");
  require(session.markdownText().toString() == QStringLiteral("alpha") &&
              session.document().pieceText().backingStoreBytes() < 1024 * 1024,
          "deletion compacts the backing store while keeping undo history");
  const auto undo = stack.takeUndo();
  require(undo.isTextDeltaCommand(), "selection deletion retains a text-delta undo command");
  const auto& delta = undo.textDeltaCommand().delta;
  require(session.applyTextDelta(delta.start, delta.insertedText.size(), delta.removedText, true),
          "undo applies after backing buffers have been rebased");
  require(session.markdownText().toString() == QStringLiteral("alpha") + pasted,
          "undo restores every deleted character after compaction");
  const auto redo = stack.takeRedo();
  const auto& repeated = redo.textDeltaCommand().delta;
  require(session.applyTextDelta(repeated.start, repeated.removedText.size(), repeated.insertedText, true),
          "redo applies after compaction");
  require(session.markdownText().toString() == QStringLiteral("alpha"), "redo restores the compacted text");
}

void testNonTextInputPreservesSelection() {
  const QString documents[] = {
      QStringLiteral("alpha beta"),
      QStringLiteral("alpha\n\nbeta\n\ngamma"),
      QStringLiteral("```text\nalpha beta\n```\n"),
      QStringLiteral("| key | value |\n| --- | --- |\n| alpha | beta |\n"),
      QStringLiteral("<div>alpha beta</div>\n"),
  };
  struct Key {
    int code;
    Qt::KeyboardModifiers modifiers;
    QString text;
  };
  const Key keys[] = {
      {Qt::Key_Shift, Qt::ShiftModifier, {}},
      {Qt::Key_Control, Qt::ControlModifier, {}},
      {Qt::Key_Alt, Qt::AltModifier, {}},
      {Qt::Key_Meta, Qt::MetaModifier, {}},
      {Qt::Key_AltGr, Qt::GroupSwitchModifier, {}},
      {Qt::Key_CapsLock, Qt::NoModifier, {}},
      {Qt::Key_NumLock, Qt::NoModifier, {}},
      {Qt::Key_ScrollLock, Qt::NoModifier, {}},
      {Qt::Key_Insert, Qt::NoModifier, {}},
      {Qt::Key_F6, Qt::NoModifier, {}},
      {Qt::Key_Dead_Grave, Qt::NoModifier, QStringLiteral("`")},
      {Qt::Key_unknown, Qt::NoModifier, QString(QChar(0x1b))},
  };
  for (const QString& original : documents) {
    for (bool reversed : {false, true}) {
      DocumentSession session;
      EditorView view;
      EditorController controller;
      controller.attach(&session, &view);
      session.setMarkdownText(original, false);
      view.setDocument(session.document());
      const qsizetype start = original.indexOf(QStringLiteral("alpha")) + 1;
      const qsizetype end = original.indexOf(QStringLiteral("beta")) + 3;
      require(controller.inputController().selectSourceRange(reversed ? end : start, reversed ? start : end),
              "non-text input fixture should select source across editable blocks");
      const SelectionRange before = controller.selection().selection();
      const quint64 revision = session.document().revision();
      const quint64 history = controller.undoStack().stateId();
      const auto requireUnchanged = [&] {
        require(session.markdownText().toString() == original, "non-text input must not delete selected text");
        const auto& after = controller.selection().selection();
        require(!after.isCollapsed() && after.anchor.blockId == before.anchor.blockId &&
                    after.focus.blockId == before.focus.blockId &&
                    after.anchor.text.sourceOffset == before.anchor.text.sourceOffset &&
                    after.focus.text.sourceOffset == before.focus.text.sourceOffset,
                "non-text input must preserve selection endpoints and direction");
        require(session.document().revision() == revision && !session.document().isModified() &&
                    controller.undoStack().stateId() == history && !controller.undoStack().canUndo(),
                "non-text input must not reparse, mark modified or create undo history");
      };
      for (const Key& key : keys) {
        QKeyEvent press(QEvent::KeyPress, key.code, key.modifiers, key.text);
        QApplication::sendEvent(&view, &press);
        requireUnchanged();
        QKeyEvent release(QEvent::KeyRelease, key.code, key.modifiers, key.text);
        QApplication::sendEvent(&view, &release);
        requireUnchanged();
      }
      require(!controller.inputController().insertText(QString()), "empty insertion should be rejected");
      requireUnchanged();
      QInputMethodEvent cancelledComposition;
      QApplication::sendEvent(&view, &cancelledComposition);
      requireUnchanged();
    }
  }
}

void testSelectionReplacementAfterShift() {
  for (const QString& original : {QStringLiteral("alpha beta"), QStringLiteral("alpha\n\nbeta\n\ngamma")}) {
    DocumentSession session;
    EditorView view;
    EditorController controller;
    controller.attach(&session, &view);
    session.setMarkdownText(original, false);
    view.setDocument(session.document());
    const qsizetype start = 1, end = original.size() - 1;
    require(controller.inputController().selectSourceRange(start, end), "select text to replace after Shift");
    QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QApplication::sendEvent(&view, &shift);
    QKeyEvent uppercase(QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier, QStringLiteral("X"));
    QApplication::sendEvent(&view, &uppercase);
    require(session.markdownText().toString() == original.left(start) + QStringLiteral("X") + original.mid(end),
            "Shift plus a printable key must still replace the selection");
    controller.undo();
    require(session.markdownText().toString() == original && !controller.undoStack().canUndo() &&
                !session.document().isModified(),
            "one undo must restore the original text and saved state after Shift typing");

    require(controller.inputController().selectSourceRange(end, start), "reselect text for an IME commit");
    QInputMethodEvent commit;
    const QString committed = QString::fromUtf8("中文");
    commit.setCommitString(committed);
    QApplication::sendEvent(&view, &commit);
    require(session.markdownText().toString() == original.left(start) + committed + original.mid(end),
            "an actual IME commit must still replace the selection");
    controller.undo();
    require(session.markdownText().toString() == original && !controller.undoStack().canUndo() &&
                !session.document().isModified(),
            "one undo must restore the original text and saved state after IME replacement");
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInputUndoRedoSnapshots);
  RUN_TEST(testUndoRedoAfterBackingCompaction);
  RUN_TEST(testNonTextInputPreservesSelection);
  RUN_TEST(testSelectionReplacementAfterShift);
  RUN_TEST(testCoalescedBackspaceUndoRedo);
  RUN_TEST(testExternalReloadResetsHistoryAndSelection);
  RUN_TEST(testControllerUndoRedoRemapsCursorAfterReparse);
  RUN_TEST(testInputSelectionReplaceAndDelete);
  RUN_TEST(testInputCrossParagraphSelectionReplaceAndDelete);
  RUN_TEST(testHeadingInput);
  RUN_TEST(testHeadingEnterAtStartInsertsParagraphBeforeBlock);
#undef RUN_TEST
  return 0;
}
