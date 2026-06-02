#include "blocks/table/TableController.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

#include <memory>

namespace muffin {
namespace {

struct TableCellSourceRange {
  qsizetype start = -1;
  qsizetype end = -1;

  bool isValid() const {
    return start >= 0 && end >= start;
  }
};

bool isHorizontalPadding(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) {
  if (line <= 0 || column <= 0) {
    return -1;
  }
  qsizetype lineStart = 0;
  for (int currentLine = 1; currentLine < line; ++currentLine) {
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
    if (newline < 0) {
      return -1;
    }
    lineStart = newline + 1;
  }
  const qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
  const qsizetype boundedLineEnd = lineEnd < 0 ? text.size() : lineEnd;
  return qBound<qsizetype>(lineStart, lineStart + column - 1, boundedLineEnd);
}

qsizetype sourceOffsetForLineEnd(const QString& text, int line) {
  if (line <= 0) {
    return -1;
  }
  const qsizetype lineStart = sourceOffsetForLineColumn(text, line, 1);
  if (lineStart < 0) {
    return -1;
  }
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
  return newline < 0 ? text.size() : newline;
}

TableCellSourceRange sourceRangeForTableCellContent(const QString& markdown, const MarkdownNode& cell) {
  if (cell.type() != BlockType::TableCell) {
    return {};
  }

  const SourceRange range = cell.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart || range.columnStart <= 0) {
    return {};
  }

  const qsizetype lineEnd = sourceOffsetForLineEnd(markdown, range.lineStart);
  qsizetype fieldStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  qsizetype fieldEnd = range.columnEnd >= range.columnStart
                           ? sourceOffsetForLineColumn(markdown, range.lineStart, range.columnEnd + 1)
                           : lineEnd;
  if (fieldStart < 0 || fieldEnd < fieldStart || lineEnd < fieldEnd) {
    return {};
  }

  qsizetype contentStart = fieldStart;
  qsizetype contentEnd = fieldEnd;
  while (contentStart < contentEnd && isHorizontalPadding(markdown.at(contentStart))) {
    ++contentStart;
  }
  while (contentEnd > contentStart && isHorizontalPadding(markdown.at(contentEnd - 1))) {
    --contentEnd;
  }
  if (contentStart == fieldEnd && fieldEnd > fieldStart) {
    contentStart = fieldStart + (fieldEnd - fieldStart) / 2;
    contentEnd = contentStart;
  }
  return {contentStart, contentEnd};
}

qsizetype sourceOffsetForTableCellVisibleOffset(const QString& escapedMarkdown, qsizetype visibleOffset) {
  visibleOffset = qMax<qsizetype>(0, visibleOffset);
  qsizetype visible = 0;
  qsizetype source = 0;
  while (source < escapedMarkdown.size()) {
    if (visible >= visibleOffset) {
      return source;
    }
    if (escapedMarkdown.at(source) == QLatin1Char('\\') && source + 1 < escapedMarkdown.size() &&
        escapedMarkdown.at(source + 1) == QLatin1Char('|')) {
      source += 2;
      ++visible;
      continue;
    }
    if (escapedMarkdown.mid(source, 4) == QStringLiteral("<br>")) {
      source += 4;
      ++visible;
      continue;
    }
    ++source;
    ++visible;
  }
  return escapedMarkdown.size();
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

}  // namespace

TableController::TableController(QObject* parent) : QObject(parent) {}

void TableController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void TableController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void TableController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void TableController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

TableLocation TableController::currentCell() const {
  if (!session_ || !selection_) {
    return {};
  }

  const HitTestResult hit = selection_->currentHit();
  if (hit.zone == HitTestResult::Zone::TableCell && hit.tableRow >= 0 && hit.tableColumn >= 0) {
    MarkdownNode* node = session_->document().node(hit.blockId);
    if (!node) {
      return {};
    }

    MarkdownNode* table = findAncestorTable(*node);
    if (!table) {
      return {};
    }

    return {table->id(), tableIndexFor(*table), hit.tableRow, hit.tableColumn};
  }

  if (!selection_->hasCursor()) {
    return {};
  }

  const CursorPosition cursor = selection_->cursorPosition();
  MarkdownNode* cell = session_->document().node(cursor.text.nodeId);
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

  return editCurrentCell(QStringLiteral("Edit Table Cell"), EditTransaction::Kind::InsertText, [text = std::move(text)](MarkdownNode& cell, qsizetype& offset) {
    QString value = cell.inlines().isEmpty() ? QString() : cell.inlines().front().text();
    offset = qBound<qsizetype>(0, offset, value.size());
    value.insert(offset, text);
    offset += text.size();
    cell.inlines().clear();
    cell.inlines().push_back(InlineNode::text(value));
    return true;
  });
}

bool TableController::deleteBackward() {
  return editCurrentCell(QStringLiteral("Backspace Table Cell"), EditTransaction::Kind::DeleteText, [](MarkdownNode& cell, qsizetype& offset) {
    QString value = cell.inlines().isEmpty() ? QString() : cell.inlines().front().text();
    offset = qBound<qsizetype>(0, offset, value.size());
    if (offset <= 0) {
      return true;
    }
    value.remove(offset - 1, 1);
    --offset;
    cell.inlines().clear();
    cell.inlines().push_back(InlineNode::text(value));
    return true;
  });
}

bool TableController::deleteForward() {
  return editCurrentCell(QStringLiteral("Delete Table Cell Text"), EditTransaction::Kind::DeleteText, [](MarkdownNode& cell, qsizetype& offset) {
    QString value = cell.inlines().isEmpty() ? QString() : cell.inlines().front().text();
    offset = qBound<qsizetype>(0, offset, value.size());
    if (offset >= value.size()) {
      return true;
    }
    value.remove(offset, 1);
    cell.inlines().clear();
    cell.inlines().push_back(InlineNode::text(value));
    return true;
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

bool TableController::setCurrentColumnAlignment(TableAlignment alignment) {
  if (!session_) {
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

  const CursorPosition beforeCursor = selection_ ? selection_->cursorPosition() : CursorPosition();
  auto afterTable = table->clone(CloneMode::PreserveIds);
  afterTable->setTableAlignments(afterAlignments);
  if (!session_->applyNodeSnapshot(beforeLocation.tableId, BlockType::Table, beforeLocation.tableIndex, *afterTable, true)) {
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
  if (selection_) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(
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
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(afterLocation.tableId);
  }
  return true;
}

bool TableController::insertTable(int rows, int columns) {
  if (!session_) {
    return false;
  }

  rows = qMax(1, rows);
  columns = qMax(1, columns);

  const CursorPosition beforeCursor = selection_ ? selection_->cursorPosition() : CursorPosition();
  const QString beforeText = session_->markdownText();

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
      cell->inlines().push_back(InlineNode::text(row == 0 ? QStringLiteral("Header") : QString()));
      rowNode->appendChild(std::move(cell));
    }
    table->appendChild(std::move(rowNode));
  }

  MarkdownSerializer serializer;
  const QString tableText = serializer.serializeBlock(*table);
  const MarkdownNode* lastBlock = session_->document().root().children().empty()
                                      ? nullptr
                                      : session_->document().root().children().back().get();
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
  const int insertedNodeIndex = static_cast<int>(session_->document().root().children().size());
  int insertedTableIndex = 0;
  const auto countTables = [&](const auto& self, const MarkdownNode& node) -> void {
    if (node.type() == BlockType::Table) {
      ++insertedTableIndex;
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  countTables(countTables, session_->document().root());
  auto commandNode = table->clone(CloneMode::PreserveIds);
  if (!session_->applyInsertedNode(
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
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(
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
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(insertedTableId);
  }
  return true;
}

bool TableController::editCurrentCell(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, qsizetype&)>& mutate) {
  if (!session_ || !selection_) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    return false;
  }

  const CursorPosition beforeCursor = selection_->cursorPosition();
  const QString beforeText = session_->markdownText();

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
  const QString removedText = beforeText.mid(contentRange.start, contentRange.end - contentRange.start);
  auto nextCell = cell->clone(CloneMode::PreserveIds);
  qsizetype nextOffset = beforeCursor.text.textOffset;
  if (!mutate(*nextCell, nextOffset)) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString insertedText = serializer.serializeTableCellContent(*nextCell);
  if (removedText == insertedText) {
    CursorPosition unchangedCursor = cursorForLocation(beforeLocation);
    unchangedCursor.text.textOffset = nextOffset;
    unchangedCursor.text.sourceOffset =
        contentRange.start + sourceOffsetForTableCellVisibleOffset(removedText, nextOffset);
    selection_->setCursorPosition(unchangedCursor);
    return true;
  }

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{tableId, contentRange.start, BlockType::Table});
  if (!session_->applyTextDelta(contentRange.start, contentRange.end - contentRange.start, insertedText, true, std::move(nodeHints))) {
    return false;
  }
  CursorPosition nextCursor = cursorForLocation(beforeLocation);
  nextCursor.text.textOffset = nextOffset;
  nextCursor.text.sourceOffset = contentRange.start + sourceOffsetForTableCellVisibleOffset(insertedText, nextOffset);
  selection_->setCursorPosition(nextCursor);
  if (undoStack_) {
    CursorPosition storedBeforeCursor = beforeCursor;
    storedBeforeCursor.blockId = tableId;
    storedBeforeCursor.text.nodeId = cellId;
    storedBeforeCursor.text.sourceOffset =
        contentRange.start + sourceOffsetForTableCellVisibleOffset(removedText, beforeCursor.text.textOffset);
    undoStack_->push(EditTransaction(
        kind,
        std::move(label),
        TextDeltaCommand{
            TextDelta{contentRange.start, removedText, insertedText},
            storedBeforeCursor,
            nextCursor,
            QVector<NodeId>{tableId}}));
  }
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(tableId);
  }
  return true;
}

bool TableController::mutateCurrentTable(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, TableLocation&)>& mutate) {
  if (!session_) {
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

  const CursorPosition beforeCursor = selection_ ? selection_->cursorPosition() : CursorPosition();
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

  if (!session_->applyTableSnapshot(beforeLocation.tableId, beforeLocation.tableIndex, *afterTable, true)) {
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
  if (selection_) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(
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
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(reparsedTable ? reparsedTable->id() : beforeLocation.tableId);
  }
  return true;
}

MarkdownNode* TableController::tableForLocation(TableLocation location) const {
  if (!session_ || !location.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = session_->document().node(location.tableId);
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
  if (!session_) {
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
  return visit(visit, session_->document().root());
}

MarkdownNode* TableController::tableByIndex(int targetIndex) const {
  if (!session_ || targetIndex < 0) {
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
  return visit(visit, session_->document().root());
}

}  // namespace muffin
