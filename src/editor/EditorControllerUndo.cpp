#include "editor/EditorController.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "blocks/table/TableModelOps.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>

#include <cstring>
#include <optional>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(undoLog, "muffin.undo", QtWarningMsg)
Q_LOGGING_CATEGORY(undoPerf, "muffin.perf", QtWarningMsg)

// Scoped perf probe routed to the muffin.perf category (captured by MUFFIN_PERF_LOG). No-op when
// perf debugging is off. Used to localize the Ctrl+Z-hangs-on-a-huge-document regression: the
// blocking cost is a synchronous full reparse (applyMarkdownText) + full layout rebuild fired when
// the undo's localized edit can't be applied (applyTextDelta returns false). These probes record
// which undo branch ran, the snapshot-diff region, and whether the localized path succeeded.
struct UndoPerfTimer : diag::ScopedPerfProbe {
  explicit UndoPerfTimer(const char* label) : diag::ScopedPerfProbe(label, undoPerf()) {}
};

const char* undoDirection(bool undo) {
  return undo ? "undo" : "redo";
}

// Common leading char count of two QStrings, compared in 4K-char chunks via memcmp (SIMD-fast on
// matching runs) so a 100MB common prefix skips in O(doc/chunk) instead of O(doc) char-by-char.
qsizetype commonPrefixLength(const QString& a, const QString& b) {
  const qsizetype chunk = 4096;
  const qsizetype minLen = qMin(a.size(), b.size());
  qsizetype p = 0;
  for (; p + chunk <= minLen; p += chunk) {
    if (std::memcmp(a.utf16() + p, b.utf16() + p, static_cast<size_t>(chunk) * sizeof(QChar)) != 0) {
      break;
    }
  }
  for (; p < minLen && a.at(p) == b.at(p); ++p) {
  }
  return p;
}

// Common trailing char count (counted from the end), stopping before `prefix` so prefix and suffix
// can't overlap. Also chunked via memcmp.
qsizetype commonSuffixLength(const QString& a, const QString& b, qsizetype prefix) {
  const qsizetype chunk = 4096;
  const qsizetype maxSuffix = qMax<qsizetype>(0, qMin(a.size(), b.size()) - prefix);
  qsizetype s = 0;
  for (; s + chunk <= maxSuffix; s += chunk) {
    if (std::memcmp(a.utf16() + a.size() - s - chunk, b.utf16() + b.size() - s - chunk,
                    static_cast<size_t>(chunk) * sizeof(QChar)) != 0) {
      break;
    }
  }
  for (; s < maxSuffix && a.at(a.size() - 1 - s) == b.at(b.size() - 1 - s); ++s) {
  }
  return s;
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
  const auto matches = [nodeType, sourceOffset](MarkdownNode& candidate) {
    const SourceRange range = candidate.sourceRange();
    return candidate.type() == nodeType && range.byteStart <= sourceOffset && range.byteEnd >= sourceOffset;
  };
  if (matches(node)) {
    return &node;
  }
  return node.findDescendant(matches);
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

  QString text = session.markdownText().toString();
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
  // If the just-applied local edit changed top-level block structure (a split/merge/insert/remove
  // altered the block count), a localized block refresh can't represent the new structure —
  // refreshBlock fails to find the reshuffled/removed slot and the brush-queue consumer falls back
  // to a whole-document layout rebuild (≈22s on a 100MB doc; this was the Ctrl+Z-hangs regression).
  // Refresh the precise top-level range the local edit recorded instead, exactly like applySnapshot
  // and the forward-edit path. Only non-structural local edits take the cheap block-refresh path.
  if (session.lastLocalEditChangedTopLevelStructure()) {
    const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
    if (range.isValid()) {
      qCDebug(undoPerf).nospace() << "undo.refresh.structural → requestTopLevelRangeRefresh"
          << " first=" << range.first << " old=" << range.oldCount << " new=" << range.newCount;
      brushQueue.requestTopLevelRangeRefresh(range);
      return;
    }
  }
  QVector<NodeId> refreshNodes = refreshNodesFor(session, affectedNodes, cursor);
  if (refreshNodes.isEmpty()) {
    // The edit applied locally but none of the stored affected/cursor nodes resolve in the live
    // tree (a block merged away on undo). We only reach here on the appliedLocally branches of
    // applyTransaction, so the session just recorded a precise top-level range change for the edit.
    // Refresh that range (localized) instead of falling back to a whole-document layout refresh,
    // which is ≈20s on a 100MB doc. The range change is authoritative: it covers exactly the
    // top-level blocks tryApplyTopLevelLocalEdit spliced.
    const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
    if (range.isValid()) {
      qCDebug(undoPerf).nospace() << "undo.refresh.emptyRefreshNodes → requestTopLevelRangeRefresh"
          << " first=" << range.first << " old=" << range.oldCount << " new=" << range.newCount;
      brushQueue.requestTopLevelRangeRefresh(range);
      return;
    }
    qCDebug(undoPerf).nospace() << "undo.refresh.emptyRefreshNodes → requestFullRefresh (no range)"
        << " affectedNodes=" << affectedNodes.size() << " cursorValid=" << cursor.isValid();
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

  UndoPerfTimer perf("undo.applySnapshot");
  // Undo/redo only changed a localized region (the originating edit was local), so the diff between
  // the current LIVE text and the snapshot text is small. Apply it as a LOCAL edit (slice reparse)
  // via applyTextDelta instead of applyMarkdownText's full-document reparse — that full reparse was
  // the Ctrl+Z-hangs-on-a-huge-document regression (O(doc) parse + full view rebuild). The chunked
  // common-prefix/suffix skips the (typically huge) unchanged runs in O(doc/chunk). Falls back to
  // the full reparse only when the local edit is rejected (a wholesale change the slice logic can't
  // localize), which is rare for ordinary edits.
  const QString current = session_->markdownText().toString();
  const QString& target = snapshot.markdownText;
  const qsizetype prefix = commonPrefixLength(current, target);
  const qsizetype commonSuffix = commonSuffixLength(current, target, prefix);
  const qsizetype curSuffix = current.size() - commonSuffix;
  const qsizetype tgtSuffix = target.size() - commonSuffix;
  const QString inserted = target.mid(prefix, tgtSuffix - prefix);
  const bool appliedLocally = session_->applyTextDelta(prefix, curSuffix - prefix, inserted, true);
  // Record the diff region + outcome so the perf trace shows exactly why a snapshot undo either
  // localized (fast) or fell back to a full reparse (the ~20s regression). curSize/tgtSize make a
  // pathological "whole document changed" diff obvious at a glance.
  qCDebug(undoPerf).nospace() << "undo.snapshot diff curSize=" << current.size()
      << " tgtSize=" << target.size() << " prefix=" << prefix << " suffix=" << commonSuffix
      << " curReplaceLen=" << (curSuffix - prefix) << " insertedLen=" << inserted.size()
      << " appliedLocally=" << appliedLocally;
  if (!appliedLocally) {
    UndoPerfTimer fullParsePerf("undo.snapshot.fullParseFallback");
    session_->applyMarkdownText(target, true, snapshot.demoteAtOffsets);
  }

  const CursorPosition cursor = remapSnapshotCursor(snapshot.cursor);
  if (cursor.isValid()) {
    selection_.setCursorPosition(cursor);
  } else {
    selection_.clear();
  }
  if (appliedLocally) {
    brushQueue_.requestTopLevelRangeRefresh(session_->lastLocalTopLevelRangeChange());
  } else {
    brushQueue_.requestFullRefresh();
  }
}

// Applies one popped undo/redo transaction. Returns true when the state change landed
// (directly or via the full-reparse fallback); false when nothing was applied — callers
// must then put the transaction back on its original stack (UndoStack::restoreUndo/
// restoreRedo), because takeUndo/takeRedo already moved it to the opposite stack.
bool EditorController::applyTransaction(const EditTransaction& transaction, bool undo) {
  if (!session_ || !transaction.isValid()) {
    return false;
  }

  UndoPerfTimer perf(undo ? "undo.applyTransaction" : "redo.applyTransaction");
  qCDebug(undoPerf).nospace() << (undo ? "undo" : "redo") << ".begin label=\"" << transaction.label()
      << "\" snapshot=" << transaction.isSnapshot() << " textDelta=" << transaction.isTextDeltaCommand();

  if (transaction.isSnapshot()) {
    applySnapshot(undo ? transaction.before() : transaction.after());
    return true;
  }

  if (transaction.isTableCommand()) {
    const TableCommand& command = transaction.tableCommand();
    const MarkdownNode* table = undo ? command.beforeTable.get() : command.afterTable.get();
    if (!table) {
      warnUndoApplyFailed(undo, "table command", "missing stored table snapshot");
      return false;
    }
    const bool applied = session_->applyTableSnapshot(command.tableId, command.tableIndex, *table, true);
    if (!applied) {
      warnUndoApplyFailed(undo, "table command", "table snapshot could not be applied");
      return false;
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
    requestRefreshForNodes(brushQueue_, *session_, QVector<NodeId>{command.tableId}, cursor);
    return true;
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
      return false;
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
    QVector<NodeId> affectedNodes = command.affectedNodes;
    if (!undo && command.nodeId.isValid() && !affectedNodes.contains(command.nodeId)) {
      affectedNodes.push_back(command.nodeId);
    }
    requestRefreshForNodes(brushQueue_, *session_, affectedNodes, cursor);
    return true;
  }

  if (transaction.isReplaceNodeCommand()) {
    const ReplaceNodeCommand& command = transaction.replaceNodeCommand();
    const MarkdownNode* node = undo ? command.beforeNode.get() : command.afterNode.get();
    if (!node) {
      warnUndoApplyFailed(undo, "replace node command", "missing stored node snapshot");
      return false;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *node, true);
    if (!appliedLocally) {
      warnUndoApplyFallback(undo, "replace node command", "node snapshot could not be applied locally");
      return false;
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
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    return true;
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
      return false;
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    return true;
  }

  if (transaction.isSetNodeAttrCommand()) {
    const SetNodeAttrCommand& command = transaction.setNodeAttrCommand();
    MarkdownNode* currentNode = nodeByIdOrIndex(*session_, command.nodeId, command.nodeType, command.nodeIndex);
    if (!currentNode) {
      warnUndoApplyFailed(undo, "set node attribute command", "target node could not be found");
      return false;
    }
    auto nextNode = currentNode->clone(CloneMode::PreserveIds);
    if (!applyNodeAttribute(*nextNode, command.attribute, undo ? command.beforeValue : command.afterValue)) {
      warnUndoApplyFailed(undo, "set node attribute command", "attribute value could not be applied");
      return false;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *nextNode, true);
    if (!appliedLocally) {
      warnUndoApplyFallback(undo, "set node attribute command", "node snapshot could not be applied locally");
      return false;
    }
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    return true;
  }

  if (!transaction.isTextDeltaCommand()) {
    warnUndoApplyFailed(undo, "transaction", "unsupported transaction storage");
    return false;
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
  qCDebug(undoPerf).nospace() << "undo.textDelta replaceStart=" << replaceStart
      << " replaceLen=" << (replaceEnd - replaceStart) << " replacementLen=" << replacement.size()
      << " affectedNodes=" << command.affectedNodes.size() << " appliedLocally=" << appliedLocally;
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
    QString text = session_->markdownText().toString();
    if (replaceStart < 0 || replaceEnd < replaceStart || replaceEnd > text.size()) {
      warnUndoApplyFailed(
          undo,
          "text delta command",
          "fallback text replacement range is invalid",
          replaceStart,
          replaceEnd >= replaceStart ? replaceEnd - replaceStart : qsizetype(-1),
          text.size());
      return false;
    }
    text.replace(replaceStart, replaceEnd - replaceStart, replacement);
    {
      UndoPerfTimer fullParsePerf("undo.textDelta.fullParseFallback");
      session_->applyMarkdownText(std::move(text), true);
    }
    brushQueue_.requestFullRefresh();
  } else {
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
  }
  return true;
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

  const PieceTable& markdown = session_->markdownText();
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
