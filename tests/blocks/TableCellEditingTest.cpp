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

void testTableCellSourceEditMixedTableTokensAndInlineMarkers() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  TableController tableController;
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

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
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

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
  tableController.setContext({&session, &selection, &undoStack, &brushQueue});

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

int main() {
  testTableCellSourceEditMixedTableTokensAndInlineMarkers();
  testTableControllerInsertTable();
  testTableControllerFormatSource();
  return 0;
}
