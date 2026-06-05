#pragma once

#include "document/MarkdownTypes.h"

#include <memory>

namespace muffin {

class MarkdownNode;

enum class InsertPosition {
  Before,
  After
};

class TableModelOps {
public:
  static bool isTable(const MarkdownNode& table);
  static void normalize(MarkdownNode& table, int columnCount = -1, int rowCount = 1);
  static int columnCount(const MarkdownNode& table);
  static int rowCount(const MarkdownNode& table);

  static void insertRow(MarkdownNode& table, int row, InsertPosition position);
  static void deleteRow(MarkdownNode& table, int row);
  static void moveRow(MarkdownNode& table, int from, int to);

  static void insertColumn(MarkdownNode& table, int column, InsertPosition position);
  static void deleteColumn(MarkdownNode& table, int column);
  static void moveColumn(MarkdownNode& table, int from, int to);

  static void setAlignment(MarkdownNode& table, int column, TableAlignment alignment);
  static void resize(MarkdownNode& table, int rows, int columns);
  static MarkdownNode* cellAt(MarkdownNode& table, int row, int column);
  static const MarkdownNode* cellAt(const MarkdownNode& table, int row, int column);

private:
  static MarkdownNode& rowAt(MarkdownNode& table, int row);
  static const MarkdownNode& rowAt(const MarkdownNode& table, int row);
  static std::unique_ptr<MarkdownNode> makeRow(bool header = false);
  static std::unique_ptr<MarkdownNode> makeCell();
};

}  // namespace muffin
