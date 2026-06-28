#include "blocks/table/TableModelOps.h"
#include "blocks/table/TableController.h"
#include "blocks/table/TableCellSourceEdit.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"
#include "projection/InlineProjection.h"

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

}  // namespace

void testTableControllerCommands() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController controller;
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  require(session.markdownText().toString().contains(QStringLiteral("| A | B |  |")), "controller insert column mismatch");
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
  require(session.markdownText().toString().contains(QStringLiteral("| ---: | --- |")), "controller alignment mismatch");
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
  require(session.markdownText().toString().contains(QStringLiteral("| 3 | 4 |\n| 1 | 2 |")), "controller move row mismatch");
  EditTransaction moveRowUndo = undoStack.takeUndo();
  require(moveRowUndo.isTableCommand(), "table row move should use TableCommand undo");

  cell = TableModelOps::cellAt(*session.document().root().children().front(), 1, 1);
  hit.blockId = session.document().root().children().front()->id();
  hit.textNodeId = cell->id();
  hit.tableRow = 1;
  hit.tableColumn = 1;
  selection.setHitResult(hit);
  require(controller.moveCurrentColumnLeft(), "controller should move column left");
  require(session.markdownText().toString().contains(QStringLiteral("| B | A |")), "controller move column mismatch");
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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  require(session.markdownText().toString().contains(QStringLiteral("| A | B |")), "controller resize header mismatch");
  require(!session.markdownText().toString().contains(QStringLiteral("C")), "controller resize should crop column");
  require(!session.markdownText().toString().contains(QStringLiteral("4")), "controller resize should crop row");
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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  require(session.markdownText().toString().contains(QStringLiteral("before")), "delete table should keep previous text");
  require(session.markdownText().toString().contains(QStringLiteral("after")), "delete table should keep next text");
  require(!session.markdownText().toString().contains(QStringLiteral("| A | B |")), "delete table should remove table markdown");
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
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

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
  require(session.markdownText().toString().contains(QStringLiteral("| 1 | 2X |")), "table cell insert markdown mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "table cell insert cursor mismatch");
  require(undoStack.canUndo(), "table cell insert should push undo");
  EditTransaction cellEditUndo = undoStack.takeUndo();
  require(cellEditUndo.isTextDeltaCommand(), "table cell edit should use TextDeltaCommand");
  require(cellEditUndo.textDeltaCommand().delta.removedText.isEmpty(), "table cell removed text mismatch");
  require(cellEditUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "table cell inserted text mismatch");

  require(tableController.deleteBackward(), "table cell backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("| 1 | 2 |")), "table cell backspace markdown mismatch");

  require(tableController.deleteBackward(), "table cell second backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("| 1 |  |")), "table cell delete to empty mismatch");
}

void testTableControllerPreservesInlineCodeOnCellEdit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

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
  require(session.markdownText().toString().contains(QStringLiteral("vendored1 `cmark-gfm`")), "table cell inline code should be preserved after insert");
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
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 2, 1);
  require(tableController.deleteBackward(), "inline code backspace inside content should work");
  require(session.markdownText().toString().contains(QStringLiteral("| `ode` |")), "inline code backspace should preserve markers");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 1, 0);
  require(tableController.deleteBackward(), "inline code start backspace should be handled");
  require(session.markdownText().toString().contains(QStringLiteral("| code` |")), "inline code start backspace removes opening marker");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 5, 4);
  require(tableController.deleteForward(), "inline code end delete should be handled");
  require(session.markdownText().toString().contains(QStringLiteral("| `code |")), "inline code end delete removes closing marker");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| `code` |"), false);
  setTableCellCursor(session, selection, 1, 0, 3, 2);
  require(tableController.deleteForward(), "inline code delete inside content should work");
  require(session.markdownText().toString().contains(QStringLiteral("| `coe` |")), "inline code delete should preserve markers");
}

void testTableControllerPreservesTableEscapesOnCellDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a \\| b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a \\|").size(), 3);
  require(tableController.deleteBackward(), "escaped pipe backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("| a  b |")), "escaped pipe backspace should delete the whole escape");
  require(TableModelOps::columnCount(*session.document().root().children().front()) == 1, "escaped pipe delete should not split table");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a \\| b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a ").size(), 2);
  require(tableController.deleteForward(), "escaped pipe delete should work");
  require(session.markdownText().toString().contains(QStringLiteral("| a  b |")), "escaped pipe delete should delete the whole escape");
  require(TableModelOps::columnCount(*session.document().root().children().front()) == 1, "escaped pipe forward delete should not split table");
}

void testTableControllerPreservesInlineContainersOnCellEdit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| **bold** |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("**bo").size(), 2);
  require(tableController.insertText(QStringLiteral("X")), "bold insert should work");
  require(session.markdownText().toString().contains(QStringLiteral("| **boXld** |")), "bold insert should preserve markers");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| [label](url) |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("[la").size(), 2);
  require(tableController.deleteForward(), "link label delete should work");
  require(session.markdownText().toString().contains(QStringLiteral("| [lael](url) |")), "link label delete should preserve destination");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| [label](url) |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("[label]").size(), 5);
  require(tableController.deleteForward(), "link label end delete should be handled");
  require(session.markdownText().toString().contains(QStringLiteral("| [label]url) |")), "link label end delete removes destination syntax");
}

void testTableControllerBrDeletesPerChar() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

  // Backspace just past '>' removes only that char, corrupting the tag — NOT the whole tag.
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a<br>b |"), false);
  setTableCellCursor(session, selection, 1, 0, QStringLiteral("a<br>").size(), 2);
  require(tableController.deleteBackward(), "br backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("| a<brb |")),
          "br backspace should remove only '>' (per-char), not the whole tag");
  require(!session.markdownText().toString().contains(QStringLiteral("<br>")),
          "br backspace should corrupt the tag so the break is gone");

  // Forward delete just before '<' removes only that char.
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a<br>b |"), false);
  setTableCellCursor(session, selection, 1, 0, 1, 1);
  require(tableController.deleteForward(), "br forward delete should work");
  require(session.markdownText().toString().contains(QStringLiteral("| abr>b |")),
          "br forward delete should remove only '<' (per-char), not the whole tag");

  // All spellings behave the same (per-char, not atomic).
  const QString variants[] = {QStringLiteral("<br/>"), QStringLiteral("<br />")};
  for (const QString& tag : variants) {
    session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| a%1b |").arg(tag), false);
    setTableCellCursor(session, selection, 1, 0, QStringLiteral("a").size() + tag.size(), 2);
    require(tableController.deleteBackward(), "br variant backspace should work");
    require(!session.markdownText().toString().contains(tag),
            "br variant backspace should corrupt the tag, not delete it whole");
  }
}

// Escaped pipes (\|) inside a table cell survive every structural mutation
// (insert row/column before/after). The escaped pipe must stay escaped in the
// serialized markdown and must never split a cell into an extra column.
void testEscapedPipeSurvivesStructuralEdits() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

  struct Case {
    QString markdown;
    int row;
    int column;
    int expectedColumns;
    QString expectedCellSlice;  // the escaped-pipe cell content after the edit
    bool (TableController::*op)();
  };
  const Case cases[] = {
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 | 3 |"), 1, 0, 3, QStringLiteral("1 \\| 2"), &TableController::insertColumnBefore},
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 | 3 |"), 1, 0, 3, QStringLiteral("1 \\| 2"), &TableController::insertColumnAfter},
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 | 3 |"), 1, 0, 2, QStringLiteral("1 \\| 2"), &TableController::insertRowBefore},
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 | 3 |"), 1, 0, 2, QStringLiteral("1 \\| 2"), &TableController::insertRowAfter},
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 \\| 3 | 4 |"), 1, 0, 3, QStringLiteral("1 \\| 2 \\| 3"), &TableController::insertColumnAfter},
      {QStringLiteral("| A | B | C |\n| --- | --- | --- |\n| 1 | 2 | 3 \\| 4 |"), 1, 2, 4, QStringLiteral("3 \\| 4"), &TableController::insertColumnAfter},
      {QStringLiteral("| A | B |\n| --- | --- |\n| 1 \\| 2 | 3 |"), 1, 1, 2, QStringLiteral("1 \\| 2"), &TableController::insertRowAfter},
  };

  for (const Case& c : cases) {
    session.setMarkdownText(c.markdown, false);
    setTableCellCursor(session, selection, c.row, c.column, 0, 0);
    require((tableController.*(c.op))(), "structural table edit should succeed");

    const QString md = session.markdownText().toString();
    const MarkdownNode& table = *session.document().root().children().front();
    const int columns = TableModelOps::columnCount(table);
    if (columns != c.expectedColumns) {
      std::cerr << "escaped pipe added a column: want " << c.expectedColumns << " got " << columns << "\n" << md.toUtf8().data() << "\n";
    }
    require(columns == c.expectedColumns, "escaped pipe should not add a column");
    const QString expectedCell = QStringLiteral("| %1 |").arg(c.expectedCellSlice);
    if (!md.contains(expectedCell)) {
      std::cerr << "escaped pipe cell content lost. expected [" << expectedCell.toUtf8().data() << "] in:\n" << md.toUtf8().data() << "\n";
    }
    require(md.contains(expectedCell), "escaped pipe cell content should survive structural edit");
  }
}

// A table cell whose source contains an escaped pipe (\|) must render as the
// decoded single pipe, not duplicate its trailing content. cmark-gfm reports an
// inline source range that is one char too short per \|; the parser adapter
// (annotateTableCellInlineSourceRanges) reconciles that range at the source so
// every consumer — including InlineProjection's gap fill — sees the true span.
void testEscapedPipeCellProjection() {
  struct Case {
    QString markdown;
    QString expectedDisplay;
  };
  const Case cases[] = {
      {QStringLiteral("| A |\n| --- |\n| 1 \\| 2 |"), QStringLiteral("1 | 2")},
      {QStringLiteral("| A |\n| --- |\n| a \\| b \\| c |"), QStringLiteral("a | b | c")},
      {QStringLiteral("| A |\n| --- |\n| \\| leading |"), QStringLiteral("| leading")},
      {QStringLiteral("| A |\n| --- |\n| trailing \\||"), QStringLiteral("trailing |")},
  };

  for (const Case& c : cases) {
    DocumentSession session;
    session.setMarkdownText(c.markdown, false);
    const MarkdownNode& table = *session.document().root().children().front();
    const MarkdownNode* cell = TableModelOps::cellAt(table, 1, 0);
    require(cell != nullptr, "escaped pipe projection test cell missing");

    const SourceRange sr = cell->sourceRange();
    const QString md = session.markdownText().toString();
    const QString sourceSlice = md.mid(sr.byteStart, sr.byteEnd - sr.byteStart);

    InlineProjection projection(cell->inlines(), sourceSlice, InlineProjectionState{}, sr.byteStart);
    if (projection.displayText() != c.expectedDisplay) {
      std::cerr << "escaped pipe display mismatch: want [" << c.expectedDisplay.toUtf8().data() << "] got ["
                << projection.displayText().toUtf8().data() << "] source [" << sourceSlice.toUtf8().data() << "]\n";
    }
    require(projection.displayText() == c.expectedDisplay, "escaped pipe cell should render decoded pipe without duplicating content");
    require(projection.visibleText() == c.expectedDisplay, "escaped pipe cell visible text should match display text");
  }
}

int main() {
  testEscapedPipeCellProjection();
  testEscapedPipeSurvivesStructuralEdits();
  testTableControllerCommands();
  testTableControllerResize();
  testTableControllerDeleteTable();
  testTableControllerCellTextEditing();
  testTableControllerPreservesInlineCodeOnCellEdit();
  testTableControllerDeletesOnlyEditableInlineContent();
  testTableControllerPreservesTableEscapesOnCellDelete();
  testTableControllerPreservesInlineContainersOnCellEdit();
  testTableControllerBrDeletesPerChar();
  return 0;
}
