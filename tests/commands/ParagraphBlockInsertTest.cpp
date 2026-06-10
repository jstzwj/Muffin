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

void testInsertCodeBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertCodeBlock(), "insert code block should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```")), "code block should contain fence markers");
  require(md.startsWith(QLatin1String("Hello")), "code block should preserve original text");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "code block undo should use TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == QStringLiteral("Hello"), "code block undo text mismatch");
}

void testInsertFormulaBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertFormulaBlock(), "insert formula block should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("$$")), "formula block should contain $$ markers");
  require(md.startsWith(QLatin1String("Hello")), "formula block should preserve original text");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "formula block undo should use TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == QStringLiteral("Hello"), "formula block undo text mismatch");
}

void testInsertLinkReference() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertLinkReference(), "insert link reference should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("[]: ")), "link reference should contain empty template");
  require(md.startsWith(QLatin1String("Hello")), "link reference should preserve original text");
  require(blockAt(session, 1)->type() == BlockType::LinkDefinition, "link reference should parse as a rendered definition block");
}

void testInsertLinkReferenceIntoEmptyDocument() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QString(), false);
  require(paragraph.insertLinkReference(), "insert link reference into empty document should succeed");

  require(session.markdownText() == QStringLiteral("[]: "), "empty document link reference markdown mismatch");
  require(session.document().root().children().size() == 1, "empty document link reference should render as one block");
  require(blockAt(session, 0)->type() == BlockType::LinkDefinition, "empty document link reference should parse as definition block");
}

void testInsertFootnoteDefinition() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertFootnoteDefinition(), "insert footnote should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("[^]: ")), "footnote should contain empty template");
  require(md.startsWith(QLatin1String("Hello")), "footnote should preserve original text");
  require(blockAt(session, 1)->type() == BlockType::FootnoteDefinition, "footnote should parse as a rendered definition block");
}

void testInsertHorizontalRule() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertHorizontalRule(), "insert horizontal rule should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("---")), "horizontal rule should contain thematic break markdown");
  require(md.startsWith(QLatin1String("Hello")), "horizontal rule should preserve original text");
  require(blockAt(session, 1)->type() == BlockType::ThematicBreak, "horizontal rule should parse as a thematic break block");
}

void testInsertParagraphBeforeAndAfter() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertParagraphBefore(), "insert before should succeed");
  require(session.document().root().children().size() == 2, "insert before should create 2 blocks");

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertParagraphAfter(), "insert after should succeed");
  require(session.document().root().children().size() == 2, "insert after should create 2 blocks");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "insert after first block should be paragraph");
}

void testInsertIntoEmptyDocument() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QString(), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.insertCodeBlock(), "insert code block into empty doc should succeed");
  require(session.markdownText().contains(QLatin1String("```")), "empty doc code block should contain fence");
}

void testToggleQuote() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.toggleQuote(), "wrap in quote should succeed");
  require(session.markdownText() == QStringLiteral("> Hello world"), "quote wrap text mismatch");

  MarkdownNode* quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "should create block quote");
  require(quote->children().size() == 1, "quote should have one child");

  setCursor(selection, firstChildOfType(quote, BlockType::Paragraph), 5);
  require(paragraph.toggleQuote(), "unwrap quote should succeed");

  require(session.markdownText() == QStringLiteral("Hello world"), "quote unwrap text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "unwrapped should be paragraph");
}

void testConvertToUnorderedList() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Item text"), false);
  setCursor(selection, blockAt(session, 0), 4);
  require(paragraph.convertToUnorderedList(), "convert to unordered list should succeed");
  require(session.markdownText() == QStringLiteral("- Item text"), "unordered list text mismatch");
  require(blockAt(session, 0)->type() == BlockType::List, "should create list block");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "list undo should use TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == QStringLiteral("Item text"), "list undo text mismatch");
}

void testConvertToOrderedList() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("First item"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.convertToOrderedList(), "convert to ordered list should succeed");
  require(session.markdownText() == QStringLiteral("1. First item"), "ordered list text mismatch");
  require(blockAt(session, 0)->type() == BlockType::List, "should create list block");

  const EditTransaction undo = undoStack.takeUndo();
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == QStringLiteral("First item"), "ordered list undo text mismatch");
}

void testConvertToTaskList() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Task item"), false);
  setCursor(selection, blockAt(session, 0), 4);
  require(paragraph.convertToTaskList(), "convert to task list should succeed");
  require(session.markdownText() == QStringLiteral("- [ ] Task item"), "task list text mismatch");
  require(blockAt(session, 0)->type() == BlockType::List, "should create list block");

  const EditTransaction undo = undoStack.takeUndo();
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText() == QStringLiteral("Task item"), "task list undo text mismatch");
}

void testConversionFromHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("## Title"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.convertToUnorderedList(), "heading -> list should succeed");
  require(session.markdownText() == QStringLiteral("- Title"), "heading -> list text mismatch");
  require(blockAt(session, 0)->type() == BlockType::List, "heading -> list type mismatch");

  session.setMarkdownText(QStringLiteral("### Quote me"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.toggleQuote(), "heading -> quote should succeed");
  require(session.markdownText() == QStringLiteral("> ### Quote me"), "heading -> quote text mismatch");
}

void testIsOnEditableBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.isOnEditableBlock(), "paragraph should be editable");

  session.setMarkdownText(QStringLiteral("# Title"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.isOnEditableBlock(), "heading should be editable");
  require(paragraph.currentHeadingLevel() == 1, "heading level should be 1");

  session.setMarkdownText(QStringLiteral("```\ncode\n```"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(!paragraph.isOnEditableBlock(), "code fence should not be editable");

  selection.clear();
  require(!paragraph.isOnEditableBlock(), "no cursor should not be editable");
  require(paragraph.currentHeadingLevel() == 0, "no cursor heading level should be 0");

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSelection(selection, blockAt(session, 0), 0, 3);
  require(!paragraph.isOnEditableBlock(), "selection should not be editable");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInsertCodeBlock);
  RUN_TEST(testInsertFormulaBlock);
  RUN_TEST(testInsertLinkReference);
  RUN_TEST(testInsertLinkReferenceIntoEmptyDocument);
  RUN_TEST(testInsertFootnoteDefinition);
  RUN_TEST(testInsertHorizontalRule);
  RUN_TEST(testInsertParagraphBeforeAndAfter);
  RUN_TEST(testInsertIntoEmptyDocument);
  RUN_TEST(testToggleQuote);
  RUN_TEST(testConvertToUnorderedList);
  RUN_TEST(testConvertToOrderedList);
  RUN_TEST(testConvertToTaskList);
  RUN_TEST(testConversionFromHeading);
  RUN_TEST(testIsOnEditableBlock);
#undef RUN_TEST
  return 0;
}
