#pragma once

#include "blocks/table/TableCellSourceEdit.h"
#include "blocks/table/TableModelOps.h"
#include "edit/EditTransaction.h"
#include "editor/CursorPosition.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>
#include <optional>

namespace muffin {

class MarkdownNode;

struct TableLocation {
  NodeId tableId;
  int tableIndex = -1;
  int row = -1;
  int column = -1;

  bool isValid() const {
    return (tableId.isValid() || tableIndex >= 0) && row >= 0 && column >= 0;
  }
};

class TableController final : public QObject {
  Q_OBJECT

public:
  explicit TableController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

  TableLocation currentCell() const;

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();

  bool insertRowBefore();
  bool insertRowAfter();
  bool deleteCurrentRow();
  bool moveCurrentRowUp();
  bool moveCurrentRowDown();

  bool insertColumnBefore();
  bool insertColumnAfter();
  bool deleteCurrentColumn();
  bool moveCurrentColumnLeft();
  bool moveCurrentColumnRight();
  bool setCurrentColumnAlignment(TableAlignment alignment);
  bool resizeCurrentTable(int rows, int columns);
  bool copyCurrentTable() const;
  bool formatCurrentTableSource();
  bool deleteCurrentTable();
  bool insertTable(int rows = 2, int columns = 2);

signals:
  void tableCommandRejected(QString reason);

private:
  bool editCurrentCellSource(
      QString label,
      EditTransaction::Kind kind,
      const std::function<std::optional<TableCellSourceEdit>(const MarkdownNode&, const QString&, qsizetype)>& buildEdit);
  bool mutateCurrentTable(QString label, EditTransaction::Kind kind, const std::function<bool(MarkdownNode&, TableLocation&)>& mutate);
  MarkdownNode* tableForLocation(TableLocation location) const;
  MarkdownNode* cellForLocation(TableLocation location) const;
  CursorPosition cursorForLocation(TableLocation location) const;
  MarkdownNode* findAncestorTable(MarkdownNode& node) const;
  int tableIndexFor(const MarkdownNode& table) const;
  MarkdownNode* tableByIndex(int index) const;

  EditorContext ctx_;
};

}  // namespace muffin
