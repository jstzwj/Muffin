#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "document/DocumentSession.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include "ParagraphTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

void testToggleCodeBlockUndo() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  const QString original = QStringLiteral("Hello world");
  session.setMarkdownText(original, false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);

  require(paragraph.toggleCodeBlock(), "split should succeed");
  require(session.markdownText() != original, "text should change after split");

  const EditTransaction undo = requireTextDeltaCommand(undoStack, "toggle code block undo should be TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == original, "undo should restore original text");
}

void testToggleCodeBlockUndoConvertBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  const QString original = QStringLiteral("```\ncode here\n```");
  session.setMarkdownText(original, false);
  setCursor(selection, blockAt(session, 0), 4);

  require(paragraph.toggleCodeBlock(), "code -> paragraph should succeed");

  const EditTransaction undo = requireTextDeltaCommand(undoStack, "convert back undo should be TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == original, "convert back undo should restore original");
}

void testToggleCodeBlockMultiBlockDocument() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("First\n\nSecond\n\nThird"), false);
  require(session.document().root().children().size() == 3, "should start with 3 blocks");

  setSourceCursor(selection, blockAt(session, 1), 3, 3 + 7);
  require(paragraph.toggleCodeBlock(), "split middle block should succeed");

  const QString md = session.markdownText();
  require(md.startsWith(QLatin1String("First")), "first block should be unchanged");
  require(md.contains(QLatin1String("```\n\n```")), "should contain code fence");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should be paragraph");
}

void testToggleCodeBlockAfterTable() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("| a |\n| --- |\n| b |"), false);
  require(blockAt(session, 0)->type() == BlockType::Table, "should start with table");

  MarkdownNode* table = blockAt(session, 0);
  require(table->children().size() >= 2, "table should have at least 2 rows");
  MarkdownNode* bodyRow = table->children().at(1).get();
  MarkdownNode* cell = bodyRow->children().at(0).get();
  setCursor(selection, cell, 0);

  require(paragraph.toggleCodeBlock(), "code block after table should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("| a |")), "table should still exist");
  require(md.contains(QLatin1String("```\n\n```")), "should insert code block after table");
  require(md.indexOf(QLatin1String("| a |")) < md.indexOf(QLatin1String("```")), "table should be before code block");
}

void testToggleFormulaBlockWithSelection() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceSelection(selection, blockAt(session, 0), 3, 3, 8, 8);
  require(paragraph.toggleFormulaBlock(), "selection wrap in formula block should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("$$\nlo wo\n$$")), "selected text should be in formula block");
}

void testToggleCodeBlockNoCursorFails() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  selection.clear();
  require(!paragraph.toggleCodeBlock(), "toggle without cursor should fail");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testToggleCodeBlockUndo);
  RUN_TEST(testToggleCodeBlockUndoConvertBack);
  RUN_TEST(testToggleCodeBlockMultiBlockDocument);
  RUN_TEST(testToggleCodeBlockAfterTable);
  RUN_TEST(testToggleFormulaBlockWithSelection);
  RUN_TEST(testToggleCodeBlockNoCursorFails);
#undef RUN_TEST
  return 0;
}
