#include "blocks/table/TableModelOps.h"

#include "document/InlineNode.h"
#include "document/MarkdownNode.h"

#include <algorithm>

namespace muffin {
namespace {

int boundedIndex(int value, int minValue, int maxValue) {
  return std::clamp(value, minValue, maxValue);
}

}  // namespace

bool TableModelOps::isTable(const MarkdownNode& table) {
  return table.type() == BlockType::Table;
}

void TableModelOps::normalize(MarkdownNode& table, int requestedColumnCount, int requestedRowCount) {
  if (!isTable(table)) {
    return;
  }

  const int rows = qMax(requestedRowCount, rowCount(table));
  while (rowCount(table) < rows) {
    table.appendChild(makeRow(rowCount(table) == 0));
  }

  int columns = requestedColumnCount;
  if (columns < 0) {
    columns = columnCount(table);
  }
  columns = qMax(1, columns);

  QVector<TableAlignment> alignments = table.tableAlignments();
  while (alignments.size() < columns) {
    alignments.push_back(TableAlignment::None);
  }
  while (alignments.size() > columns) {
    alignments.pop_back();
  }
  table.setTableAlignments(std::move(alignments));

  for (auto& row : table.children()) {
    if (row->type() != BlockType::TableRow) {
      row->setType(BlockType::TableRow);
    }
    while (static_cast<int>(row->children().size()) < columns) {
      row->appendChild(makeCell());
    }
    while (static_cast<int>(row->children().size()) > columns) {
      row->detachChild(static_cast<qsizetype>(row->children().size() - 1));
    }
  }
}

int TableModelOps::columnCount(const MarkdownNode& table) {
  if (!isTable(table)) {
    return 0;
  }

  int columns = table.tableAlignments().size();
  for (const auto& row : table.children()) {
    columns = qMax(columns, static_cast<int>(row->children().size()));
  }
  return columns;
}

int TableModelOps::rowCount(const MarkdownNode& table) {
  return isTable(table) ? static_cast<int>(table.children().size()) : 0;
}

void TableModelOps::insertRow(MarkdownNode& table, int row, InsertPosition position) {
  if (!isTable(table)) {
    return;
  }

  normalize(table);
  const int insertAt = boundedIndex(row + (position == InsertPosition::After ? 1 : 0), 0, rowCount(table));
  auto newRow = makeRow(insertAt == 0);
  for (int column = 0; column < columnCount(table); ++column) {
    newRow->appendChild(makeCell());
  }
  table.insertChild(insertAt, std::move(newRow));

  for (int i = 0; i < rowCount(table); ++i) {
    rowAt(table, i).setTableRowIsHeader(i == 0);
  }
}

void TableModelOps::deleteRow(MarkdownNode& table, int row) {
  if (!isTable(table) || rowCount(table) <= 1) {
    return;
  }

  normalize(table);
  const int removeAt = boundedIndex(row, 0, rowCount(table) - 1);
  table.detachChild(removeAt);
  if (rowCount(table) == 0) {
    table.appendChild(makeRow(true));
  }
  rowAt(table, 0).setTableRowIsHeader(true);
}

void TableModelOps::moveRow(MarkdownNode& table, int from, int to) {
  if (!isTable(table) || rowCount(table) <= 1) {
    return;
  }

  normalize(table);
  const int source = boundedIndex(from, 0, rowCount(table) - 1);
  const int target = boundedIndex(to, 0, rowCount(table) - 1);
  if (source == target) {
    return;
  }

  auto row = table.detachChild(source);
  table.insertChild(target, std::move(row));
  for (int i = 0; i < rowCount(table); ++i) {
    rowAt(table, i).setTableRowIsHeader(i == 0);
  }
}

void TableModelOps::insertColumn(MarkdownNode& table, int column, InsertPosition position) {
  if (!isTable(table)) {
    return;
  }

  normalize(table);
  const int insertAt = boundedIndex(column + (position == InsertPosition::After ? 1 : 0), 0, columnCount(table));
  for (auto& row : table.children()) {
    row->insertChild(insertAt, makeCell());
  }

  QVector<TableAlignment> alignments = table.tableAlignments();
  alignments.insert(insertAt, TableAlignment::None);
  table.setTableAlignments(std::move(alignments));
  normalize(table);
}

void TableModelOps::deleteColumn(MarkdownNode& table, int column) {
  if (!isTable(table) || columnCount(table) <= 1) {
    return;
  }

  normalize(table);
  const int removeAt = boundedIndex(column, 0, columnCount(table) - 1);
  for (auto& row : table.children()) {
    row->detachChild(removeAt);
  }

  QVector<TableAlignment> alignments = table.tableAlignments();
  if (removeAt >= 0 && removeAt < alignments.size()) {
    alignments.removeAt(removeAt);
  }
  table.setTableAlignments(std::move(alignments));
  normalize(table);
}

void TableModelOps::moveColumn(MarkdownNode& table, int from, int to) {
  if (!isTable(table) || columnCount(table) <= 1) {
    return;
  }

  normalize(table);
  const int source = boundedIndex(from, 0, columnCount(table) - 1);
  const int target = boundedIndex(to, 0, columnCount(table) - 1);
  if (source == target) {
    return;
  }

  for (auto& row : table.children()) {
    auto cell = row->detachChild(source);
    row->insertChild(target, std::move(cell));
  }

  QVector<TableAlignment> alignments = table.tableAlignments();
  const TableAlignment moved = alignments.at(source);
  alignments.removeAt(source);
  alignments.insert(target, moved);
  table.setTableAlignments(std::move(alignments));
}

void TableModelOps::setAlignment(MarkdownNode& table, int column, TableAlignment alignment) {
  if (!isTable(table)) {
    return;
  }

  normalize(table);
  QVector<TableAlignment> alignments = table.tableAlignments();
  const int target = boundedIndex(column, 0, qMax(0, columnCount(table) - 1));
  if (target >= alignments.size()) {
    alignments.resize(target + 1);
  }
  alignments[target] = alignment;
  table.setTableAlignments(std::move(alignments));
}

void TableModelOps::resize(MarkdownNode& table, int rows, int columns) {
  if (!isTable(table)) {
    return;
  }

  rows = qMax(1, rows);
  columns = qMax(1, columns);
  normalize(table, columns, rows);
  while (rowCount(table) > rows) {
    table.detachChild(rowCount(table) - 1);
  }
  for (int row = 0; row < rowCount(table); ++row) {
    rowAt(table, row).setTableRowIsHeader(row == 0);
  }
}

MarkdownNode* TableModelOps::cellAt(MarkdownNode& table, int row, int column) {
  if (!isTable(table)) {
    return nullptr;
  }
  normalize(table);
  if (row < 0 || row >= rowCount(table) || column < 0 || column >= columnCount(table)) {
    return nullptr;
  }
  return rowAt(table, row).children().at(static_cast<size_t>(column)).get();
}

const MarkdownNode* TableModelOps::cellAt(const MarkdownNode& table, int row, int column) {
  if (!isTable(table) || row < 0 || row >= rowCount(table) || column < 0 || column >= columnCount(table)) {
    return nullptr;
  }
  return rowAt(table, row).children().at(static_cast<size_t>(column)).get();
}

MarkdownNode& TableModelOps::rowAt(MarkdownNode& table, int row) {
  return *table.children().at(static_cast<size_t>(row));
}

const MarkdownNode& TableModelOps::rowAt(const MarkdownNode& table, int row) {
  return *table.children().at(static_cast<size_t>(row));
}

std::unique_ptr<MarkdownNode> TableModelOps::makeRow(bool header) {
  auto row = std::make_unique<MarkdownNode>(BlockType::TableRow);
  row->setTableRowIsHeader(header);
  return row;
}

std::unique_ptr<MarkdownNode> TableModelOps::makeCell() {
  auto cell = std::make_unique<MarkdownNode>(BlockType::TableCell);
  cell->inlines().push_back(InlineNode::text(QString()));
  return cell;
}

}  // namespace muffin
