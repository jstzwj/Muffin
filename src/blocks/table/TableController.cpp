#include "blocks/table/TableController.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

#include <QApplication>
#include <QClipboard>

#include <memory>
#include <utility>

namespace muffin {
namespace {

struct TableCellSourceRange {
  qsizetype start = -1;
  qsizetype end = -1;

  bool isValid() const {
    return start >= 0 && end >= start;
  }
};

TableCellSourceRange sourceRangeForTableCellContent(const QString& markdown, const MarkdownNode& cell) {
  if (cell.type() != BlockType::TableCell) {
    return {};
  }

  const SourceRange range = cell.sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return {};
  }
  return {range.byteStart, range.byteEnd};
}

TableLocation clampedTableLocation(TableLocation location, const MarkdownNode& table) {
  const int rows = TableModelOps::rowCount(table);
  const int columns = TableModelOps::columnCount(table);
  if (rows <= 0 || columns <= 0) {
    return {};
  }
  location.row = qBound(0, location.row, rows - 1);
  location.column = qBound(0, location.column, columns - 1);
  return location;
}

int topLevelIndexOf(const MarkdownNode& root, const MarkdownNode& target) {
  const auto& blocks = root.children();
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == &target) {
      return i;
    }
  }
  return -1;
}

CursorPosition cursorForNodeStart(const MarkdownNode* node) {
  CursorPosition cursor;
  if (!node) {
    return cursor;
  }
  cursor.blockId = node->id();
  cursor.text.nodeId = node->id();
  cursor.text.textOffset = 0;
  cursor.text.sourceOffset = node->sourceRange().byteStart;
  return cursor;
}

}  // namespace

TableController::TableController(QObject* parent) : QObject(parent) {}

void TableController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
}

TableLocation TableController::currentCell() const {
  if (!ctx_.session || !ctx_.selection) {
    return {};
  }

  const HitTestResult hit = ctx_.selection->currentHit();
  if (hit.zone == HitTestResult::Zone::TableCell && hit.tableRow >= 0 && hit.tableColumn >= 0) {
    MarkdownNode* node = ctx_.session->document().node(hit.blockId);
    if (!node) {
      return {};
    }

    MarkdownNode* table = findAncestorTable(*node);
    if (!table) {
      return {};
    }

    return {table->id(), tableIndexFor(*table), hit.tableRow, hit.tableColumn};
  }

  if (!ctx_.selection->hasCursor()) {
    return {};
  }

  const CursorPosition cursor = ctx_.selection->cursorPosition();
  MarkdownNode* cell = ctx_.session->document().node(cursor.text.nodeId);
  if (!cell || cell->type() != BlockType::TableCell) {
    return {};
  }
  MarkdownNode* table = findAncestorTable(*cell);
  if (!table) {
    return {};
  }

  for (int row = 0; row < TableModelOps::rowCount(*table); ++row) {
    for (int column = 0; column < TableModelOps::columnCount(*table); ++column) {
      if (TableModelOps::cellAt(*table, row, column) == cell) {
        return {table->id(), tableIndexFor(*table), row, column};
      }
    }
  }
  return {};
}

bool TableController::insertText(QString text) {
  if (text.isEmpty()) {
    return false;
  }

  return editCurrentCellSource(
      QStringLiteral("Edit Table Cell"),
      EditTransaction::Kind::InsertText,
      [text = escapeTableCellInsertedText(std::move(text))](const MarkdownNode& cell, const QString& content, qsizetype sourceOffset) {
        return buildTableCellInsertEdit(cell, content, sourceOffset, text);
      });
}

bool TableController::deleteBackward() {
  return editCurrentCellSource(
      QStringLiteral("Backspace Table Cell"),
      EditTransaction::Kind::DeleteText,
      [](const MarkdownNode& cell, const QString& content, qsizetype sourceOffset) {
        return buildTableCellDeleteBackwardEdit(cell, content, sourceOffset);
      });
}

bool TableController::deleteForward() {
  return editCurrentCellSource(
      QStringLiteral("Delete Table Cell Text"),
      EditTransaction::Kind::DeleteText,
      [](const MarkdownNode& cell, const QString& content, qsizetype sourceOffset) {
        return buildTableCellDeleteForwardEdit(cell, content, sourceOffset);
      });
}

bool TableController::deleteSelection() {
  return false;
}

bool TableController::insertRowBefore() {
  return mutateCurrentTable(QStringLiteral("Insert Table Row Before"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation& location) {
    TableModelOps::insertRow(table, location.row, InsertPosition::Before);
    return true;
  });
}

bool TableController::insertRowAfter() {
  return mutateCurrentTable(QStringLiteral("Insert Table Row After"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation& location) {
    TableModelOps::insertRow(table, location.row, InsertPosition::After);
    ++location.row;
    return true;
  });
}

bool TableController::deleteCurrentRow() {
  return mutateCurrentTable(QStringLiteral("Delete Table Row"), EditTransaction::Kind::DeleteText, [](MarkdownNode& table, TableLocation& location) {
    if (TableModelOps::rowCount(table) <= 1) {
      return false;
    }
    TableModelOps::deleteRow(table, location.row);
    location.row = qMin(location.row, TableModelOps::rowCount(table) - 1);
    return true;
  });
}

bool TableController::moveCurrentRowUp() {
  return mutateCurrentTable(QStringLiteral("Move Table Row Up"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation& location) {
    if (location.row <= 0) {
      return false;
    }
    TableModelOps::moveRow(table, location.row, location.row - 1);
    --location.row;
    return true;
  });
}

bool TableController::moveCurrentRowDown() {
  return mutateCurrentTable(QStringLiteral("Move Table Row Down"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation& location) {
    if (location.row + 1 >= TableModelOps::rowCount(table)) {
      return false;
    }
    TableModelOps::moveRow(table, location.row, location.row + 1);
    ++location.row;
    return true;
  });
}

bool TableController::insertColumnBefore() {
  return mutateCurrentTable(QStringLiteral("Insert Table Column Before"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation& location) {
    TableModelOps::insertColumn(table, location.column, InsertPosition::Before);
    return true;
  });
}

bool TableController::insertColumnAfter() {
  return mutateCurrentTable(QStringLiteral("Insert Table Column After"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation& location) {
    TableModelOps::insertColumn(table, location.column, InsertPosition::After);
    ++location.column;
    return true;
  });
}

bool TableController::deleteCurrentColumn() {
  return mutateCurrentTable(QStringLiteral("Delete Table Column"), EditTransaction::Kind::DeleteText, [](MarkdownNode& table, TableLocation& location) {
    if (TableModelOps::columnCount(table) <= 1) {
      return false;
    }
    TableModelOps::deleteColumn(table, location.column);
    location.column = qMin(location.column, TableModelOps::columnCount(table) - 1);
    return true;
  });
}

bool TableController::moveCurrentColumnLeft() {
  return mutateCurrentTable(QStringLiteral("Move Table Column Left"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation& location) {
    if (location.column <= 0) {
      return false;
    }
    TableModelOps::moveColumn(table, location.column, location.column - 1);
    --location.column;
    return true;
  });
}

bool TableController::moveCurrentColumnRight() {
  return mutateCurrentTable(QStringLiteral("Move Table Column Right"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation& location) {
    if (location.column + 1 >= TableModelOps::columnCount(table)) {
      return false;
    }
    TableModelOps::moveColumn(table, location.column, location.column + 1);
    ++location.column;
    return true;
  });
}

bool TableController::resizeCurrentTable(int rows, int columns) {
  rows = qMax(1, rows);
  columns = qMax(1, columns);
  return mutateCurrentTable(
      QStringLiteral("Resize Table"),
      EditTransaction::Kind::ReplaceDocumentText,
      [rows, columns](MarkdownNode& table, TableLocation& location) {
        TableModelOps::resize(table, rows, columns);
        location.row = qMin(location.row, TableModelOps::rowCount(table) - 1);
        location.column = qMin(location.column, TableModelOps::columnCount(table) - 1);
        return true;
      });
}

bool TableController::copyCurrentTable() const {
  if (!ctx_.session) {
    return false;
  }

  const TableLocation location = currentCell();
  if (!location.isValid()) {
    return false;
  }

  const MarkdownNode* table = tableForLocation(location);
  if (!table) {
    return false;
  }

  const SourceRange range = table->sourceRange();
  const QString& markdown = ctx_.session->markdownText();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return false;
  }

  QApplication::clipboard()->setText(markdown.mid(range.byteStart, range.byteEnd - range.byteStart));
  return true;
}

bool TableController::formatCurrentTableSource() {
  if (!ctx_.session) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    emit tableCommandRejected(QStringLiteral("No table cell is active."));
    return false;
  }

  MarkdownNode* table = tableForLocation(beforeLocation);
  if (!table) {
    return false;
  }

  const SourceRange range = table->sourceRange();
  const QString beforeText = ctx_.session->markdownText();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > beforeText.size()) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString replacementText = serializer.serializeBlock(*table);
  const QString removedText = beforeText.mid(range.byteStart, range.byteEnd - range.byteStart);
  if (removedText == replacementText) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  QVector<LocalEditNodeHint> nodeHints{LocalEditNodeHint{beforeLocation.tableId, range.byteStart, BlockType::Table}};
  if (!ctx_.session->applyTextDelta(range.byteStart, range.byteEnd - range.byteStart, replacementText, true, std::move(nodeHints))) {
    return false;
  }

  TableLocation afterLocation = beforeLocation;
  if (MarkdownNode* reparsedTable = tableForLocation(beforeLocation)) {
    afterLocation.tableId = reparsedTable->id();
    afterLocation.tableIndex = tableIndexFor(*reparsedTable);
  }
  const CursorPosition nextCursor = cursorForLocation(afterLocation);
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Format Table Source"),
        TextDeltaCommand{
            TextDelta{range.byteStart, removedText, replacementText},
            beforeCursor,
            nextCursor,
            QVector<NodeId>{beforeLocation.tableId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(afterLocation.tableId.isValid() ? afterLocation.tableId : beforeLocation.tableId);
  }
  return true;
}

bool TableController::deleteCurrentTable() {
  if (!ctx_.session) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    emit tableCommandRejected(QStringLiteral("No table cell is active."));
    return false;
  }

  MarkdownNode* table = tableForLocation(beforeLocation);
  if (!table || !table->parent() || table->parent()->type() != BlockType::Document) {
    return false;
  }

  const SourceRange tableRange = table->sourceRange();
  qsizetype blockStart = tableRange.byteStart;
  qsizetype blockEnd = tableRange.byteEnd;
  const QString beforeText = ctx_.session->markdownText();
  if (blockStart < 0 || blockEnd < blockStart || blockEnd > beforeText.size()) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  const int nodeIndex = topLevelIndexOf(ctx_.session->document().root(), *table);
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = beforeText.size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, beforeText.size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, beforeText.size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  const NodeId removedNodeId = table->id();
  std::unique_ptr<MarkdownNode> removedNode = table->clone(CloneMode::PreserveIds);
  const QString removedText = beforeText.mid(deleteStart, deleteEnd - deleteStart);

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, BlockType::Table});
  if (!ctx_.session->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  const auto& nextBlocks = ctx_.session->document().root().children();
  const MarkdownNode* nextNode = nullptr;
  if (!nextBlocks.empty()) {
    const int nextIndex = qBound(0, nodeIndex, static_cast<int>(nextBlocks.size()) - 1);
    nextNode = nextBlocks.at(static_cast<size_t>(nextIndex)).get();
  }
  const CursorPosition nextCursor = cursorForNodeStart(nextNode);
  if (ctx_.selection) {
    if (nextCursor.isValid()) {
      ctx_.selection->setCursorPosition(nextCursor);
    } else {
      ctx_.selection->clear();
    }
  }

  if (ctx_.undoStack && beforeCursor.isValid()) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::DeleteText,
        QStringLiteral("Delete Table"),
        RemoveNodeCommand{
            removedNodeId,
            BlockType::Table,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestFullRefresh();
  }
  return true;
}

bool TableController::setCurrentColumnAlignment(TableAlignment alignment) {
  if (!ctx_.session) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    emit tableCommandRejected(QStringLiteral("No table cell is active."));
    return false;
  }

  MarkdownNode* table = tableForLocation(beforeLocation);
  if (!table) {
    return false;
  }
  const int columnCount = TableModelOps::columnCount(*table);
  if (beforeLocation.column < 0 || beforeLocation.column >= columnCount) {
    return false;
  }

  QVector<TableAlignment> beforeAlignments = table->tableAlignments();
  while (beforeAlignments.size() < columnCount) {
    beforeAlignments.push_back(TableAlignment::None);
  }
  QVector<TableAlignment> afterAlignments = beforeAlignments;
  afterAlignments[beforeLocation.column] = alignment;
  if (beforeAlignments == afterAlignments) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  auto afterTable = table->clone(CloneMode::PreserveIds);
  afterTable->setTableAlignments(afterAlignments);
  if (!ctx_.session->applyNodeSnapshot(beforeLocation.tableId, BlockType::Table, beforeLocation.tableIndex, *afterTable, true)) {
    return false;
  }

  TableLocation afterLocation = beforeLocation;
  if (MarkdownNode* reparsedTable = tableForLocation(beforeLocation)) {
    afterLocation.tableId = reparsedTable->id();
    afterLocation.tableIndex = tableIndexFor(*reparsedTable);
  }
  const CursorPosition nextCursor = cursorForLocation(afterLocation);
  if (!nextCursor.isValid()) {
    return false;
  }
  if (ctx_.selection) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Set Table Column Alignment"),
        SetNodeAttrCommand{
            beforeLocation.tableId,
            BlockType::Table,
            beforeLocation.tableIndex,
            NodeAttribute::TableAlignments,
            beforeAlignments,
            afterAlignments,
            beforeCursor,
            nextCursor,
            QVector<NodeId>{beforeLocation.tableId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(afterLocation.tableId);
  }
  return true;
}

bool TableController::insertTable(int rows, int columns) {
  if (!ctx_.session) {
    return false;
  }

  rows = qMax(1, rows);
  columns = qMax(1, columns);

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  const QString beforeText = ctx_.session->markdownText();

  auto table = std::make_unique<MarkdownNode>(BlockType::Table);
  const NodeId insertedTableId = table->id();
  QVector<TableAlignment> alignments;
  for (int column = 0; column < columns; ++column) {
    alignments.push_back(TableAlignment::None);
  }
  table->setTableAlignments(std::move(alignments));
  for (int row = 0; row < rows; ++row) {
    auto rowNode = std::make_unique<MarkdownNode>(BlockType::TableRow);
    rowNode->setTableRowIsHeader(row == 0);
    for (int column = 0; column < columns; ++column) {
      auto cell = std::make_unique<MarkdownNode>(BlockType::TableCell);
      cell->inlines().push_back(InlineNode::text(QString()));
      rowNode->appendChild(std::move(cell));
    }
    table->appendChild(std::move(rowNode));
  }

  MarkdownSerializer serializer;
  const QString tableText = serializer.serializeBlock(*table);
  const MarkdownNode* lastBlock = ctx_.session->document().root().children().empty()
                                      ? nullptr
                                      : ctx_.session->document().root().children().back().get();
  const qsizetype replaceStart = lastBlock && lastBlock->sourceRange().byteStart >= 0
                                     ? lastBlock->sourceRange().byteStart
                                     : beforeText.size();
  if (replaceStart < 0 || replaceStart > beforeText.size()) {
    return false;
  }
  const QString removedText = beforeText.mid(replaceStart);
  const QString prefix = removedText.isEmpty() ? QString() : removedText + QStringLiteral("\n\n");
  const QString insertedText = prefix + tableText;
  const qsizetype tableSourceStart = replaceStart + prefix.size();
  const int insertedNodeIndex = static_cast<int>(ctx_.session->document().root().children().size());
  int insertedTableIndex = 0;
  const auto countTables = [&](const auto& self, const MarkdownNode& node) -> void {
    if (node.type() == BlockType::Table) {
      ++insertedTableIndex;
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  countTables(countTables, ctx_.session->document().root());
  auto commandNode = table->clone(CloneMode::PreserveIds);
  if (!ctx_.session->applyInsertedNode(
          insertedTableId,
          BlockType::Table,
          replaceStart,
          tableSourceStart,
          removedText.size(),
          insertedText,
          true)) {
    return false;
  }

  TableLocation location;
  location.tableId = insertedTableId;
  location.tableIndex = insertedTableIndex;
  location.row = 0;
  location.column = 0;
  const CursorPosition nextCursor = cursorForLocation(location);
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Table"),
        InsertNodeCommand{
            insertedTableId,
            BlockType::Table,
            insertedNodeIndex,
            TextDelta{replaceStart, removedText, insertedText},
            tableSourceStart,
            std::move(commandNode),
            beforeCursor,
            nextCursor,
            QVector<NodeId>{insertedTableId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(insertedTableId);
  }
  return true;
}

bool TableController::editCurrentCellSource(
    QString label,
    EditTransaction::Kind kind,
    const std::function<std::optional<TableCellSourceEdit>(const MarkdownNode&, const QString&, qsizetype)>& buildEdit) {
  if (!ctx_.session || !ctx_.selection) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString beforeText = ctx_.session->markdownText();
  MarkdownNode* table = tableForLocation(beforeLocation);
  if (!table) {
    return false;
  }
  MarkdownNode* cell = TableModelOps::cellAt(*table, beforeLocation.row, beforeLocation.column);
  if (!cell) {
    return false;
  }

  const NodeId tableId = table->id();
  const NodeId cellId = cell->id();
  const TableCellSourceRange contentRange = sourceRangeForTableCellContent(beforeText, *cell);
  if (!contentRange.isValid() || contentRange.end > beforeText.size()) {
    return false;
  }
  const QString content = beforeText.mid(contentRange.start, contentRange.end - contentRange.start);
  const qsizetype localSourceOffset = beforeCursor.text.sourceOffset >= contentRange.start && beforeCursor.text.sourceOffset <= contentRange.end
                                       ? beforeCursor.text.sourceOffset - contentRange.start
                                       : tableCellSourceOffsetForVisibleOffset(content, beforeCursor.text.textOffset);

  std::optional<TableCellSourceEdit> edit = buildEdit(*cell, content, localSourceOffset);
  if (!edit.has_value()) {
    return false;
  }
  edit->replaceStart = qBound<qsizetype>(0, edit->replaceStart, content.size());
  edit->replaceLength = qBound<qsizetype>(0, edit->replaceLength, content.size() - edit->replaceStart);
  edit->nextSourceOffset =
      qBound<qsizetype>(0, edit->nextSourceOffset, content.size() - edit->replaceLength + edit->replacement.size());

  const QString removedText = content.mid(edit->replaceStart, edit->replaceLength);
  if (removedText == edit->replacement) {
    CursorPosition unchangedCursor = cursorForLocation(beforeLocation);
    unchangedCursor.text.sourceOffset = contentRange.start + edit->nextSourceOffset;
    unchangedCursor.text.textOffset = tableCellVisibleOffsetForEditCursor(*cell, content, edit->nextSourceOffset);
    ctx_.selection->setCursorPosition(unchangedCursor);
    return true;
  }

  const qsizetype absoluteReplaceStart = contentRange.start + edit->replaceStart;
  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{tableId, contentRange.start, BlockType::Table});
  if (!ctx_.session->applyTextDelta(absoluteReplaceStart, edit->replaceLength, edit->replacement, true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorForLocation(beforeLocation);
  nextCursor.text.sourceOffset = contentRange.start + edit->nextSourceOffset;
  if (MarkdownNode* nextTable = tableForLocation(beforeLocation)) {
    if (MarkdownNode* nextCell = TableModelOps::cellAt(*nextTable, beforeLocation.row, beforeLocation.column)) {
      const QString nextContent = content.left(edit->replaceStart) + edit->replacement +
                                  content.mid(edit->replaceStart + edit->replaceLength);
      nextCursor.text.nodeId = nextCell->id();
      nextCursor.text.textOffset = tableCellVisibleOffsetForEditCursor(*nextCell, nextContent, edit->nextSourceOffset);
    }
  }
  ctx_.selection->setCursorPosition(nextCursor);

  if (ctx_.undoStack) {
    CursorPosition storedBeforeCursor = beforeCursor;
    storedBeforeCursor.blockId = tableId;
    storedBeforeCursor.text.nodeId = cellId;
    storedBeforeCursor.text.sourceOffset = contentRange.start + localSourceOffset;
    ctx_.undoStack->push(EditTransaction(
        kind,
        std::move(label),
        TextDeltaCommand{
            TextDelta{absoluteReplaceStart, removedText, edit->replacement},
            storedBeforeCursor,
            nextCursor,
            QVector<NodeId>{tableId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(tableId);
  }
  return true;
}

bool TableController::mutateCurrentTable(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, TableLocation&)>& mutate) {
  if (!ctx_.session) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    emit tableCommandRejected(QStringLiteral("No table cell is active."));
    return false;
  }

  MarkdownNode* table = tableForLocation(beforeLocation);
  if (!table) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  std::unique_ptr<MarkdownNode> beforeTable = table->clone(CloneMode::PreserveIds);
  std::unique_ptr<MarkdownNode> afterTable = table->clone(CloneMode::PreserveIds);
  TableLocation afterLocation = beforeLocation;
  if (!mutate(*afterTable, afterLocation)) {
    return false;
  }
  afterLocation = clampedTableLocation(afterLocation, *afterTable);
  if (!afterLocation.isValid()) {
    return false;
  }

  MarkdownSerializer serializer;
  if (serializer.serializeBlock(*beforeTable) == serializer.serializeBlock(*afterTable)) {
    return false;
  }

  if (!ctx_.session->applyTableSnapshot(beforeLocation.tableId, beforeLocation.tableIndex, *afterTable, true)) {
    return false;
  }

  MarkdownNode* reparsedTable = tableForLocation(afterLocation);
  if (reparsedTable) {
    afterLocation.tableId = reparsedTable->id();
    afterLocation.tableIndex = tableIndexFor(*reparsedTable);
  }
  const CursorPosition nextCursor = cursorForLocation(afterLocation);
  if (!nextCursor.isValid()) {
    return false;
  }
  if (ctx_.selection) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        kind,
        std::move(label),
        TableCommand{
            beforeLocation.tableId,
            beforeLocation.tableIndex,
            beforeLocation.row,
            beforeLocation.column,
            afterLocation.row,
            afterLocation.column,
            std::move(beforeTable),
            std::move(afterTable),
            beforeCursor,
            nextCursor}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(reparsedTable ? reparsedTable->id() : beforeLocation.tableId);
  }
  return true;
}

MarkdownNode* TableController::tableForLocation(TableLocation location) const {
  if (!ctx_.session || !location.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = ctx_.session->document().node(location.tableId);
  if (node && node->type() == BlockType::Table) {
    return node;
  }
  return tableByIndex(location.tableIndex);
}

MarkdownNode* TableController::cellForLocation(TableLocation location) const {
  MarkdownNode* table = tableForLocation(location);
  return table ? TableModelOps::cellAt(*table, location.row, location.column) : nullptr;
}

CursorPosition TableController::cursorForLocation(TableLocation location) const {
  CursorPosition cursor;
  MarkdownNode* table = tableForLocation(location);
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = cellForLocation(location);
  if (!cell) {
    return cursor;
  }

  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  cursor.text.textOffset = 0;
  return cursor;
}

MarkdownNode* TableController::findAncestorTable(MarkdownNode& node) const {
  MarkdownNode* current = &node;
  while (current) {
    if (current->type() == BlockType::Table) {
      return current;
    }
    current = current->parent();
  }
  return nullptr;
}

int TableController::tableIndexFor(const MarkdownNode& table) const {
  if (!ctx_.session) {
    return -1;
  }

  int index = 0;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> int {
    if (node.type() == BlockType::Table) {
      if (node.id() == table.id()) {
        return index;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      const int found = self(self, *child);
      if (found >= 0) {
        return found;
      }
    }
    return -1;
  };
  return visit(visit, ctx_.session->document().root());
}

MarkdownNode* TableController::tableByIndex(int targetIndex) const {
  if (!ctx_.session || targetIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::Table) {
      if (index == targetIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, ctx_.session->document().root());
}

}  // namespace muffin
