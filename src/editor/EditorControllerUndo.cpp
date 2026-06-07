#include "editor/EditorController.h"

#include "blocks/table/TableModelOps.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"

namespace muffin {
namespace {

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
T attributeValue(const NodeAttributeValue& value) {
  return std::get<T>(value);
}

void applyNodeAttribute(MarkdownNode& node, NodeAttribute attribute, const NodeAttributeValue& value) {
  switch (attribute) {
    case NodeAttribute::HeadingLevel:
      node.setHeadingLevel(attributeValue<int>(value));
      break;
    case NodeAttribute::ListKind:
      node.setListKind(attributeValue<ListKind>(value));
      break;
    case NodeAttribute::ListStart:
      node.setListStart(attributeValue<int>(value));
      break;
    case NodeAttribute::ListTight:
      node.setListTight(attributeValue<bool>(value));
      break;
    case NodeAttribute::TaskChecked:
      node.setTaskChecked(attributeValue<bool>(value));
      break;
    case NodeAttribute::CodeLanguage:
      node.setCodeLanguage(attributeValue<QString>(value));
      break;
    case NodeAttribute::TableAlignments:
      node.setTableAlignments(attributeValue<QVector<TableAlignment>>(value));
      break;
    case NodeAttribute::TableRowIsHeader:
      node.setTableRowIsHeader(attributeValue<bool>(value));
      break;
    case NodeAttribute::Unknown:
      break;
  }
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

  session_->applyMarkdownText(snapshot.markdownText, true);
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
      return;
    }
    const bool applied = session_->applyTableSnapshot(command.tableId, command.tableIndex, *table, true);
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
      return;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *node, true);
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
      return;
    }
    auto nextNode = currentNode->clone(CloneMode::PreserveIds);
    applyNodeAttribute(*nextNode, command.attribute, undo ? command.beforeValue : command.afterValue);
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *nextNode, true);
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
    QString text = session_->markdownText();
    if (replaceStart < 0 || replaceEnd < replaceStart || replaceEnd > text.size()) {
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
