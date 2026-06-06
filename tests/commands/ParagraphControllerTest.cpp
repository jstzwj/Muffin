#include "app/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"

#include <QApplication>

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

MarkdownNode* blockAt(const DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
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

void wireParagraph(
    ParagraphController& paragraph,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue) {
  paragraph.setDocumentSession(&session);
  paragraph.setSelectionController(&selection);
  paragraph.setUndoStack(&undoStack);
  paragraph.setBrushQueue(&brushQueue);
}

// ---------------------------------------------------------------------------
// Heading level tests
// ---------------------------------------------------------------------------

void testSetHeadingLevel() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Paragraph → Heading 1
  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.setHeadingLevel(1), "paragraph → H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Hello world"), "paragraph → H1 text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "H1 block type mismatch");
  require(blockAt(session, 0)->headingLevel() == 1, "H1 level mismatch");

  // Heading 1 → Heading 3
  require(paragraph.setHeadingLevel(3), "H1 → H3 should succeed");
  require(session.markdownText() == QStringLiteral("### Hello world"), "H1 → H3 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 3, "H3 level mismatch");

  // Heading 3 → Paragraph
  require(paragraph.setHeadingLevel(0), "H3 → paragraph should succeed");
  require(session.markdownText() == QStringLiteral("Hello world"), "H3 → paragraph text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "paragraph block type mismatch");

  // Same level is no-op
  require(paragraph.setHeadingLevel(0), "paragraph → paragraph should be no-op");
  require(session.markdownText() == QStringLiteral("Hello world"), "paragraph no-op text mismatch");
}

void testPromoteDemoteHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Start at H3
  session.setMarkdownText(QStringLiteral("### Title"), false);
  setCursor(selection, blockAt(session, 0), 3);

  // Promote: H3 → H2
  require(paragraph.promoteHeading(), "promote H3 → H2 should succeed");
  require(session.markdownText() == QStringLiteral("## Title"), "promote H3 → H2 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "promote level mismatch");

  // Promote: H2 → H1
  require(paragraph.promoteHeading(), "promote H2 → H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Title"), "promote H2 → H1 text mismatch");

  // Promote at H1: should fail
  require(!paragraph.promoteHeading(), "promote H1 should fail");

  // Demote: H1 → H2
  require(paragraph.demoteHeading(), "demote H1 → H2 should succeed");
  require(session.markdownText() == QStringLiteral("## Title"), "demote H1 → H2 text mismatch");

  // Demote: H2 → H3
  require(paragraph.demoteHeading(), "demote H2 → H3 should succeed");
  require(session.markdownText() == QStringLiteral("### Title"), "demote H2 → H3 text mismatch");

  // Demote to H6 then stop
  paragraph.setHeadingLevel(6);
  require(!paragraph.demoteHeading(), "demote H6 should fail");

  // Demote on plain paragraph should fail
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

  // Undo
  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "heading undo should use TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("Hello"), "heading undo text mismatch");

  // Redo
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

  // Convert a short paragraph to heading and back
  session.setMarkdownText(QStringLiteral("Hi"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(2), "short paragraph → H2 should succeed");
  require(blockAt(session, 0)->type() == BlockType::Heading, "short → H2 type mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "short → H2 level mismatch");

  // Heading back to paragraph — re-resolve cursor on the new heading node
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(0), "H2 → paragraph should succeed");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "H2 → paragraph type mismatch");
}

void testHeadingPreservesCursorInContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Set cursor in the middle of content, convert to heading, cursor should stay in content
  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.setHeadingLevel(1), "set H1 should succeed");
  require(session.markdownText() == QStringLiteral("# Hello world"), "H1 text mismatch");
  // Cursor should be at the same position in the content (offset 5), now at source offset 7 (after "# ")
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

  // Convert second paragraph to heading
  setCursor(selection, blockAt(session, 1), 3);
  require(paragraph.setHeadingLevel(2), "convert second block to H2 should succeed");
  require(session.markdownText() == QStringLiteral("First\n\n## Second\n\nThird"), "multi-block H2 text mismatch");

  // First and third blocks should be unchanged
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should stay paragraph");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third block should stay paragraph");
  require(blockAt(session, 1)->type() == BlockType::Heading, "second block should be heading");
}

// ---------------------------------------------------------------------------
// Block insert tests
// ---------------------------------------------------------------------------

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

  // Undo
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

  // Undo
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
  require(md.contains(QLatin1String("[label]: url")), "link reference should contain template");
  require(md.startsWith(QLatin1String("Hello")), "link reference should preserve original text");
}

void testInsertParagraphBeforeAndAfter() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Insert before
  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.insertParagraphBefore(), "insert before should succeed");
  require(session.document().root().children().size() == 2, "insert before should create 2 blocks");

  // Insert after
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

// ---------------------------------------------------------------------------
// Block conversion tests
// ---------------------------------------------------------------------------

void testToggleQuote() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Wrap in quote
  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.toggleQuote(), "wrap in quote should succeed");
  require(session.markdownText() == QStringLiteral("> Hello world"), "quote wrap text mismatch");

  // The block should now be inside a block quote
  MarkdownNode* quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "should create block quote");
  require(quote->children().size() == 1, "quote should have one child");

  // Unwrap quote
  setCursor(selection, firstChildOfType(quote, BlockType::Paragraph), 5);
  require(paragraph.toggleQuote(), "unwrap quote should succeed");

  // After unwrapping, the text should be plain again
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

  // Undo
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

  // Undo
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

  // Undo
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

  // Convert heading to list
  session.setMarkdownText(QStringLiteral("## Title"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.convertToUnorderedList(), "heading → list should succeed");
  require(session.markdownText() == QStringLiteral("- Title"), "heading → list text mismatch");
  require(blockAt(session, 0)->type() == BlockType::List, "heading → list type mismatch");

  // Convert heading to quote
  session.setMarkdownText(QStringLiteral("### Quote me"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.toggleQuote(), "heading → quote should succeed");
  require(session.markdownText() == QStringLiteral("> ### Quote me"), "heading → quote text mismatch");
}

// ---------------------------------------------------------------------------
// Query / guard tests
// ---------------------------------------------------------------------------

void testIsOnEditableBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Paragraph
  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.isOnEditableBlock(), "paragraph should be editable");

  // Heading
  session.setMarkdownText(QStringLiteral("# Title"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(paragraph.isOnEditableBlock(), "heading should be editable");
  require(paragraph.currentHeadingLevel() == 1, "heading level should be 1");

  // Code fence — not editable for paragraph commands
  session.setMarkdownText(QStringLiteral("```\ncode\n```"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(!paragraph.isOnEditableBlock(), "code fence should not be editable");

  // No cursor
  selection.clear();
  require(!paragraph.isOnEditableBlock(), "no cursor should not be editable");
  require(paragraph.currentHeadingLevel() == 0, "no cursor heading level should be 0");

  // Multi-block selection — should not be editable
  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSelection(selection, blockAt(session, 0), 0, 3);
  require(!paragraph.isOnEditableBlock(), "selection should not be editable");
}

}  // namespace

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
  RUN_TEST(testInsertCodeBlock);
  RUN_TEST(testInsertFormulaBlock);
  RUN_TEST(testInsertLinkReference);
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
