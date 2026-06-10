#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"
#include "theme/RenderTheme.h"

#include "../editor/EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>

#include <cstdlib>
#include <iostream>
#include <variant>

using namespace muffin;

// testTableStructureUndoUsesTableCommand (lines 194-233)
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

// testTableCellTextUndoUsesTextDeltaCommand (lines 235-271)
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

// testInsertTableUndoUsesInsertNodeCommand (lines 273-299)
void testInsertTableUndoUsesInsertNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  MarkdownNode* paragraph = blockAt(session, 0);
  setCursor(controller.selection(), paragraph, 5);

  require(controller.tableController().insertTable(), "insert table should work");
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n|  |  |")), "insert table text mismatch");
  require(controller.undoStack().canUndo(), "insert table should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isInsertNodeCommand(), "insert table undo should use InsertNodeCommand");
  require(transaction.insertNodeCommand().nodeType == BlockType::Table, "insert table node type mismatch");
  require(transaction.insertNodeCommand().insertedNode != nullptr, "insert table node snapshot missing");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha"), "insert table undo text mismatch");

  controller.redo();
  require(session.markdownText().startsWith(QStringLiteral("alpha\n\n|  |  |")), "insert table redo text mismatch");
  require(controller.selection().hasCursor(), "insert table redo should keep cursor");
  require(controller.tableController().currentCell().isValid(), "insert table redo should restore table cursor");
}

// testResizeTableUndoUsesTableCommand (lines 301-333)
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

  require(controller.tableController().resizeCurrentTable(2, 2), "resize table should work");
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

// testDeleteTableUndoUsesRemoveNodeCommand (lines 335-365)
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

  require(controller.tableController().deleteCurrentTable(), "delete table should work");
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

// testStructuredNodeCommandModels (lines 448-498)
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

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTableStructureUndoUsesTableCommand);
  RUN_TEST(testTableCellTextUndoUsesTextDeltaCommand);
  RUN_TEST(testInsertTableUndoUsesInsertNodeCommand);
  RUN_TEST(testResizeTableUndoUsesTableCommand);
  RUN_TEST(testDeleteTableUndoUsesRemoveNodeCommand);
  RUN_TEST(testStructuredNodeCommandModels);
#undef RUN_TEST
  return 0;
}
