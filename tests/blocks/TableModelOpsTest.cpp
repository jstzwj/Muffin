#include "blocks/table/TableModelOps.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
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

}  // namespace

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

int main() {
  testNormalizeAndCellAccess();
  testInsertDeleteRows();
  testInsertDeleteColumnsAndAlignment();
  testMoveRowsAndColumns();
  testResizeCropAndPad();
  return 0;
}
