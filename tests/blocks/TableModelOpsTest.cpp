#include "blocks/table/TableModelOps.h"
#include "blocks/table/TableController.h"
#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

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

MarkdownNode& parseTable(QString markdown, MarkdownDocument& document) {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(QStringView(markdown), options);
  require(parsed.root != nullptr, "parser returned null root");
  document.setMarkdownText(std::move(markdown), std::move(parsed.root));
  require(!document.root().children().empty(), "document has no blocks");
  MarkdownNode& table = *document.root().children().front();
  require(table.type() == BlockType::Table, "first block is not table");
  return table;
}

QString serialize(const MarkdownDocument& document) {
  MarkdownSerializer serializer;
  return serializer.serializeDocument(document);
}

void testNormalizeAndCellAccess() {
  MarkdownDocument document;
  MarkdownNode& table = parseTable(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), document);

  TableModelOps::normalize(table);
  require(TableModelOps::rowCount(table) == 2, "row count mismatch");
  require(TableModelOps::columnCount(table) == 2, "column count mismatch");
  require(TableModelOps::cellAt(table, 1, 1) != nullptr, "cellAt should find existing cell");
}

void testInsertDeleteRows() {
  MarkdownDocument document;
  MarkdownNode& table = parseTable(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), document);

  TableModelOps::insertRow(table, 1, InsertPosition::After);
  require(TableModelOps::rowCount(table) == 3, "insert row count mismatch");
  QString markdown = serialize(document);
  require(markdown.contains(QStringLiteral("|  |  |")), "inserted row should serialize empty cells");

  TableModelOps::deleteRow(table, 1);
  require(TableModelOps::rowCount(table) == 2, "delete row count mismatch");
}

void testInsertDeleteColumnsAndAlignment() {
  MarkdownDocument document;
  MarkdownNode& table = parseTable(QStringLiteral("| A | B |\n| :--- | ---: |\n| 1 | 2 |"), document);

  TableModelOps::insertColumn(table, 0, InsertPosition::After);
  require(TableModelOps::columnCount(table) == 3, "insert column count mismatch");
  require(table.tableAlignments().at(0) == TableAlignment::Left, "left alignment should remain");
  require(table.tableAlignments().at(1) == TableAlignment::None, "new column alignment should be none");
  require(table.tableAlignments().at(2) == TableAlignment::Right, "right alignment should shift");

  TableModelOps::setAlignment(table, 1, TableAlignment::Center);
  require(table.tableAlignments().at(1) == TableAlignment::Center, "set alignment mismatch");

  TableModelOps::deleteColumn(table, 0);
  require(TableModelOps::columnCount(table) == 2, "delete column count mismatch");
  require(table.tableAlignments().at(0) == TableAlignment::Center, "delete column should shift alignment");
}

void testMoveRowsAndColumns() {
  MarkdownDocument document;
  MarkdownNode& table = parseTable(QStringLiteral("| A | B | C |\n| --- | :---: | ---: |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |"), document);

  TableModelOps::moveRow(table, 2, 1);
  QString markdown = serialize(document);
  require(markdown.contains(QStringLiteral("| 4 | 5 | 6 |\n| 1 | 2 | 3 |")), "move row serialize mismatch");

  TableModelOps::moveColumn(table, 2, 0);
  require(table.tableAlignments().at(0) == TableAlignment::Right, "move column alignment mismatch");
  markdown = serialize(document);
  require(markdown.contains(QStringLiteral("| C | A | B |")), "move column header mismatch");
}

void testTableControllerCommands() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, 1, 1);
  require(cell != nullptr, "controller test cell missing");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = cell->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  selection.setHitResult(hit);

  const TableLocation location = controller.currentCell();
  require(location.isValid(), "current table cell should resolve");
  require(location.row == 1 && location.column == 1, "current table location mismatch");

  require(controller.insertColumnAfter(), "controller should insert column after");
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "controller insert column mismatch");
  require(undoStack.canUndo(), "table command should push undo");

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  cell = TableModelOps::cellAt(*session.document().root().children().front(), 1, 0);
  hit.blockId = cell->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 0;
  selection.setHitResult(hit);
  require(controller.setCurrentColumnAlignment(TableAlignment::Right), "controller should set alignment");
  require(session.markdownText().contains(QStringLiteral("| ---: | --- |")), "controller alignment mismatch");

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |"), false);
  MarkdownNode& moveTable = *session.document().root().children().front();
  cell = TableModelOps::cellAt(moveTable, 2, 1);
  hit.blockId = moveTable.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 2;
  hit.tableColumn = 1;
  selection.setHitResult(hit);
  require(controller.moveCurrentRowUp(), "controller should move row up");
  require(session.markdownText().contains(QStringLiteral("| 3 | 4 |\n| 1 | 2 |")), "controller move row mismatch");

  cell = TableModelOps::cellAt(*session.document().root().children().front(), 1, 1);
  hit.blockId = session.document().root().children().front()->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  selection.setHitResult(hit);
  require(controller.moveCurrentColumnLeft(), "controller should move column left");
  require(session.markdownText().contains(QStringLiteral("| B | A |")), "controller move column mismatch");
}

void testTableControllerCellTextEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, 1, 1);
  require(cell != nullptr, "cell editing target missing");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  hit.textOffset = 1;
  selection.setHitResult(hit);

  require(tableController.insertText(QStringLiteral("X")), "table cell insert should work");
  require(session.markdownText().contains(QStringLiteral("| 1 | 2X |")), "table cell insert markdown mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "table cell insert cursor mismatch");

  require(tableController.deleteBackward(), "table cell backspace should work");
  require(session.markdownText().contains(QStringLiteral("| 1 | 2 |")), "table cell backspace markdown mismatch");

  require(tableController.deleteBackward(), "table cell second backspace should work");
  require(session.markdownText().contains(QStringLiteral("| 1 |  |")), "table cell delete to empty mismatch");
}

void testTableControllerInsertTable() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  require(tableController.insertTable(), "insert table should work");
  require(session.markdownText().contains(QStringLiteral("| Header | Header |")), "insert table header mismatch");
  require(session.markdownText().contains(QStringLiteral("| --- | --- |")), "insert table delimiter mismatch");
  require(undoStack.canUndo(), "insert table should push undo");
}

}  // namespace

int main() {
  testNormalizeAndCellAccess();
  testInsertDeleteRows();
  testInsertDeleteColumnsAndAlignment();
  testMoveRowsAndColumns();
  testTableControllerCommands();
  testTableControllerCellTextEditing();
  testTableControllerInsertTable();
  return 0;
}
