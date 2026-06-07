#include "blocks/table/TableModelOps.h"
#include "blocks/table/TableController.h"
#include "blocks/table/TableCellSourceEdit.h"
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
#include <variant>

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

MarkdownNode* setTableCellCursor(
    DocumentSession& session,
    SelectionController& selection,
    int row,
    int column,
    qsizetype localSourceOffset,
    qsizetype textOffset = 0) {
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, row, column);
  require(cell != nullptr, "table cell cursor target missing");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = row;
  hit.tableColumn = column;
  hit.textOffset = textOffset;
  hit.sourceOffset = cell->sourceRange().byteStart + localSourceOffset;
  selection.setHitResult(hit);
  return cell;
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

void testResizeCropAndPad() {
  MarkdownDocument document;
  MarkdownNode& table = parseTable(QStringLiteral("| A | B | C |\n| :--- | :---: | ---: |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |"), document);

  TableModelOps::resize(table, 2, 2);
  require(TableModelOps::rowCount(table) == 2, "resize crop row count mismatch");
  require(TableModelOps::columnCount(table) == 2, "resize crop column count mismatch");
  require(table.tableAlignments().size() == 2, "resize crop alignment count mismatch");
  require(table.tableAlignments().at(0) == TableAlignment::Left, "resize crop first alignment mismatch");
  require(table.tableAlignments().at(1) == TableAlignment::Center, "resize crop second alignment mismatch");
  QString markdown = serialize(document);
  require(!markdown.contains(QStringLiteral("C")), "resize crop should remove trailing header");
  require(!markdown.contains(QStringLiteral("| 4 | 5 |")), "resize crop should remove trailing row");

  TableModelOps::resize(table, 4, 4);
  require(TableModelOps::rowCount(table) == 4, "resize pad row count mismatch");
  require(TableModelOps::columnCount(table) == 4, "resize pad column count mismatch");
  require(table.tableAlignments().at(2) == TableAlignment::None, "resize pad third alignment mismatch");
  require(table.tableAlignments().at(3) == TableAlignment::None, "resize pad fourth alignment mismatch");
  markdown = serialize(document);
  require(markdown.contains(QStringLiteral("| A | B |  |  |")), "resize pad header mismatch");
  require(markdown.contains(QStringLiteral("|  |  |  |  |")), "resize pad empty row mismatch");
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
  require(session.lastParseWasLocalEdit(), "controller insert column should use local table apply");
  require(session.markdownText().contains(QStringLiteral("| A | B |  |")), "controller insert column mismatch");
  require(undoStack.canUndo(), "table command should push undo");
  EditTransaction insertColumnUndo = undoStack.takeUndo();
  require(insertColumnUndo.isTableCommand(), "table structure command should use TableCommand undo");
  require(insertColumnUndo.tableCommand().beforeTable != nullptr, "table command before snapshot missing");
  require(insertColumnUndo.tableCommand().afterTable != nullptr, "table command after snapshot missing");
  require(TableModelOps::columnCount(*insertColumnUndo.tableCommand().beforeTable) == 2, "table command before column count mismatch");
  require(TableModelOps::columnCount(*insertColumnUndo.tableCommand().afterTable) == 3, "table command after column count mismatch");

  session.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"), false);
  cell = TableModelOps::cellAt(*session.document().root().children().front(), 1, 0);
  hit.blockId = cell->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 0;
  selection.setHitResult(hit);
  require(controller.setCurrentColumnAlignment(TableAlignment::Right), "controller should set alignment");
  require(session.markdownText().contains(QStringLiteral("| ---: | --- |")), "controller alignment mismatch");
  EditTransaction alignmentUndo = undoStack.takeUndo();
  require(alignmentUndo.isSetNodeAttrCommand(), "table alignment should use SetNodeAttrCommand undo");
  require(alignmentUndo.setNodeAttrCommand().attribute == NodeAttribute::TableAlignments, "table alignment attribute mismatch");
  require(std::get<QVector<TableAlignment>>(alignmentUndo.setNodeAttrCommand().beforeValue).at(0) == TableAlignment::None,
          "table alignment before value mismatch");
  require(std::get<QVector<TableAlignment>>(alignmentUndo.setNodeAttrCommand().afterValue).at(0) == TableAlignment::Right,
          "table alignment after value mismatch");

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
  EditTransaction moveRowUndo = undoStack.takeUndo();
  require(moveRowUndo.isTableCommand(), "table row move should use TableCommand undo");

  cell = TableModelOps::cellAt(*session.document().root().children().front(), 1, 1);
  hit.blockId = session.document().root().children().front()->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  selection.setHitResult(hit);
  require(controller.moveCurrentColumnLeft(), "controller should move column left");
  require(session.markdownText().contains(QStringLiteral("| B | A |")), "controller move column mismatch");
  EditTransaction moveColumnUndo = undoStack.takeUndo();
  require(moveColumnUndo.isTableCommand(), "table column move should use TableCommand undo");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| 1 |"), false);
  MarkdownNode& singleColumnTable = *session.document().root().children().front();
  cell = TableModelOps::cellAt(singleColumnTable, 1, 0);
  hit.blockId = singleColumnTable.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 0;
  selection.setHitResult(hit);
  require(!controller.deleteCurrentColumn(), "deleting only column should be a no-op");
  require(!undoStack.canUndo(), "no-op table command should not push undo");
  require(!controller.moveCurrentColumnLeft(), "moving first column left should be a no-op");
  require(!undoStack.canUndo(), "boundary table command should not push undo");
}

void testTableControllerResize() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A | B | C |\n| --- | --- | --- |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |"), false);
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, 2, 2);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 2;
  hit.tableColumn = 2;
  selection.setHitResult(hit);

  require(controller.resizeCurrentTable(2, 2), "controller resize should work");
  require(session.markdownText().contains(QStringLiteral("| A | B |")), "controller resize header mismatch");
  require(!session.markdownText().contains(QStringLiteral("C")), "controller resize should crop column");
  require(!session.markdownText().contains(QStringLiteral("4")), "controller resize should crop row");
  require(undoStack.canUndo(), "controller resize should push undo");
  EditTransaction resizeUndo = undoStack.takeUndo();
  require(resizeUndo.isTableCommand(), "controller resize should use TableCommand undo");
  require(TableModelOps::columnCount(*resizeUndo.tableCommand().afterTable) == 2, "controller resize after column count mismatch");
  require(TableModelOps::rowCount(*resizeUndo.tableCommand().afterTable) == 2, "controller resize after row count mismatch");
}

void testTableControllerDeleteTable() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("before\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nafter"), false);
  MarkdownNode& table = *session.document().root().children().at(1);
  MarkdownNode* cell = TableModelOps::cellAt(table, 1, 0);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 0;
  selection.setHitResult(hit);

  require(controller.deleteCurrentTable(), "controller delete table should work");
  require(session.markdownText().contains(QStringLiteral("before")), "delete table should keep previous text");
  require(session.markdownText().contains(QStringLiteral("after")), "delete table should keep next text");
  require(!session.markdownText().contains(QStringLiteral("| A | B |")), "delete table should remove table markdown");
  require(undoStack.canUndo(), "delete table should push undo");
  EditTransaction deleteUndo = undoStack.takeUndo();
  require(deleteUndo.isRemoveNodeCommand(), "delete table should use RemoveNodeCommand");
  require(deleteUndo.removeNodeCommand().nodeType == BlockType::Table, "delete table command type mismatch");
  require(deleteUndo.removeNodeCommand().removedNode != nullptr, "delete table command removed node missing");
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
  require(undoStack.canUndo(), "table cell insert should push undo");
  EditTransaction cellEditUndo = undoStack.takeUndo();
  require(cellEditUndo.isTextDeltaCommand(), "table cell edit should use TextDeltaCommand");
  require(cellEditUndo.textDeltaCommand().delta.removedText.isEmpty(), "table cell removed text mismatch");
  require(cellEditUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "table cell inserted text mismatch");

  require(tableController.deleteBackward(), "table cell backspace should work");
  require(session.markdownText().contains(QStringLiteral("| 1 | 2 |")), "table cell backspace markdown mismatch");

  require(tableController.deleteBackward(), "table cell second backspace should work");
  require(session.markdownText().contains(QStringLiteral("| 1 |  |")), "table cell delete to empty mismatch");
}

void testTableControllerPreservesInlineCodeOnCellEdit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| vendored `cmark-gfm` |"), false);
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, 1, 0);
  require(cell != nullptr, "inline code cell editing target missing");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 0;
  hit.textOffset = QStringLiteral("vendored").size();
  hit.sourceOffset = cell->sourceRange().byteStart + hit.textOffset;
  selection.setHitResult(hit);

  require(tableController.insertText(QStringLiteral("1")), "table cell rich inline insert should work");
  require(session.markdownText().contains(QStringLiteral("vendored1 `cmark-gfm`")), "table cell inline code should be preserved after insert");
  require(selection.cursorPosition().text.sourceOffset == hit.sourceOffset + 1, "table cell rich inline source cursor mismatch");
  require(undoStack.canUndo(), "table cell rich inline insert should push undo");
  EditTransaction cellEditUndo = undoStack.takeUndo();
  require(cellEditUndo.isTextDeltaCommand(), "table cell rich inline edit should use TextDeltaCommand");
  require(cellEditUndo.textDeltaCommand().delta.removedText.isEmpty(), "table cell rich inline removed text mismatch");
  require(cellEditUndo.textDeltaCommand().delta.insertedText == QStringLiteral("1"), "table cell rich inline inserted text mismatch");
}

void testTableControllerDeletesOnlyEditableInlineContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 2, 1);
  require(tableController.deleteBackward(), "inline code backspace inside content should work");
  require(session.markdownText().contains(QStringLiteral("| `ode` |")), "inline code backspace should preserve markers");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 1, 0);
  require(tableController.deleteBackward(), "inline code start backspace should be handled");
  require(session.markdownText().contains(QStringLiteral("| code` |")), "inline code start backspace removes opening marker");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 5, 4);
  require(tableController.deleteForward(), "inline code end delete should be handled");
  require(session.markdownText().contains(QStringLiteral("| `code |")), "inline code end delete removes closing marker");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 3, 2);
  require(tableController.deleteForward(), "inline code delete inside content should work");
  require(session.markdownText().contains(QStringLiteral("| `coe` |")), "inline code delete should preserve markers");
}

void testTableControllerPreservesTableEscapesOnCellDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a \\| b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a \\|").size(), 3);
  require(tableController.deleteBackward(), "escaped pipe backspace should work");
  require(session.markdownText().contains(QStringLiteral("| a  b |")), "escaped pipe backspace should delete the whole escape");
  require(TableModelOps::columnCount(*session.document().root().children().front()) == 1, "escaped pipe delete should not split table");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a \\| b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a ").size(), 2);
  require(tableController.deleteForward(), "escaped pipe delete should work");
  require(session.markdownText().contains(QStringLiteral("| a  b |")), "escaped pipe delete should delete the whole escape");
  require(TableModelOps::columnCount(*session.document().root().children().front()) == 1, "escaped pipe forward delete should not split table");
}

void testTableControllerPreservesInlineContainersOnCellEdit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| **bold** |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("**bo").size(), 2);
  require(tableController.insertText(QStringLiteral("X")), "bold insert should work");
  require(session.markdownText().contains(QStringLiteral("| **boXld** |")), "bold insert should preserve markers");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| [label](url) |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("[la").size(), 2);
  require(tableController.deleteForward(), "link label delete should work");
  require(session.markdownText().contains(QStringLiteral("| [lael](url) |")), "link label delete should preserve destination");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| [label](url) |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("[label]").size(), 5);
  require(tableController.deleteForward(), "link label end delete should be handled");
  require(session.markdownText().contains(QStringLiteral("| [label]url) |")), "link label end delete removes destination syntax");
}

void testTableControllerDeletesTableBreakAsUnit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a<br>b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a<br>").size(), 2);
  require(tableController.deleteBackward(), "table break backspace should work");
  require(session.markdownText().contains(QStringLiteral("| ab |")), "table break backspace should remove whole break token");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a<br>b |"), false);
  setTableCellCursor(session, selection, 1, 0, 1, 1);
  require(tableController.deleteForward(), "table break delete should work");
  require(session.markdownText().contains(QStringLiteral("| ab |")), "table break delete should remove whole break token");
}

void testTableCellSourceEditMixedTableTokensAndInlineMarkers() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  const QString boldContent = QStringLiteral("a \\| b<br> **bold**");
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(boldContent), false);
  MarkdownNode* boldCell = setTableCellCursor(
      session,
      selection,
      1,
      0,
      boldContent.indexOf(QStringLiteral("**bold")) + 2,
      QStringLiteral("a | b\n ").size());
  require(boldCell != nullptr, "mixed bold cell missing");
  require(tableCellVisibleOffsetForEditCursor(*boldCell, boldContent, boldContent.indexOf(QStringLiteral("**bold")) + 2) ==
              QStringLiteral("a | b\n ").size(),
          "mixed bold marker visible offset should account for table tokens");
  const qsizetype boldCellSourceStart = boldCell->sourceRange().byteStart;
  require(tableController.insertText(QStringLiteral("X")), "mixed bold marker insert should work");
  require(session.markdownText().contains(QStringLiteral("| a \\| b<br> **Xbold** |")), "mixed bold insert markdown mismatch");
  require(selection.cursorPosition().text.sourceOffset ==
              boldCellSourceStart + boldContent.indexOf(QStringLiteral("**bold")) + 3,
          "mixed bold insert source cursor mismatch");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("a | b\n X").size(), "mixed bold insert text cursor mismatch");

  const QString codeContent = QStringLiteral("a \\| b<br> `code`");
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(codeContent), false);
  MarkdownNode* codeCell = setTableCellCursor(
      session,
      selection,
      1,
      0,
      codeContent.indexOf(QStringLiteral("`code")) + 1,
      QStringLiteral("a | b\n ").size());
  require(codeCell != nullptr, "mixed code cell missing");
  require(tableCellVisibleOffsetForEditCursor(*codeCell, codeContent, codeContent.indexOf(QStringLiteral("`code")) + 1) ==
              QStringLiteral("a | b\n ").size(),
          "mixed code marker visible offset should account for table tokens");
  const qsizetype codeCellSourceStart = codeCell->sourceRange().byteStart;
  require(tableController.deleteBackward(), "mixed code marker backspace should be handled");
  require(session.markdownText().contains(QStringLiteral("| a \\| b<br> code` |")), "mixed code marker backspace removes opening marker");
  require(selection.cursorPosition().text.sourceOffset ==
              codeCellSourceStart + codeContent.indexOf(QStringLiteral("`code")),
          "mixed code marker backspace source cursor mismatch");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("a | b\n ").size(), "mixed code marker backspace text cursor mismatch");

  const QString linkContent = QStringLiteral("a \\| b<br> [label](url)");
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(linkContent), false);
  MarkdownNode* linkCell = setTableCellCursor(
      session,
      selection,
      1,
      0,
      linkContent.indexOf(QStringLiteral("](url)")),
      QStringLiteral("a | b\n label").size());
  require(linkCell != nullptr, "mixed link cell missing");
  require(tableCellVisibleOffsetForEditCursor(*linkCell, linkContent, linkContent.indexOf(QStringLiteral("](url)"))) ==
              QStringLiteral("a | b\n label").size(),
          "mixed link hidden syntax visible offset should account for table tokens");
  const qsizetype linkCellSourceStart = linkCell->sourceRange().byteStart;
  require(tableController.deleteForward(), "mixed link hidden syntax delete should be handled");
  require(session.markdownText().contains(QStringLiteral("| a \\| b<br> [label(url) |")), "mixed link hidden syntax delete removes hidden syntax");
  require(selection.cursorPosition().text.sourceOffset ==
              linkCellSourceStart + linkContent.indexOf(QStringLiteral("](url)")),
          "mixed link delete source cursor mismatch");
  require(selection.cursorPosition().text.textOffset == QStringLiteral("a | b\n [label").size(), "mixed link delete text cursor mismatch");
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
  require(session.markdownText().contains(QStringLiteral("|  |  |")), "insert table header mismatch");
  require(session.markdownText().contains(QStringLiteral("| --- | --- |")), "insert table delimiter mismatch");
  require(undoStack.canUndo(), "insert table should push undo");
  EditTransaction insertTableUndo = undoStack.takeUndo();
  require(insertTableUndo.isInsertNodeCommand(), "insert table should use InsertNodeCommand");
  require(insertTableUndo.insertNodeCommand().nodeType == BlockType::Table, "insert table command type mismatch");
  require(insertTableUndo.insertNodeCommand().insertedNode != nullptr, "insert table command node missing");
  require(!insertTableUndo.insertNodeCommand().delta.insertedText.isEmpty(), "insert table command delta missing");
}

void testTableControllerFormatSource() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setDocumentSession(&session);
  tableController.setSelectionController(&selection);
  tableController.setUndoStack(&undoStack);
  tableController.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("| A|B |\n|---|:---:|\n| 1| 2 |"), false);
  MarkdownNode& table = *session.document().root().children().front();
  MarkdownNode* cell = TableModelOps::cellAt(table, 1, 1);
  require(cell != nullptr, "format source target cell missing");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table.id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  selection.setHitResult(hit);

  require(tableController.formatCurrentTableSource(), "format table source should work");
  require(session.markdownText() == QStringLiteral("| A | B |\n| --- | :---: |\n| 1 | 2 |"), "format table source markdown mismatch");
  require(undoStack.canUndo(), "format table source should push undo");
  EditTransaction formatUndo = undoStack.takeUndo();
  require(formatUndo.isTextDeltaCommand(), "format table source should use TextDeltaCommand");
  require(formatUndo.label() == QStringLiteral("Format Table Source"), "format table source undo label mismatch");
  require(selection.cursorPosition().blockId.isValid(), "format table source should keep a table cursor");
}

}  // namespace

int main() {
  testNormalizeAndCellAccess();
  testInsertDeleteRows();
  testInsertDeleteColumnsAndAlignment();
  testMoveRowsAndColumns();
  testResizeCropAndPad();
  testTableControllerCommands();
  testTableControllerResize();
  testTableControllerDeleteTable();
  testTableControllerCellTextEditing();
  testTableControllerPreservesInlineCodeOnCellEdit();
  testTableControllerDeletesOnlyEditableInlineContent();
  testTableControllerPreservesTableEscapesOnCellDelete();
  testTableControllerPreservesInlineContainersOnCellEdit();
  testTableControllerDeletesTableBreakAsUnit();
  testTableCellSourceEditMixedTableTokensAndInlineMarkers();
  testTableControllerInsertTable();
  testTableControllerFormatSource();
  return 0;
}
