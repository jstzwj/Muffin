#include "app/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QKeyEvent>

#include <cstdlib>
#include <iostream>
#include <variant>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void require(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << "\n";
    std::exit(1);
  }
}

void runTest(const char* name, void (*test)()) {
  std::cerr << "RUN " << name << "\n";
  test();
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

void testFrontMatterEnterEditsContentAndBlockAfterCreatesParagraph() {
  {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);

    session.setMarkdownText(QStringLiteral("---\ntitle: 123213\n---\nBody"), false);
  MarkdownNode* frontMatter = firstBlockOfType(session, BlockType::FrontMatter);

  HitTestResult frontMatterHit;
  frontMatterHit.zone = HitTestResult::Zone::FrontMatter;
  frontMatterHit.blockId = frontMatter->id();
  frontMatterHit.textNodeId = frontMatter->id();
  frontMatterHit.textOffset = QStringLiteral("title: 123").size();
  controller.activateHit(frontMatterHit);
  require(controller.frontMatterController().isEditing(), "front matter click should enter edit mode");
  require(controller.inputController().insertParagraphBreak(), "enter inside front matter should edit literal content");
  require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n---")),
          "first front matter enter should split the current literal line immediately");
  frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
  require(frontMatter->literal() == QStringLiteral("title: 123\n213"), "front matter literal should preserve first inserted newline");

  frontMatterHit.blockId = frontMatter->id();
  frontMatterHit.textNodeId = frontMatter->id();
  frontMatterHit.textOffset = frontMatter->literal().size();
  controller.activateHit(frontMatterHit);
  require(controller.inputController().insertParagraphBreak(), "enter at front matter end should edit literal content");
  require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n\n---")),
          "front matter enter at end should preserve a new literal blank line");

  frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
  HitTestResult blockAfterHit;
  blockAfterHit.zone = HitTestResult::Zone::BlockAfter;
  blockAfterHit.blockId = frontMatter->id();
  blockAfterHit.textNodeId = frontMatter->id();
  controller.activateHit(blockAfterHit);
  require(!controller.frontMatterController().isEditing(), "block-after click should leave front matter edit mode");
  require(controller.selection().currentHit().zone == HitTestResult::Zone::BlockAfter, "block-after click should become current hit");
  MarkdownNode* blockAfterNode = session.document().node(controller.selection().cursorPosition().blockId);
  require(blockAfterNode != nullptr, "block-after cursor should reference an existing node");
  const SourceRange blockAfterRange = blockAfterNode->sourceRange();
  require(blockAfterRange.byteEnd >= blockAfterRange.byteStart, "block-after cursor node should have a valid source range");
  require(blockAfterRange.byteEnd <= session.markdownText().size(), "block-after cursor node source range should fit markdown text");
  require(controller.inputController().insertParagraphBreak(), "enter after front matter block should create a paragraph boundary");
    require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n\n---\n\n\nBody")),
            "block-after enter should create an empty paragraph after front matter");
  }

  {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);

    session.setMarkdownText(QStringLiteral("---\ntitle: 123213\n---"), false);
    MarkdownNode* frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
    HitTestResult blockAfterHit;
    blockAfterHit.zone = HitTestResult::Zone::BlockAfter;
    blockAfterHit.blockId = frontMatter->id();
    blockAfterHit.textNodeId = frontMatter->id();
    controller.activateHit(blockAfterHit);

    require(!controller.frontMatterController().isEditing(), "block-after text click should not enter front matter edit mode");
    require(controller.inputController().insertText(QStringLiteral("OK")), "typing after front matter should create a paragraph");
    require(session.markdownText() == QStringLiteral("---\ntitle: 123213\n---\nOK"),
            "typing after front matter should insert normal paragraph text after the block");
    require(firstBlockOfType(session, BlockType::Paragraph) != nullptr, "typed block-after text should parse as a paragraph");
  }
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

void testTableStructureUndoUsesTableCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 1);

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 1;
  controller.activateHit(cellHit);

  require(controller.tableController().insertColumnAfter(), "table insert column should work");
  require(session.lastParseWasLocalEdit(), "table insert column should use local table apply");
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "table insert column text mismatch");
  require(controller.undoStack().canUndo(), "table insert column should push undo");
  require(controller.undoStack().undoText() == QStringLiteral("Insert Table Column After"), "table command undo label mismatch");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isTableCommand(), "table structure undo should use TableCommand");
  require(transaction.tableCommand().beforeTable != nullptr && transaction.tableCommand().afterTable != nullptr,
          "table command snapshots should exist");
  require(transaction.tableCommand().tableId.isValid() || transaction.tableCommand().tableIndex >= 0, "table command target missing");
  require(TableModelOps::columnCount(*transaction.tableCommand().beforeTable) == 2, "table command before column count mismatch");
  require(TableModelOps::columnCount(*transaction.tableCommand().afterTable) == 3, "table command after column count mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "table command undo text mismatch");
  require(controller.selection().hasCursor(), "table command undo should keep cursor");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "table command redo text mismatch");
  require(controller.selection().hasCursor(), "table command redo should keep cursor");
}

void testTableCellTextUndoUsesTextDeltaCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  const NodeId tableId = table->id();
  MarkdownNode* tableCell = childAt(childAt(table, 1), 1);

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 1;
  cellHit.textOffset = 1;
  controller.activateHit(cellHit);

  require(controller.inputController().insertText(QStringLiteral("|X")), "table cell input should work");
  require(session.markdownText().contains(QStringLiteral("| 1 | 2\\|X |")), "table cell input should escape pipe");
  require(controller.undoStack().canUndo(), "table cell input should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isTextDeltaCommand(), "table cell text undo should use TextDeltaCommand");
  require(transaction.textDeltaCommand().affectedNodes.contains(tableId), "table cell text delta should refresh table");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "table cell text undo mismatch");
  require(controller.selection().hasCursor(), "table cell text undo should keep cursor");
  require(controller.selection().cursorPosition().text.nodeId.isValid(), "table cell text undo should keep text node");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("| 1 | 2\\|X |")), "table cell text redo mismatch");
  require(controller.selection().hasCursor(), "table cell text redo should keep cursor");
}

void testInsertTableUndoUsesInsertNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  MarkdownNode* paragraph = blockAt(session, 0);
  setCursor(controller.selection(), paragraph, 5);

  require(controller.insertTable(), "insert table should work");
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n| Header | Header |")), "insert table text mismatch");
  require(controller.undoStack().canUndo(), "insert table should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isInsertNodeCommand(), "insert table undo should use InsertNodeCommand");
  require(transaction.insertNodeCommand().nodeType == BlockType::Table, "insert table node type mismatch");
  require(transaction.insertNodeCommand().insertedNode != nullptr, "insert table node snapshot missing");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha"), "insert table undo text mismatch");

  controller.redo();
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n| Header | Header |")), "insert table redo text mismatch");
  require(controller.selection().hasCursor(), "insert table redo should keep cursor");
  require(controller.tableController().currentCell().isValid(), "insert table redo should restore table cursor");
}

void testResizeTableUndoUsesTableCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| A | B | C |\n| --- | --- | --- |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 2), 2);
  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 2;
  cellHit.tableColumn = 2;
  controller.activateHit(cellHit);

  require(controller.resizeTable(2, 2), "resize table should work");
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "resize table crop markdown mismatch");
  require(controller.undoStack().canUndo(), "resize table should push undo");
  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isTableCommand(), "resize table should use TableCommand");
  require(TableModelOps::rowCount(*transaction.tableCommand().afterTable) == 2, "resize table after row count mismatch");
  require(TableModelOps::columnCount(*transaction.tableCommand().afterTable) == 2, "resize table after column count mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText().contains(QStringLiteral("| A | B | C |")), "resize table undo text mismatch");
  require(controller.selection().hasCursor(), "resize table undo should keep cursor");

  controller.redo();
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), "resize table redo text mismatch");
  require(controller.selection().hasCursor(), "resize table redo should keep cursor");
}

void testDeleteTableUndoUsesRemoveNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("before\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nafter"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 0);
  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 0;
  controller.activateHit(cellHit);

  require(controller.deleteTable(), "delete table should work");
  require(session.markdownText() == QStringLiteral("before\n\nafter"), "delete table markdown mismatch");
  require(controller.undoStack().canUndo(), "delete table should push undo");
  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isRemoveNodeCommand(), "delete table should use RemoveNodeCommand");
  require(transaction.removeNodeCommand().nodeType == BlockType::Table, "delete table command type mismatch");
  require(transaction.removeNodeCommand().removedNode != nullptr, "delete table command snapshot missing");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText().contains(QStringLiteral("| A | B |")), "delete table undo text mismatch");

  controller.redo();
  require(session.markdownText() == QStringLiteral("before\n\nafter"), "delete table redo text mismatch");
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

void testCodeFenceUndoUsesReplaceNodeCommand() {
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

  require(controller.inputController().insertText(QStringLiteral("X")), "code input should work");
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code input text mismatch");
  require(controller.undoStack().canUndo(), "code input should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isReplaceNodeCommand(), "code input undo should use ReplaceNodeCommand");
  require(transaction.replaceNodeCommand().nodeType == BlockType::CodeFence, "code replace command type mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("```cpp\nabcd\n```"), "code replace undo text mismatch");
  require(controller.selection().hasCursor(), "code replace undo should keep cursor");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code replace redo text mismatch");
  require(controller.selection().hasCursor(), "code replace redo should keep cursor");
}

void testWholeBlockDeleteUsesRemoveNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  MarkdownNode* middle = blockAt(session, 1);
  setSelection(controller.selection(), middle, 0, 4);

  require(controller.inputController().deleteSelection(), "whole block delete should work");
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete markdown mismatch");
  require(controller.undoStack().canUndo(), "whole block delete should push undo");
  EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isRemoveNodeCommand(), "whole block delete should use RemoveNodeCommand");
  require(transaction.removeNodeCommand().nodeType == BlockType::Paragraph, "remove node command type mismatch");
  require(transaction.removeNodeCommand().removedNode != nullptr, "remove node command snapshot missing");
  require(transaction.removeNodeCommand().delta.removedText.contains(QStringLiteral("beta")), "remove node command removed text mismatch");
  require(transaction.removeNodeCommand().delta.insertedText.isEmpty(), "remove node command inserted text should be empty");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta\n\ngamma"), "whole block delete undo mismatch");
  controller.redo();
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete redo mismatch");
  require(controller.selection().hasCursor(), "whole block delete redo should keep cursor");
}

void testStructuredNodeCommandModels() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("# Title"), false);
  MarkdownNode* heading = firstBlockOfType(session, BlockType::Heading);
  require(heading != nullptr, "heading command target missing");
  setCursor(controller.selection(), heading, 0);
  const CursorPosition cursor = controller.selection().cursorPosition();

  SetNodeAttrCommand attrCommand{
      heading->id(),
      BlockType::Heading,
      0,
      NodeAttribute::HeadingLevel,
      1,
      2,
      cursor,
      cursor,
      QVector<NodeId>{heading->id()}};
  require(attrCommand.isValid(), "set node attr command should validate");
  EditTransaction attrTransaction(EditTransaction::Kind::ReplaceDocumentText, QStringLiteral("Set Heading Level"), attrCommand);
  require(attrTransaction.isSetNodeAttrCommand(), "set node attr transaction storage mismatch");
  auto afterHeading = heading->clone(CloneMode::PreserveIds);
  afterHeading->setHeadingLevel(2);
  session.applyNodeSnapshot(heading->id(), BlockType::Heading, 0, *afterHeading, true);
  controller.undoStack().push(attrTransaction);
  controller.undo();
  require(session.markdownText() == QStringLiteral("# Title"), "set node attr undo mismatch");
  controller.redo();
  require(session.markdownText() == QStringLiteral("## Title"), "set node attr redo mismatch");
  controller.undo();
  require(session.markdownText() == QStringLiteral("# Title"), "set node attr second undo mismatch");

  heading = firstBlockOfType(session, BlockType::Heading);
  const QString removedText = session.markdownText();
  RemoveNodeCommand removeCommand{
      heading->id(),
      BlockType::Heading,
      0,
      TextDelta{0, removedText, QString()},
      0,
      heading->clone(CloneMode::PreserveIds),
      cursor,
      CursorPosition(),
      QVector<NodeId>{heading->id()}};
  require(removeCommand.isValid(), "remove node command should validate");
  EditTransaction removeTransaction(EditTransaction::Kind::DeleteText, QStringLiteral("Remove Heading"), removeCommand);
  require(removeTransaction.isRemoveNodeCommand(), "remove node transaction storage mismatch");
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

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testFrontMatterEnterEditsContentAndBlockAfterCreatesParagraph);
  RUN_TEST(testComplexBlockActivationRoutesInput);
  RUN_TEST(testTableStructureUndoUsesTableCommand);
  RUN_TEST(testTableCellTextUndoUsesTextDeltaCommand);
  RUN_TEST(testInsertTableUndoUsesInsertNodeCommand);
  RUN_TEST(testResizeTableUndoUsesTableCommand);
  RUN_TEST(testDeleteTableUndoUsesRemoveNodeCommand);
  RUN_TEST(testCodeActivationKeepsClickedOffsetForTyping);
  RUN_TEST(testCodeFenceUndoUsesReplaceNodeCommand);
  RUN_TEST(testWholeBlockDeleteUsesRemoveNodeCommand);
  RUN_TEST(testStructuredNodeCommandModels);
  RUN_TEST(testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock);
#undef RUN_TEST
  return 0;
}
