#include "document/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include "ParagraphTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

void testSetHeadingLevel() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.setHeadingLevel(1), "paragraph -> H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Hello world"), "paragraph -> H1 text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "H1 block type mismatch");
  require(blockAt(session, 0)->headingLevel() == 1, "H1 level mismatch");

  require(paragraph.setHeadingLevel(3), "H1 -> H3 should succeed");
  require(session.markdownText() == QStringLiteral("### Hello world"), "H1 -> H3 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 3, "H3 level mismatch");

  require(paragraph.setHeadingLevel(0), "H3 -> paragraph should succeed");
  require(session.markdownText() == QStringLiteral("Hello world"), "H3 -> paragraph text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "paragraph block type mismatch");

  require(paragraph.setHeadingLevel(0), "paragraph -> paragraph should be no-op");
  require(session.markdownText() == QStringLiteral("Hello world"), "paragraph no-op text mismatch");
}

void testPromoteDemoteHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("### Title"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(paragraph.promoteHeading(), "promote H3 -> H2 should succeed");
  require(session.markdownText() == QStringLiteral("## Title"), "promote H3 -> H2 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "promote level mismatch");

  require(paragraph.promoteHeading(), "promote H2 -> H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Title"), "promote H2 -> H1 text mismatch");

  require(!paragraph.promoteHeading(), "promote H1 should fail");

  require(paragraph.demoteHeading(), "demote H1 -> H2 should succeed");
  require(session.markdownText() == QStringLiteral("## Title"), "demote H1 -> H2 text mismatch");

  require(paragraph.demoteHeading(), "demote H2 -> H3 should succeed");
  require(session.markdownText() == QStringLiteral("### Title"), "demote H2 -> H3 text mismatch");

  paragraph.setHeadingLevel(6);
  require(!paragraph.demoteHeading(), "demote H6 should fail");

  session.setMarkdownText(QStringLiteral("plain text"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(!paragraph.demoteHeading(), "demote on paragraph should fail");
  require(!paragraph.promoteHeading(), "promote on paragraph should fail");
}

void testHeadingUndoRedo() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(paragraph.setHeadingLevel(2), "set H2 should succeed");
  require(session.markdownText() == QStringLiteral("## Hello"), "set H2 text mismatch");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "heading undo should use TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("Hello"), "heading undo text mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyTextDelta(
      redo.textDeltaCommand().delta.start,
      redo.textDeltaCommand().delta.removedText.size(),
      redo.textDeltaCommand().delta.insertedText,
      true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText() == QStringLiteral("## Hello"), "heading redo text mismatch");
}

void testHeadingEmptyContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hi"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(2), "short paragraph -> H2 should succeed");
  require(blockAt(session, 0)->type() == BlockType::Heading, "short -> H2 type mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "short -> H2 level mismatch");

  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(0), "H2 -> paragraph should succeed");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "H2 -> paragraph type mismatch");
}

void testHeadingPreservesCursorInContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.setHeadingLevel(1), "set H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Hello world"), "H1 text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "cursor should stay at content offset 5");
}

void testHeadingMultiBlockDocument() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("First\n\nSecond\n\nThird"), false);
  require(session.document().root().children().size() == 3, "should have 3 blocks");

  setCursor(selection, blockAt(session, 1), 3);
  require(paragraph.setHeadingLevel(2), "convert second block to H2 should succeed");
  require(session.markdownText() == QStringLiteral("First\n\n## Second\n\nThird"), "multi-block H2 text mismatch");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should stay paragraph");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third block should stay paragraph");
  require(blockAt(session, 1)->type() == BlockType::Heading, "second block should be heading");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSetHeadingLevel);
  RUN_TEST(testPromoteDemoteHeading);
  RUN_TEST(testHeadingUndoRedo);
  RUN_TEST(testHeadingEmptyContent);
  RUN_TEST(testHeadingPreservesCursorInContent);
  RUN_TEST(testHeadingMultiBlockDocument);
#undef RUN_TEST
  return 0;
}
