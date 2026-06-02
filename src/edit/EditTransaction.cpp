#include "edit/EditTransaction.h"

#include <utility>

namespace muffin {

bool TextDelta::isValid() const {
  return start >= 0 && (!removedText.isEmpty() || !insertedText.isEmpty()) && removedText != insertedText;
}

bool TextDeltaCommand::isValid() const {
  return delta.isValid() && beforeCursor.isValid() && afterCursor.isValid();
}

TableCommand::TableCommand(
    NodeId tableId,
    int tableIndex,
    int cursorRow,
    int cursorColumn,
    std::unique_ptr<MarkdownNode> beforeTable,
    std::unique_ptr<MarkdownNode> afterTable,
    CursorPosition beforeCursor,
    CursorPosition afterCursor)
    : tableId(std::move(tableId)),
      tableIndex(tableIndex),
      cursorRow(cursorRow),
      cursorColumn(cursorColumn),
      beforeTable(std::move(beforeTable)),
      afterTable(std::move(afterTable)),
      beforeCursor(std::move(beforeCursor)),
      afterCursor(std::move(afterCursor)) {}

TableCommand::TableCommand(const TableCommand& other)
    : tableId(other.tableId),
      tableIndex(other.tableIndex),
      cursorRow(other.cursorRow),
      cursorColumn(other.cursorColumn),
      beforeTable(other.beforeTable ? other.beforeTable->clone(CloneMode::PreserveIds) : nullptr),
      afterTable(other.afterTable ? other.afterTable->clone(CloneMode::PreserveIds) : nullptr),
      beforeCursor(other.beforeCursor),
      afterCursor(other.afterCursor) {}

TableCommand& TableCommand::operator=(const TableCommand& other) {
  if (this == &other) {
    return *this;
  }
  tableId = other.tableId;
  tableIndex = other.tableIndex;
  cursorRow = other.cursorRow;
  cursorColumn = other.cursorColumn;
  beforeTable = other.beforeTable ? other.beforeTable->clone(CloneMode::PreserveIds) : nullptr;
  afterTable = other.afterTable ? other.afterTable->clone(CloneMode::PreserveIds) : nullptr;
  beforeCursor = other.beforeCursor;
  afterCursor = other.afterCursor;
  return *this;
}

bool TableCommand::isValid() const {
  return (tableId.isValid() || tableIndex >= 0) && beforeTable && afterTable && beforeTable->type() == BlockType::Table &&
         afterTable->type() == BlockType::Table && beforeCursor.isValid() && afterCursor.isValid();
}

InsertNodeCommand::InsertNodeCommand(
    NodeId nodeId,
    BlockType nodeType,
    int nodeIndex,
    TextDelta delta,
    qsizetype nodeSourceStart,
    std::unique_ptr<MarkdownNode> insertedNode,
    CursorPosition beforeCursor,
    CursorPosition afterCursor,
    QVector<NodeId> affectedNodes)
    : nodeId(std::move(nodeId)),
      nodeType(nodeType),
      nodeIndex(nodeIndex),
      delta(std::move(delta)),
      nodeSourceStart(nodeSourceStart),
      insertedNode(std::move(insertedNode)),
      beforeCursor(std::move(beforeCursor)),
      afterCursor(std::move(afterCursor)),
      affectedNodes(std::move(affectedNodes)) {}

InsertNodeCommand::InsertNodeCommand(const InsertNodeCommand& other)
    : nodeId(other.nodeId),
      nodeType(other.nodeType),
      nodeIndex(other.nodeIndex),
      delta(other.delta),
      nodeSourceStart(other.nodeSourceStart),
      insertedNode(other.insertedNode ? other.insertedNode->clone(CloneMode::PreserveIds) : nullptr),
      beforeCursor(other.beforeCursor),
      afterCursor(other.afterCursor),
      affectedNodes(other.affectedNodes) {}

InsertNodeCommand& InsertNodeCommand::operator=(const InsertNodeCommand& other) {
  if (this == &other) {
    return *this;
  }
  nodeId = other.nodeId;
  nodeType = other.nodeType;
  nodeIndex = other.nodeIndex;
  delta = other.delta;
  nodeSourceStart = other.nodeSourceStart;
  insertedNode = other.insertedNode ? other.insertedNode->clone(CloneMode::PreserveIds) : nullptr;
  beforeCursor = other.beforeCursor;
  afterCursor = other.afterCursor;
  affectedNodes = other.affectedNodes;
  return *this;
}

bool InsertNodeCommand::isValid() const {
  return nodeId.isValid() && nodeType != BlockType::Unknown && nodeIndex >= 0 && delta.isValid() && nodeSourceStart >= 0 &&
         insertedNode && insertedNode->type() == nodeType && afterCursor.isValid();
}

ReplaceNodeCommand::ReplaceNodeCommand(
    NodeId nodeId,
    BlockType nodeType,
    int nodeIndex,
    std::unique_ptr<MarkdownNode> beforeNode,
    std::unique_ptr<MarkdownNode> afterNode,
    CursorPosition beforeCursor,
    CursorPosition afterCursor,
    QVector<NodeId> affectedNodes)
    : nodeId(std::move(nodeId)),
      nodeType(nodeType),
      nodeIndex(nodeIndex),
      beforeNode(std::move(beforeNode)),
      afterNode(std::move(afterNode)),
      beforeCursor(std::move(beforeCursor)),
      afterCursor(std::move(afterCursor)),
      affectedNodes(std::move(affectedNodes)) {}

ReplaceNodeCommand::ReplaceNodeCommand(const ReplaceNodeCommand& other)
    : nodeId(other.nodeId),
      nodeType(other.nodeType),
      nodeIndex(other.nodeIndex),
      beforeNode(other.beforeNode ? other.beforeNode->clone(CloneMode::PreserveIds) : nullptr),
      afterNode(other.afterNode ? other.afterNode->clone(CloneMode::PreserveIds) : nullptr),
      beforeCursor(other.beforeCursor),
      afterCursor(other.afterCursor),
      affectedNodes(other.affectedNodes) {}

ReplaceNodeCommand& ReplaceNodeCommand::operator=(const ReplaceNodeCommand& other) {
  if (this == &other) {
    return *this;
  }
  nodeId = other.nodeId;
  nodeType = other.nodeType;
  nodeIndex = other.nodeIndex;
  beforeNode = other.beforeNode ? other.beforeNode->clone(CloneMode::PreserveIds) : nullptr;
  afterNode = other.afterNode ? other.afterNode->clone(CloneMode::PreserveIds) : nullptr;
  beforeCursor = other.beforeCursor;
  afterCursor = other.afterCursor;
  affectedNodes = other.affectedNodes;
  return *this;
}

bool ReplaceNodeCommand::isValid() const {
  return (nodeId.isValid() || nodeIndex >= 0) && nodeType != BlockType::Unknown && beforeNode && afterNode &&
         beforeNode->type() == nodeType && afterNode->type() == nodeType && beforeCursor.isValid() && afterCursor.isValid();
}

EditTransaction::EditTransaction(const EditTransaction& other)
    : storage_(other.storage_),
      kind_(other.kind_),
      label_(other.label_),
      before_(other.before_),
      after_(other.after_),
      textDeltaCommand_(other.textDeltaCommand_),
      tableCommand_(other.tableCommand_),
      insertNodeCommand_(other.insertNodeCommand_),
      replaceNodeCommand_(other.replaceNodeCommand_) {}

EditTransaction& EditTransaction::operator=(const EditTransaction& other) {
  if (this == &other) {
    return *this;
  }
  storage_ = other.storage_;
  kind_ = other.kind_;
  label_ = other.label_;
  before_ = other.before_;
  after_ = other.after_;
  textDeltaCommand_ = other.textDeltaCommand_;
  tableCommand_ = other.tableCommand_;
  insertNodeCommand_ = other.insertNodeCommand_;
  replaceNodeCommand_ = other.replaceNodeCommand_;
  return *this;
}

EditTransaction::EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after)
    : storage_(Storage::Snapshot), kind_(kind), label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

EditTransaction::EditTransaction(
    Kind kind,
    QString label,
    TextDeltaCommand command)
    : storage_(Storage::TextDeltaCommand),
      kind_(kind),
      label_(std::move(label)),
      textDeltaCommand_(std::move(command)) {}

EditTransaction::EditTransaction(
    Kind kind,
    QString label,
    TableCommand command)
    : storage_(Storage::TableCommand),
      kind_(kind),
      label_(std::move(label)),
      tableCommand_(std::move(command)) {}

EditTransaction::EditTransaction(
    Kind kind,
    QString label,
    InsertNodeCommand command)
    : storage_(Storage::InsertNodeCommand),
      kind_(kind),
      label_(std::move(label)),
      insertNodeCommand_(std::move(command)) {}

EditTransaction::EditTransaction(
    Kind kind,
    QString label,
    ReplaceNodeCommand command)
    : storage_(Storage::ReplaceNodeCommand),
      kind_(kind),
      label_(std::move(label)),
      replaceNodeCommand_(std::move(command)) {}

EditTransaction::Kind EditTransaction::kind() const {
  return kind_;
}

QString EditTransaction::label() const {
  return label_;
}

EditTransaction::Storage EditTransaction::storage() const {
  return storage_;
}

bool EditTransaction::isSnapshot() const {
  return storage_ == Storage::Snapshot;
}

bool EditTransaction::isTextDeltaCommand() const {
  return storage_ == Storage::TextDeltaCommand;
}

bool EditTransaction::isTableCommand() const {
  return storage_ == Storage::TableCommand;
}

bool EditTransaction::isInsertNodeCommand() const {
  return storage_ == Storage::InsertNodeCommand;
}

bool EditTransaction::isReplaceNodeCommand() const {
  return storage_ == Storage::ReplaceNodeCommand;
}

const DocumentSnapshot& EditTransaction::before() const {
  return before_;
}

const DocumentSnapshot& EditTransaction::after() const {
  return after_;
}

const TextDeltaCommand& EditTransaction::textDeltaCommand() const {
  return textDeltaCommand_;
}

const TableCommand& EditTransaction::tableCommand() const {
  return tableCommand_;
}

const InsertNodeCommand& EditTransaction::insertNodeCommand() const {
  return insertNodeCommand_;
}

const ReplaceNodeCommand& EditTransaction::replaceNodeCommand() const {
  return replaceNodeCommand_;
}

bool EditTransaction::isValid() const {
  if (isSnapshot()) {
    return before_.markdownText != after_.markdownText;
  }
  if (isTextDeltaCommand()) {
    return textDeltaCommand_.isValid();
  }
  if (isTableCommand()) {
    return tableCommand_.isValid();
  }
  if (isInsertNodeCommand()) {
    return insertNodeCommand_.isValid();
  }
  if (isReplaceNodeCommand()) {
    return replaceNodeCommand_.isValid();
  }
  return false;
}

}  // namespace muffin
