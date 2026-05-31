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
  return mutateCurrentTable(QStringLiteral("Insert Table Row Before"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::insertRow(table, location.row, InsertPosition::Before);
    return true;
  });
}

bool TableController::insertRowAfter() {
  return mutateCurrentTable(QStringLiteral("Insert Table Row After"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::insertRow(table, location.row, InsertPosition::After);
    return true;
  });
}

bool TableController::deleteCurrentRow() {
  return mutateCurrentTable(QStringLiteral("Delete Table Row"), EditTransaction::Kind::DeleteText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::deleteRow(table, location.row);
    return true;
  });
}

bool TableController::moveCurrentRowUp() {
  return mutateCurrentTable(QStringLiteral("Move Table Row Up"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation location) {
    if (location.row <= 0) {
      return false;
    }
    TableModelOps::moveRow(table, location.row, location.row - 1);
    return true;
  });
}

bool TableController::moveCurrentRowDown() {
  return mutateCurrentTable(QStringLiteral("Move Table Row Down"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation location) {
    if (location.row + 1 >= TableModelOps::rowCount(table)) {
      return false;
    }
    TableModelOps::moveRow(table, location.row, location.row + 1);
    return true;
  });
}

bool TableController::insertColumnBefore() {
  return mutateCurrentTable(QStringLiteral("Insert Table Column Before"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::insertColumn(table, location.column, InsertPosition::Before);
    return true;
  });
}

bool TableController::insertColumnAfter() {
  return mutateCurrentTable(QStringLiteral("Insert Table Column After"), EditTransaction::Kind::InsertText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::insertColumn(table, location.column, InsertPosition::After);
    return true;
  });
}

bool TableController::deleteCurrentColumn() {
  return mutateCurrentTable(QStringLiteral("Delete Table Column"), EditTransaction::Kind::DeleteText, [](MarkdownNode& table, TableLocation location) {
    TableModelOps::deleteColumn(table, location.column);
    return true;
  });
}

bool TableController::moveCurrentColumnLeft() {
  return mutateCurrentTable(QStringLiteral("Move Table Column Left"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation location) {
    if (location.column <= 0) {
      return false;
    }
    TableModelOps::moveColumn(table, location.column, location.column - 1);
    return true;
  });
}

bool TableController::moveCurrentColumnRight() {
  return mutateCurrentTable(QStringLiteral("Move Table Column Right"), EditTransaction::Kind::ReplaceDocumentText, [](MarkdownNode& table, TableLocation location) {
    if (location.column + 1 >= TableModelOps::columnCount(table)) {
      return false;
    }
    TableModelOps::moveColumn(table, location.column, location.column + 1);
    return true;
  });
}

bool TableController::setCurrentColumnAlignment(TableAlignment alignment) {
  return mutateCurrentTable(QStringLiteral("Set Table Column Alignment"), EditTransaction::Kind::ReplaceDocumentText,
                            [alignment](MarkdownNode& table, TableLocation location) {
                              TableModelOps::setAlignment(table, location.column, alignment);
                              return true;
                            });
}

bool TableController::insertTable(int rows, int columns) {
  if (!session_) {
    return false;
  }

  rows = qMax(1, rows);
  columns = qMax(1, columns);

  const CursorPosition beforeCursor = selection_ ? selection_->cursorPosition() : CursorPosition();
  const QString beforeText = session_->markdownText();
  auto rootCopy = session_->document().root().clone(CloneMode::PreserveIds);

  auto table = std::make_unique<MarkdownNode>(BlockType::Table);
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
  rootCopy->appendChild(std::move(table));

  MarkdownDocument nextDocument;
  nextDocument.setMarkdownText(beforeText, std::move(rootCopy));
  MarkdownSerializer serializer;
  QString nextText = serializer.serializeDocument(nextDocument);
  session_->applyMarkdownText(nextText, true);

  TableLocation location;
  location.tableIndex = 0;
  int index = 0;
  const auto countTables = [&](const auto& self, const MarkdownNode& node) -> void {
    if (node.type() == BlockType::Table) {
      location.tableIndex = index;
      ++index;
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  countTables(countTables, session_->document().root());
  location.row = 0;
  location.column = 0;
  const CursorPosition nextCursor = cursorForLocation(location);
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(EditTransaction::Kind::InsertText, QStringLiteral("Insert Table"), {beforeText, beforeCursor},
                                     {nextText, nextCursor}));
  }
  if (brushQueue_) {
    brushQueue_->requestFullRefresh();
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
  auto rootCopy = session_->document().root().clone(CloneMode::PreserveIds);

  const auto findById = [&](const auto& self, MarkdownNode& node, NodeId id) -> MarkdownNode* {
    if (node.id() == id) {
      return &node;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child, id)) {
        return found;
      }
    }
    return nullptr;
  };

  MarkdownNode* table = findById(findById, *rootCopy, beforeLocation.tableId);
  if (!table) {
    return false;
  }
  MarkdownNode* cell = TableModelOps::cellAt(*table, beforeLocation.row, beforeLocation.column);
  if (!cell) {
    return false;
  }

  qsizetype nextOffset = beforeCursor.text.textOffset;
  if (!mutate(*cell, nextOffset)) {
    return false;
  }

  MarkdownDocument nextDocument;
  nextDocument.setMarkdownText(beforeText, std::move(rootCopy));
  MarkdownSerializer serializer;
  QString nextText = serializer.serializeDocument(nextDocument);

  session_->applyMarkdownText(nextText, true);
  CursorPosition nextCursor = cursorForLocation(beforeLocation);
  nextCursor.text.textOffset = nextOffset;
  selection_->setCursorPosition(nextCursor);
  if (undoStack_) {
    undoStack_->push(EditTransaction(kind, std::move(label), {beforeText, beforeCursor}, {nextText, nextCursor}));
  }
  if (brushQueue_) {
    brushQueue_->requestFullRefresh();
  }
  return true;
}

bool TableController::mutateCurrentTable(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, TableLocation)>& mutate) {
  if (!session_) {
    return false;
  }

  const TableLocation beforeLocation = currentCell();
  if (!beforeLocation.isValid()) {
    emit tableCommandRejected(QStringLiteral("No table cell is active."));
    return false;
  }

  const CursorPosition beforeCursor = selection_ ? selection_->cursorPosition() : CursorPosition();
  const QString beforeText = session_->markdownText();
  auto rootCopy = session_->document().root().clone(CloneMode::PreserveIds);

  MarkdownNode* targetTable = nullptr;
  const auto findById = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.id() == beforeLocation.tableId) {
      return &node;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  targetTable = findById(findById, *rootCopy);
  if (!targetTable || !mutate(*targetTable, beforeLocation)) {
    return false;
  }

  MarkdownDocument nextDocument;
  nextDocument.setMarkdownText(beforeText, std::move(rootCopy));
  MarkdownSerializer serializer;
  QString nextText = serializer.serializeDocument(nextDocument);

  session_->applyMarkdownText(nextText, true);
  const CursorPosition nextCursor = cursorForLocation(beforeLocation);
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(kind, std::move(label), {beforeText, beforeCursor}, {nextText, nextCursor}));
  }
  if (brushQueue_) {
    brushQueue_->requestFullRefresh();
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
