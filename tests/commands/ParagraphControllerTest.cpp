#include "document/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
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

void setSourceSelection(SelectionController& selection, MarkdownNode* block,
                        qsizetype anchorText, qsizetype anchorSource,
                        qsizetype focusText, qsizetype focusSource) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchorText;
  range.anchor.text.sourceOffset = anchorSource;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focusText;
  range.focus.text.sourceOffset = focusSource;
  selection.setSelection(range);
}

void wireParagraph(
    ParagraphController& paragraph,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue) {
  EditorContext ctx;
  ctx.session = &session;
  ctx.selection = &selection;
  ctx.undoStack = &undoStack;
  ctx.brushQueue = &brushQueue;
  paragraph.setContext(ctx);
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

// ---------------------------------------------------------------------------
// Toggle code/math block tests
// ---------------------------------------------------------------------------

void testToggleCodeBlockSplitMiddle() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Paragraph "Hello world", cursor at space between Hello and world
  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleCodeBlock(), "split code block at middle should succeed");

  // normalizeSplitOffset removes the space, so result is Hello\n\n```\n\n```\n\nworld
  require(session.markdownText() == QStringLiteral("Hello\n\n```\n\n```\n\nworld"),
          "split middle text mismatch");

  // Verify block structure: Paragraph, CodeFence, Paragraph
  require(session.document().root().children().size() >= 3, "should have at least 3 blocks");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second block should be code fence");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third block should be paragraph");
}

void testToggleCodeBlockSplitAtStart() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(paragraph.toggleCodeBlock(), "split code block at start should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\n\n```")), "should contain empty code fence");
  require(md.contains(QLatin1String("Hello")), "text should be preserved after code fence");
  // Verify a CodeFence block exists in the parsed document
  bool foundCodeFence = false;
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::CodeFence) { foundCodeFence = true; break; }
  }
  require(foundCodeFence, "document should contain a CodeFence block");
}

void testToggleCodeBlockSplitAtEnd() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleCodeBlock(), "split code block at end should succeed");

  require(session.markdownText() == QStringLiteral("Hello\n\n```\n\n```\n\n"),
          "split at end text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second block should be code fence");
}

void testToggleCodeBlockHeadingSplit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // ## Hello world — cursor between Hello and world (source offset 8 = 3 + 5)
  session.setMarkdownText(QStringLiteral("## Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 8);
  require(paragraph.toggleCodeBlock(), "heading split should succeed");

  // Both halves keep the heading level (## ), space normalized away
  require(session.markdownText() == QStringLiteral("## Hello\n\n```\n\n```\n\n## world"),
          "heading split text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "first should be heading");
  require(blockAt(session, 0)->headingLevel() == 2, "first heading level should be 2");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second should be code fence");
  require(blockAt(session, 2)->type() == BlockType::Heading, "third should be heading");
  require(blockAt(session, 2)->headingLevel() == 2, "third heading level should be 2");
}

void testToggleCodeBlockInlineBold() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Bold text split at offset 13 (space between "bold" and "text")
  // "before **bold text** after" — cursor inside bold span
  session.setMarkdownText(QStringLiteral("before **bold text** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 13, 13);
  require(paragraph.toggleCodeBlock(), "inline bold split should succeed");

  // Bold should be closed before code block and reopened after
  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\n\n```")), "should contain empty code fence");
  require(md.startsWith(QLatin1String("before **bold**")), "bold should be closed in first paragraph");
  require(md.contains(QLatin1String("**text** after")), "bold should be reopened in last paragraph");
}

void testToggleCodeBlockWithSelection() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // "Hello world" — select "lo wo" (source offsets 3..8, i.e. 5 chars)
  // H=0 e=1 l=2 l=3 o=4 ' '=5 w=6 o=7 r=8 l=9 d=10
  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceSelection(selection, blockAt(session, 0), 3, 3, 8, 8);
  require(paragraph.toggleCodeBlock(), "selection wrap in code block should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nlo wo\n```")), "selected text should be in code block");
  require(md.startsWith(QLatin1String("Hel")), "before text should be paragraph A");
  require(md.contains(QLatin1String("rld")), "after text should be paragraph B");
}

void testToggleCodeBlockSelectionHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // "## Hello world" — select "lo wo" (source offsets 6..11, content offsets 3..8)
  // ## ' '=2 H=3 e=4 l=5 l=6 o=7 ' '=8 w=9 o=10 r=11 l=12 d=13
  session.setMarkdownText(QStringLiteral("## Hello world"), false);
  setSourceSelection(selection, blockAt(session, 0), 3, 6, 8, 11);
  require(paragraph.toggleCodeBlock(), "heading selection wrap should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nlo wo\n```")), "selected text in code block");
  // Both halves should keep ## heading prefix
  require(md.contains(QLatin1String("## Hel")), "before part keeps heading prefix");
  require(md.contains(QLatin1String("## rld")), "after part gets heading prefix");
}

void testToggleCodeBlockConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Code block with content → paragraph
  session.setMarkdownText(QStringLiteral("```\ncode here\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as code fence");

  setCursor(selection, blockAt(session, 0), 4);
  require(paragraph.toggleCodeBlock(), "code → paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
  require(session.markdownText() == QStringLiteral("code here"), "code → paragraph text mismatch");
}

void testToggleCodeBlockEmptyConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Empty code block → empty paragraph
  session.setMarkdownText(QStringLiteral("```\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as empty code fence");

  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.toggleCodeBlock(), "empty code → paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
}

void testToggleCodeBlockMathToCode() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Math block → code block (cross-convert)
  session.setMarkdownText(QStringLiteral("$$\nx=1\n$$"), false);
  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should start as math block");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleCodeBlock(), "math → code should succeed");

  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should become code fence");
  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nx=1\n```")), "math → code text mismatch");
}

void testToggleFormulaBlockSplitMiddle() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleFormulaBlock(), "formula split at middle should succeed");

  require(session.markdownText() == QStringLiteral("Hello\n\n$$\n\n$$\n\nworld"),
          "formula split text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::MathBlock, "second should be math block");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third should be paragraph");
}

void testToggleFormulaBlockConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("$$\nx^2\n$$"), false);
  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should start as math block");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleFormulaBlock(), "math → paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
  require(session.markdownText() == QStringLiteral("x^2"), "math → paragraph text mismatch");
}

void testToggleFormulaBlockCodeToMath() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // Code block → math block (cross-convert)
  session.setMarkdownText(QStringLiteral("```\ncode\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as code fence");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleFormulaBlock(), "code → math should succeed");

  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should become math block");
  const QString md = session.markdownText();
  require(md.contains(QLatin1String("$$\ncode\n$$")), "code → math text mismatch");
}

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

  // Undo
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

  require(paragraph.toggleCodeBlock(), "code → paragraph should succeed");

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

  // Toggle on the second paragraph
  setSourceCursor(selection, blockAt(session, 1), 3, 3 + 7);  // "Second" starts at offset 7
  require(paragraph.toggleCodeBlock(), "split middle block should succeed");

  // "Sec" + code block + "ond" + surrounding paragraphs
  const QString md = session.markdownText();
  require(md.startsWith(QLatin1String("First")), "first block should be unchanged");
  require(md.contains(QLatin1String("```\n\n```")), "should contain code fence");

  // First and last paragraphs should still exist
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

  // Place cursor inside the table (first body cell)
  MarkdownNode* table = blockAt(session, 0);
  require(table->children().size() >= 2, "table should have at least 2 rows");
  MarkdownNode* bodyRow = table->children().at(1).get();
  MarkdownNode* cell = bodyRow->children().at(0).get();
  setCursor(selection, cell, 0);

  require(paragraph.toggleCodeBlock(), "code block after table should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("| a |")), "table should still exist");
  require(md.contains(QLatin1String("```\n\n```")), "should insert code block after table");
  // Table should come first, then code block
  require(md.indexOf(QLatin1String("| a |")) < md.indexOf(QLatin1String("```")), "table should be before code block");
}

void testToggleFormulaBlockWithSelection() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  // "Hello world" — select "lo wo" (source offsets 3..8)
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
  RUN_TEST(testToggleCodeBlockSplitMiddle);
  RUN_TEST(testToggleCodeBlockSplitAtStart);
  RUN_TEST(testToggleCodeBlockSplitAtEnd);
  RUN_TEST(testToggleCodeBlockHeadingSplit);
  RUN_TEST(testToggleCodeBlockInlineBold);
  RUN_TEST(testToggleCodeBlockWithSelection);
  RUN_TEST(testToggleCodeBlockSelectionHeading);
  RUN_TEST(testToggleCodeBlockConvertsBack);
  RUN_TEST(testToggleCodeBlockEmptyConvertsBack);
  RUN_TEST(testToggleCodeBlockMathToCode);
  RUN_TEST(testToggleFormulaBlockSplitMiddle);
  RUN_TEST(testToggleFormulaBlockConvertsBack);
  RUN_TEST(testToggleFormulaBlockCodeToMath);
  RUN_TEST(testToggleCodeBlockUndo);
  RUN_TEST(testToggleCodeBlockUndoConvertBack);
  RUN_TEST(testToggleCodeBlockMultiBlockDocument);
  RUN_TEST(testToggleCodeBlockAfterTable);
  RUN_TEST(testToggleFormulaBlockWithSelection);
  RUN_TEST(testToggleCodeBlockNoCursorFails);
#undef RUN_TEST
  return 0;
}
