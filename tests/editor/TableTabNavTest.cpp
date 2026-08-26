#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>

#include <vector>

using namespace muffin;

namespace {

DocumentSession& setupTableDoc(DocumentSession& session, EditorController& controller, EditorView& view,
                               const QString& text) {
  controller.attach(&session, &view);
  view.resize(900, 500);
  session.setMarkdownText(text, false);
  view.setDocument(session.document());
  return session;
}

// Activate the caret at the start of a body cell.
void activateCell(EditorController& controller, EditorView& view, MarkdownNode* table, MarkdownNode* cell,
                  int row, int column) {
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table->id();
  hit.textNodeId = cell->id();
  hit.tableRow = row;
  hit.tableColumn = column;
  hit.textOffset = 0;
  controller.activateHit(hit);
  Q_UNUSED(view);
}

// The n-th cell (row-major, 0-based over ALL rows) — resolves the TableRow children in order.
MarkdownNode* cellOfRow(MarkdownNode* table, int rowFromEnd, int column) {
  std::vector<MarkdownNode*> rows;
  for (const auto& child : table->children()) {
    if (child->type() == BlockType::TableRow) {
      rows.push_back(child.get());
    }
  }
  require(!rows.empty(), "table must have rows");
  require(rowFromEnd >= 0 && rowFromEnd < static_cast<int>(rows.size()), "row index in range");
  MarkdownNode* row = rows.at(static_cast<size_t>(rows.size() - 1 - rowFromEnd));
  require(column >= 0 && column < static_cast<int>(row->children().size()), "target cell must exist");
  return row->children().at(static_cast<size_t>(column)).get();
}

void testTabMovesToNextCell() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  setupTableDoc(session, controller, view,
                QStringLiteral("| a | b |\n| --- | --- |\n| c | d |"));

  MarkdownNode* table = blockAt(session, 0);
  activateCell(controller, view, table, cellOfRow(table, 0, 0), 1, 0);

  require(pressKey(controller.inputController(), &view, Qt::Key_Tab), "tab in a cell should be handled");
  require(controller.selection().cursorPosition().text.nodeId == cellOfRow(table, 0, 1)->id(),
          "tab should move to the next cell in the row");

  require(pressKey(controller.inputController(), &view, Qt::Key_Tab), "second tab should be handled");
  // Wrapping past the row end: the table only has header+one body row, so a new row is appended.
  const QString text = session.markdownText().toString();
  require(text.count(QLatin1Char('\n')) >= 3, "tab at the last cell should append a row");
  require(controller.selection().cursorPosition().text.textOffset == 0,
          "caret should land at the start of the new row's first cell");
}

void testBacktabMovesToPreviousCell() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  setupTableDoc(session, controller, view,
                QStringLiteral("| a | b |\n| --- | --- |\n| c | d |"));

  MarkdownNode* table = blockAt(session, 0);
  activateCell(controller, view, table, cellOfRow(table, 0, 1), 1, 1);

  require(pressKey(controller.inputController(), &view, Qt::Key_Backtab), "backtab in a cell should be handled");
  require(controller.selection().cursorPosition().text.nodeId == cellOfRow(table, 0, 0)->id(),
          "backtab should move to the previous cell in the row");

  // Backtab again wraps to the header row's last cell (row-major order).
  require(pressKey(controller.inputController(), &view, Qt::Key_Backtab), "second backtab should be handled");
  require(controller.selection().cursorPosition().text.nodeId == cellOfRow(table, 1, 1)->id(),
          "backtab at the row start should wrap to the previous row's last cell");
}

void testBacktabAtFirstCellLeavesTable() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  setupTableDoc(session, controller, view,
                QStringLiteral("intro text\n\n| a | b |\n| --- | --- |\n| c | d |"));

  MarkdownNode* table = blockAt(session, 1);
  activateCell(controller, view, table, cellOfRow(table, 1, 0), 0, 0);

  require(pressKey(controller.inputController(), &view, Qt::Key_Backtab), "backtab at (0,0) should be handled");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(),
          "backtab at (0,0) should leave the table for the previous block");
}

void testTabNoLongerInsertsZeroWidthSpaceInCells() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  setupTableDoc(session, controller, view,
                QStringLiteral("| a | b |\n| --- | --- |\n| c | d |"));

  MarkdownNode* table = blockAt(session, 0);
  activateCell(controller, view, table, cellOfRow(table, 0, 0), 1, 0);

  require(pressKey(controller.inputController(), &view, Qt::Key_Tab), "tab should be handled");
  require(!session.markdownText().toString().contains(QChar(0x200b)),
          "tab in a cell must not insert a zero-width space");
  require(session.markdownText().toString().contains(QStringLiteral("| d |")),
          "no accidental text changes in the table");
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTabMovesToNextCell);
  RUN_TEST(testBacktabMovesToPreviousCell);
  RUN_TEST(testBacktabAtFirstCellLeavesTable);
  RUN_TEST(testTabNoLongerInsertsZeroWidthSpaceInCells);
#undef RUN_TEST
  qInfo("All table tab-navigation tests passed.");
  return 0;
}
