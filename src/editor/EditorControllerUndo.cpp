#include "editor/EditorController.h"

#include "blocks/table/TableModelOps.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"

#include <QDebug>
#include <QLoggingCategory>

#include <optional>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(undoLog, "muffin.undo", QtWarningMsg)

const char* undoDirection(bool undo) {
  return undo ? "undo" : "redo";
}

void warnUndoApplyFailed(bool undo, const char* command, const char* reason) {
  qCWarning(undoLog).nospace()
      << "Cannot apply " << undoDirection(undo) << " " << command << ": " << reason;
}

void warnUndoApplyFailed(bool undo, const char* command, const char* reason, qsizetype start, qsizetype length, qsizetype documentSize) {
  qCWarning(undoLog).nospace()
      << "Cannot apply " << undoDirection(undo) << " " << command << ": " << reason
      << " start=" << start
      << " length=" << length
      << " documentSize=" << documentSize;
}

void warnUndoApplyFallback(bool undo, const char* command, const char* reason) {
  qCWarning(undoLog).nospace()
      << "Falling back while applying " << undoDirection(undo) << " " << command << ": " << reason;
}

MarkdownNode* tableByIdOrIndex(DocumentSession& session, NodeId tableId, int tableIndex) {
  if (tableId.isValid()) {
    if (MarkdownNode* table = session.document().node(tableId)) {
      if (table->type() == BlockType::Table) {
        return table;
      }
    }
  }
  if (tableIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::Table) {
      if (index == tableIndex) {
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
  return visit(visit, session.document().root());
}

CursorPosition tableCursorForLocation(DocumentSession& session, const TableCommand& command, int row, int column, const CursorPosition& fallback) {
  CursorPosition cursor;
  MarkdownNode* table = tableByIdOrIndex(session, command.tableId, command.tableIndex);
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = TableModelOps::cellAt(*table, qMax(0, row), qMax(0, column));
  if (!cell && fallback.text.nodeId.isValid()) {
    if (MarkdownNode* fallbackCell = session.document().node(fallback.text.nodeId)) {
      if (fallbackCell->type() == BlockType::TableCell) {
        cell = fallbackCell;
      }
    }
  }
  if (!cell) {
    return cursor;
  }

  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  cursor.text.textOffset = qMax<qsizetype>(0, fallback.text.textOffset);
  cursor.text.sourceOffset = fallback.text.sourceOffset;
  return cursor;
}

CursorPosition tableCellTextCursor(DocumentSession& session, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (!storedCursor.isValid() || !storedCursor.text.nodeId.isValid()) {
    return cursor;
  }

  MarkdownNode* cell = session.document().node(storedCursor.text.nodeId);
  if (!cell || cell->type() != BlockType::TableCell) {
    return cursor;
  }

  MarkdownNode* table = cell;
  while (table && table->type() != BlockType::Table) {
    table = table->parent();
  }
  if (!table) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  return cursor;
}

MarkdownNode* nodeByTopLevelIndex(DocumentSession& session, int nodeIndex, BlockType nodeType) {
  const auto& children = session.document().root().children();
  if (nodeIndex < 0 || nodeIndex >= static_cast<int>(children.size())) {
    return nullptr;
  }
  MarkdownNode* node = children.at(static_cast<size_t>(nodeIndex)).get();
  return node && node->type() == nodeType ? node : nullptr;
}

MarkdownNode* nodeBySourceOffset(MarkdownNode& node, BlockType nodeType, qsizetype sourceOffset) {
  const SourceRange range = node.sourceRange();
  if (node.type() == nodeType && range.byteStart <= sourceOffset && range.byteEnd >= sourceOffset) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = nodeBySourceOffset(*child, nodeType, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

CursorPosition insertedNodeCursor(DocumentSession& session, const InsertNodeCommand& command, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (command.nodeType == BlockType::FrontMatter) {
    MarkdownNode* node = session.document().node(command.nodeId);
    if (!node || node->type() != BlockType::FrontMatter) {
      node = nodeBySourceOffset(session.document().root(), BlockType::FrontMatter, command.nodeSourceStart);
    }
    if (!node) {
      node = nodeByTopLevelIndex(session, command.nodeIndex, BlockType::FrontMatter);
    }
    if (!node) {
      return cursor;
    }
    cursor = storedCursor;
    cursor.blockId = node->id();
    cursor.text.nodeId = node->id();
    cursor.text.textOffset = qBound<qsizetype>(0, storedCursor.text.textOffset, node->literal().size());
    return cursor;
  }
  if (command.nodeType != BlockType::Table) {
    return cursor;
  }

  MarkdownNode* table = session.document().node(command.nodeId);
  if (!table || table->type() != BlockType::Table) {
    table = nodeBySourceOffset(session.document().root(), BlockType::Table, command.nodeSourceStart);
  }
  if (!table) {
    table = nodeByTopLevelIndex(session, command.nodeIndex, BlockType::Table);
  }
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = storedCursor.text.nodeId.isValid() ? session.document().node(storedCursor.text.nodeId) : nullptr;
  if (!cell || cell->type() != BlockType::TableCell) {
    cell = TableModelOps::cellAt(*table, 0, 0);
  }
  if (!cell) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  cursor.text.textOffset = qMax<qsizetype>(0, storedCursor.text.textOffset);
  return cursor;
}

CursorPosition replacedNodeCursor(DocumentSession& session, const ReplaceNodeCommand& command, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (!storedCursor.isValid()) {
    return cursor;
  }

  MarkdownNode* node = command.nodeId.isValid() ? session.document().node(command.nodeId) : nullptr;
  if (!node || node->type() != command.nodeType) {
    node = nodeByTopLevelIndex(session, command.nodeIndex, command.nodeType);
  }
  if (!node) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = node->id();
  cursor.text.nodeId = node->id();
  return cursor;
}

MarkdownNode* nodeByIdOrIndex(DocumentSession& session, NodeId nodeId, BlockType nodeType, int nodeIndex) {
  MarkdownNode* node = nodeId.isValid() ? session.document().node(nodeId) : nullptr;
  if (!node || node->type() != nodeType) {
    node = nodeByTopLevelIndex(session, nodeIndex, nodeType);
  }
  return node;
}

template <typename T>
std::optional<T> attributeValue(NodeAttribute attribute, const NodeAttributeValue& value) {
  if (const T* typed = std::get_if<T>(&value)) {
    return *typed;
  }
  qWarning() << "Cannot apply node attribute because value type does not match attribute" << static_cast<int>(attribute);
  return std::nullopt;
}

bool applyNodeAttribute(MarkdownNode& node, NodeAttribute attribute, const NodeAttributeValue& value) {
  if (!nodeAttributeAcceptsValue(attribute, value)) {
    qWarning() << "Cannot apply invalid node attribute value" << static_cast<int>(attribute);
    return false;
  }

  switch (attribute) {
    case NodeAttribute::HeadingLevel: {
      const std::optional<int> typed = attributeValue<int>(attribute, value);
      if (!typed) return false;
      node.setHeadingLevel(*typed);
      return true;
    }
    case NodeAttribute::ListKind: {
      const std::optional<ListKind> typed = attributeValue<ListKind>(attribute, value);
      if (!typed) return false;
      node.setListKind(*typed);
      return true;
    }
    case NodeAttribute::ListStart: {
      const std::optional<int> typed = attributeValue<int>(attribute, value);
      if (!typed) return false;
      node.setListStart(*typed);
      return true;
    }
    case NodeAttribute::ListTight: {
      const std::optional<bool> typed = attributeValue<bool>(attribute, value);
      if (!typed) return false;
      node.setListTight(*typed);
      return true;
    }
    case NodeAttribute::TaskChecked: {
      const std::optional<bool> typed = attributeValue<bool>(attribute, value);
      if (!typed) return false;
      node.setTaskChecked(*typed);
      return true;
    }
    case NodeAttribute::CodeLanguage: {
      const std::optional<QString> typed = attributeValue<QString>(attribute, value);
      if (!typed) return false;
      node.setCodeLanguage(*typed);
      return true;
    }
    case NodeAttribute::TableAlignments: {
      const std::optional<QVector<TableAlignment>> typed = attributeValue<QVector<TableAlignment>>(attribute, value);
      if (!typed) return false;
      node.setTableAlignments(*typed);
      return true;
    }
    case NodeAttribute::TableRowIsHeader: {
      const std::optional<bool> typed = attributeValue<bool>(attribute, value);
      if (!typed) return false;
      node.setTableRowIsHeader(*typed);
      return true;
    }
    case NodeAttribute::Unknown:
      return false;
  }
  return false;
}

bool applyTextReplacement(DocumentSession& session, qsizetype replaceStart, qsizetype replaceLength, const QString& replacement, const QVector<NodeId>& affectedNodes) {
  QVector<LocalEditNodeHint> nodeHints;
  for (NodeId nodeId : affectedNodes) {
    nodeHints.push_back(LocalEditNodeHint{nodeId, replaceStart, BlockType::Unknown});
  }
  if (session.applyTextDelta(replaceStart, replaceLength, replacement, true, std::move(nodeHints))) {
    return true;
  }

  QString text = session.markdownText();
  if (replaceStart < 0 || replaceLength < 0 || replaceStart + replaceLength > text.size()) {
    return false;
  }
  text.replace(replaceStart, replaceLength, replacement);
  session.applyMarkdownText(std::move(text), true);
  return true;
}

NodeId topLevelBlockIdFor(DocumentSession& session, NodeId nodeId) {
  if (!nodeId.isValid()) {
    return {};
  }
  const MarkdownNode* node = session.document().node(nodeId);
  if (!node) {
    return {};
  }
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  return node && node->parent() && node->parent()->type() == BlockType::Document ? node->id() : NodeId();
}

void addRefreshNode(DocumentSession& session, QVector<NodeId>& refreshNodes, NodeId nodeId) {
  NodeId topLevelId = topLevelBlockIdFor(session, nodeId);
  if (!topLevelId.isValid()) {
    topLevelId = nodeId;
  }
  if (topLevelId.isValid() && !refreshNodes.contains(topLevelId)) {
    refreshNodes.push_back(topLevelId);
  }
}

QVector<NodeId> refreshNodesFor(DocumentSession& session, const QVector<NodeId>& affectedNodes, CursorPosition cursor = {}) {
  QVector<NodeId> refreshNodes;
  for (NodeId nodeId : affectedNodes) {
    addRefreshNode(session, refreshNodes, nodeId);
  }
  if (cursor.isValid()) {
    addRefreshNode(session, refreshNodes, cursor.blockId);
    addRefreshNode(session, refreshNodes, cursor.text.nodeId);
  }
  return refreshNodes;
}

void requestRefreshForNodes(BrushQueue& brushQueue, DocumentSession& session, const QVector<NodeId>& affectedNodes, CursorPosition cursor = {}) {
  QVector<NodeId> refreshNodes = refreshNodesFor(session, affectedNodes, cursor);
  if (refreshNodes.isEmpty()) {
    brushQueue.requestFullRefresh();
    return;
  }
  brushQueue.requestBlocksRefresh(std::move(refreshNodes));
}

}  // namespace

void EditorController::applySnapshot(const DocumentSnapshot& snapshot) {
  if (!session_) {
    return;
  }

  session_->applyMarkdownText(snapshot.markdownText, true, snapshot.demoteAtOffsets);
  const CursorPosition cursor = remapSnapshotCursor(snapshot.cursor);
  if (cursor.isValid()) {
    selection_.setCursorPosition(cursor);
  } else {
    selection_.clear();
  }
  brushQueue_.requestFullRefresh();
}

void EditorController::applyTransaction(const EditTransaction& transaction, bool undo) {
  if (!session_ || !transaction.isValid()) {
    return;
  }

  if (transaction.isSnapshot()) {
    applySnapshot(undo ? transaction.before() : transaction.after());
    return;
  }

  if (transaction.isTableCommand()) {
    const TableCommand& command = transaction.tableCommand();
    const MarkdownNode* table = undo ? command.beforeTable.get() : command.afterTable.get();
    if (!table) {
      warnUndoApplyFailed(undo, "table command", "missing stored table snapshot");
      return;
    }
    const bool applied = session_->applyTableSnapshot(command.tableId, command.tableIndex, *table, true);
    if (!applied) {
      warnUndoApplyFailed(undo, "table command", "table snapshot could not be applied");
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = tableCursorForLocation(
        *session_, command, undo ? command.beforeRow : command.afterRow, undo ? command.beforeColumn : command.afterColumn, storedCursor);
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (applied) {
      requestRefreshForNodes(brushQueue_, *session_, QVector<NodeId>{command.tableId}, cursor);
    }
    return;
  }

  if (transaction.isInsertNodeCommand()) {
    const InsertNodeCommand& command = transaction.insertNodeCommand();
    const QString replacement = undo ? command.delta.removedText : command.delta.insertedText;
    const qsizetype replaceStart = command.delta.start;
    const qsizetype replaceEnd = command.delta.start + (undo ? command.delta.insertedText.size() : command.delta.removedText.size());
    const bool appliedLocally =
        session_->applyInsertedNode(command.nodeId, command.nodeType, replaceStart, command.nodeSourceStart, replaceEnd - replaceStart, replacement, true);
    if (!appliedLocally) {
      warnUndoApplyFallback(undo, "insert node command", "inserted node delta could not be applied locally");
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = undo ? CursorPosition() : insertedNodeCursor(*session_, command, storedCursor);
    if (!cursor.isValid()) {
      cursor = tableCellTextCursor(*session_, storedCursor);
    }
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      QVector<NodeId> affectedNodes = command.affectedNodes;
      if (!undo && command.nodeId.isValid() && !affectedNodes.contains(command.nodeId)) {
        affectedNodes.push_back(command.nodeId);
      }
      requestRefreshForNodes(brushQueue_, *session_, affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (transaction.isReplaceNodeCommand()) {
    const ReplaceNodeCommand& command = transaction.replaceNodeCommand();
    const MarkdownNode* node = undo ? command.beforeNode.get() : command.afterNode.get();
    if (!node) {
      warnUndoApplyFailed(undo, "replace node command", "missing stored node snapshot");
      return;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *node, true);
    if (!appliedLocally) {
      warnUndoApplyFallback(undo, "replace node command", "node snapshot could not be applied locally");
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = replacedNodeCursor(*session_, command, storedCursor);
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (transaction.isRemoveNodeCommand()) {
    const RemoveNodeCommand& command = transaction.removeNodeCommand();
    const QString replacement = undo ? command.delta.removedText : QString();
    const qsizetype replaceLength = undo ? 0 : command.delta.removedText.size();
    const bool applied = applyTextReplacement(*session_, command.delta.start, replaceLength, replacement, command.affectedNodes);
    if (!applied) {
      warnUndoApplyFailed(
          undo,
          "remove node command",
          "text replacement range is invalid",
          command.delta.start,
          replaceLength,
          session_->markdownText().size());
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (applied) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    }
    return;
  }

  if (transaction.isSetNodeAttrCommand()) {
    const SetNodeAttrCommand& command = transaction.setNodeAttrCommand();
    MarkdownNode* currentNode = nodeByIdOrIndex(*session_, command.nodeId, command.nodeType, command.nodeIndex);
    if (!currentNode) {
      warnUndoApplyFailed(undo, "set node attribute command", "target node could not be found");
      return;
    }
    auto nextNode = currentNode->clone(CloneMode::PreserveIds);
    if (!applyNodeAttribute(*nextNode, command.attribute, undo ? command.beforeValue : command.afterValue)) {
      warnUndoApplyFailed(undo, "set node attribute command", "attribute value could not be applied");
      return;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *nextNode, true);
    if (!appliedLocally) {
      warnUndoApplyFallback(undo, "set node attribute command", "node snapshot could not be applied locally");
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (!transaction.isTextDeltaCommand()) {
    warnUndoApplyFailed(undo, "transaction", "unsupported transaction storage");
    return;
  }

  const TextDeltaCommand& command = transaction.textDeltaCommand();
  const TextDelta& delta = command.delta;
  const QString replacement = undo ? delta.removedText : delta.insertedText;
  const qsizetype replaceStart = delta.start;
  const qsizetype replaceEnd = delta.start + (undo ? delta.insertedText.size() : delta.removedText.size());
  QVector<LocalEditNodeHint> nodeHints;
  for (NodeId nodeId : command.affectedNodes) {
    nodeHints.push_back(LocalEditNodeHint{nodeId, replaceStart, BlockType::Unknown});
  }
  const bool appliedLocally = session_->applyTextDelta(replaceStart, replaceEnd - replaceStart, replacement, true, std::move(nodeHints));
  const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
  CursorPosition cursor = tableCellTextCursor(*session_, storedCursor);
  if (!cursor.isValid()) {
    cursor = remapSnapshotCursor(storedCursor);
  }
  if (cursor.isValid()) {
    selection_.setCursorPosition(cursor);
  } else {
    selection_.clear();
  }

  if (!appliedLocally) {
    warnUndoApplyFallback(undo, "text delta command", "text delta could not be applied locally");
    QString text = session_->markdownText();
    if (replaceStart < 0 || replaceEnd < replaceStart || replaceEnd > text.size()) {
      warnUndoApplyFailed(
          undo,
          "text delta command",
          "fallback text replacement range is invalid",
          replaceStart,
          replaceEnd >= replaceStart ? replaceEnd - replaceStart : qsizetype(-1),
          text.size());
      return;
    }
    text.replace(replaceStart, replaceEnd - replaceStart, replacement);
    session_->applyMarkdownText(std::move(text), true);
    brushQueue_.requestFullRefresh();
  } else {
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
  }
}

CursorPosition EditorController::remapSnapshotCursor(const CursorPosition& snapshotCursor) const {
  CursorPosition cursor;
  if (!session_ || !snapshotCursor.isValid()) {
    return cursor;
  }

  BlockEditContextResolver resolver(const_cast<DocumentSession*>(session_), const_cast<SelectionController*>(&selection_));
  // The virtual trailing-paragraph caret is positional (no source offset); keep
  // it on the trailing line as long as its block still exists after the undo/redo.
  if (snapshotCursor.afterBlock) {
    MarkdownNode* node = session_->document().node(snapshotCursor.blockId);
    if (!node) {
      // A full reparse (e.g. snapshot undo) may reassign node ids; fall back to
      // the last top-level block, which is where the trailing paragraph lives.
      const auto& children = session_->document().root().children();
      if (!children.empty()) {
        node = children.back().get();
      }
    }
    if (node && node->parent() && node->parent()->type() == BlockType::Document) {
      cursor.blockId = node->id();
      cursor.text.nodeId = node->id();
      cursor.afterBlock = true;
      return cursor;
    }
    return cursor;
  }
  if (snapshotCursor.text.sourceOffset >= 0) {
    if (MarkdownNode* node = resolver.nodeAtContentSourceOffset(session_->document().root(), snapshotCursor.text.sourceOffset)) {
      BlockEditContext context;
      if (resolver.fill(*node, context)) {
        const qsizetype localSourceOffset =
            qBound<qsizetype>(0, snapshotCursor.text.sourceOffset - context.contentRange.byteStart, context.contentText.size());
        qsizetype visibleOffset = -1;
        if (!context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
          visibleOffset = qBound<qsizetype>(0, localSourceOffset, context.visibleText.size());
        }
        cursor.blockId = node->id();
        cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
        cursor.text.textOffset = visibleOffset;
        cursor.text.sourceOffset = snapshotCursor.text.sourceOffset;
        cursor.text.inMeta = snapshotCursor.text.inMeta;
        return cursor;
      }
    }
  }

  if (MarkdownNode* node = session_->document().node(snapshotCursor.blockId)) {
    BlockEditContext context;
    if (resolver.fill(*node, context)) {
      const qsizetype visibleOffset = qBound<qsizetype>(0, snapshotCursor.text.textOffset, context.visibleText.size());
      qsizetype localSourceOffset = -1;
      context.inlineProjection.sourceOffsetForVisibleOffset(visibleOffset, localSourceOffset);
      cursor.blockId = node->id();
      cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
      cursor.text.textOffset = visibleOffset;
      cursor.text.sourceOffset = localSourceOffset >= 0 ? context.contentRange.byteStart + localSourceOffset : snapshotCursor.text.sourceOffset;
      cursor.text.inMeta = snapshotCursor.text.inMeta;
      return cursor;
    }
    cursor.blockId = node->id();
    cursor.text.nodeId = node->id();
    cursor.text.textOffset = snapshotCursor.text.textOffset;
    cursor.text.sourceOffset = snapshotCursor.text.sourceOffset;
    cursor.text.inMeta = snapshotCursor.text.inMeta;
    return cursor;
  }

  const QString markdown = session_->markdownText();
  const qsizetype sourceOffset = qBound<qsizetype>(0, snapshotCursor.text.sourceOffset >= 0 ? snapshotCursor.text.sourceOffset : markdown.size(), markdown.size());
  if (MarkdownNode* node = resolver.nodeAtContentSourceOffset(session_->document().root(), sourceOffset)) {
    BlockEditContext context;
    if (resolver.fill(*node, context)) {
      const qsizetype localSourceOffset = qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
      qsizetype visibleOffset = -1;
      context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset);
      cursor.blockId = node->id();
      cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
      cursor.text.textOffset = qBound<qsizetype>(0, visibleOffset, context.visibleText.size());
      cursor.text.sourceOffset = context.contentRange.byteStart + localSourceOffset;
      cursor.text.inMeta = snapshotCursor.text.inMeta;
      return cursor;
    }
  }
  return cursor;
}

}  // namespace muffin
