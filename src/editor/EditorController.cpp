#include "editor/EditorController.h"

#include "blocks/table/TableModelOps.h"
#include "document/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "editor/EditorView.h"

namespace muffin {
namespace {

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) {
  if (line <= 0 || column <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (currentLine < line && offset < text.size()) {
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }

  if (currentLine != line) {
    return -1;
  }
  return qMin(offset + column - 1, text.size());
}

qsizetype sourceOffsetForLineEnd(const QString& text, int line) {
  if (line <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (offset < text.size()) {
    if (currentLine == line && text.at(offset) == QLatin1Char('\n')) {
      return offset;
    }
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }
  return currentLine == line ? text.size() : -1;
}

MarkdownNode* primaryParagraph(MarkdownNode& node) {
  if (node.type() == BlockType::ListItem) {
    for (const auto& child : node.children()) {
      if (child->type() == BlockType::Paragraph) {
        return child.get();
      }
    }
  }
  return &node;
}

bool fillSourceOffsetForTextHit(const DocumentSession& session, HitTestResult& hit) {
  if (hit.zone != HitTestResult::Zone::Text && hit.zone != HitTestResult::Zone::Marker &&
      hit.zone != HitTestResult::Zone::TableCell) {
    return false;
  }

  MarkdownNode* node = session.document().node(hit.zone == HitTestResult::Zone::TableCell ? hit.textNodeId : hit.blockId);
  if (!node) {
    return false;
  }

  MarkdownNode* editable = primaryParagraph(*node);
  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading &&
                    editable->type() != BlockType::TableCell)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  const QString markdown = session.markdownText();
  qsizetype start = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (start < 0 || end < start) {
    return false;
  }

  if (editable->type() == BlockType::Heading) {
    while (start < end && markdown.at(start) == QLatin1Char('#')) {
      ++start;
    }
    if (start < end && markdown.at(start).isSpace()) {
      ++start;
    }
  }

  const QString contentText = markdown.mid(start, end - start);
  qsizetype localSourceOffset = -1;
  CursorPosition cursor = hit.cursorPosition();
  cursor.text.textOffset = hit.textOffset;
  cursor.text.sourceOffset = hit.sourceOffset;
  InlineProjectionState projectionState = InlineProjectionState::forCursor(cursor, hit.blockId, start);
  InlineProjection projection(editable->inlines(), contentText, projectionState);
  if (hit.sourceOffset >= 0) {
    localSourceOffset = qBound<qsizetype>(0, hit.sourceOffset - start, contentText.size());
  } else if (!projection.sourceOffsetForVisibleOffset(hit.textOffset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, hit.textOffset, contentText.size());
  }
  hit.sourceOffset = start + localSourceOffset;
  return true;
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

CursorPosition tableCursorForLocation(DocumentSession& session, const TableCommand& command, const CursorPosition& fallback) {
  CursorPosition cursor;
  MarkdownNode* table = tableByIdOrIndex(session, command.tableId, command.tableIndex);
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = TableModelOps::cellAt(*table, qMax(0, command.cursorRow), qMax(0, command.cursorColumn));
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

}  // namespace

EditorController::EditorController(QObject* parent) : QObject(parent) {}

void EditorController::attach(DocumentSession* session, EditorView* view) {
  if (session_ == session && view_ == view) {
    return;
  }

  detach();
  session_ = session;
  view_ = view;

  inputController_.setDocumentSession(session_);
  inputController_.setSelectionController(&selection_);
  inputController_.setUndoStack(&undoStack_);
  inputController_.setBrushQueue(&brushQueue_);
  inputController_.setTableController(&tableController_);
  inputController_.setCodeFenceController(&codeFenceController_);
  inputController_.setHtmlBlockController(&htmlBlockController_);
  inputController_.setMathBlockController(&mathBlockController_);
  inputController_.attach(view_);
  stylizeController_.setDocumentSession(session_);
  stylizeController_.setSelectionController(&selection_);
  stylizeController_.setUndoStack(&undoStack_);
  stylizeController_.setBrushQueue(&brushQueue_);
  codeFenceController_.setDocumentSession(session_);
  codeFenceController_.setSelectionController(&selection_);
  codeFenceController_.setUndoStack(&undoStack_);
  codeFenceController_.setBrushQueue(&brushQueue_);
  htmlBlockController_.setDocumentSession(session_);
  htmlBlockController_.setSelectionController(&selection_);
  htmlBlockController_.setUndoStack(&undoStack_);
  htmlBlockController_.setBrushQueue(&brushQueue_);
  mathBlockController_.setDocumentSession(session_);
  mathBlockController_.setSelectionController(&selection_);
  mathBlockController_.setUndoStack(&undoStack_);
  mathBlockController_.setBrushQueue(&brushQueue_);
  tableController_.setDocumentSession(session_);
  tableController_.setSelectionController(&selection_);
  tableController_.setUndoStack(&undoStack_);
  tableController_.setBrushQueue(&brushQueue_);
  clipboardController_.setDocumentSession(session_);
  clipboardController_.setSelectionController(&selection_);
  clipboardController_.setInputController(&inputController_);

  if (view_) {
    connect(view_, &EditorView::blockClicked, this, &EditorController::activateHit);
    connect(view_, &EditorView::selectionChanged, &selection_, &SelectionController::setSelection);
    connect(view_, &EditorView::textCommitted, &inputController_, &InputController::insertText);
  }
  connect(&selection_, &SelectionController::selectionChanged, this, [this](SelectionRange selection, HitTestResult hit) {
    if (view_) {
      if (selection.focus.isValid() && !selection.isCollapsed()) {
        view_->setSelectionRange(selection);
      } else if (selection.focus.isValid()) {
        view_->setCursorPosition(selection.focus);
      } else {
        view_->clearCursor();
      }
    }
    emit cursorChanged(hit);
    emit stateChanged();
  });
  connect(&undoStack_, &UndoStack::stateChanged, this, &EditorController::stateChanged);
  connect(&brushQueue_, &BrushQueue::refreshRequested, this, [this](const BrushQueue::RefreshRequest& request) {
    if (!session_ || !view_) {
      return;
    }
    if (request.fullLayoutDirty) {
      view_->setDocument(session_->document());
      return;
    }
    if (request.layoutDirtyBlocks.size() == 1) {
      if (!view_->refreshBlock(request.layoutDirtyBlocks.first(), session_->document())) {
        view_->setDocument(session_->document());
      }
      return;
    }
    if (!request.layoutDirtyBlocks.isEmpty() && !view_->refreshBlocks(request.layoutDirtyBlocks, session_->document())) {
      view_->setDocument(session_->document());
    }
  });
}

void EditorController::detach() {
  if (view_) {
    view_->disconnect(this);
  }
  selection_.disconnect(this);
  undoStack_.disconnect(this);
  brushQueue_.disconnect(this);
  inputController_.attach(nullptr);
  session_ = nullptr;
  view_ = nullptr;
}

SelectionController& EditorController::selection() {
  return selection_;
}

const SelectionController& EditorController::selection() const {
  return selection_;
}

UndoStack& EditorController::undoStack() {
  return undoStack_;
}

const UndoStack& EditorController::undoStack() const {
  return undoStack_;
}

InputController& EditorController::inputController() {
  return inputController_;
}

StylizeController& EditorController::stylizeController() {
  return stylizeController_;
}

CodeFenceController& EditorController::codeFenceController() {
  return codeFenceController_;
}

HtmlBlockController& EditorController::htmlBlockController() {
  return htmlBlockController_;
}

MathBlockController& EditorController::mathBlockController() {
  return mathBlockController_;
}

TableController& EditorController::tableController() {
  return tableController_;
}

ClipboardController& EditorController::clipboardController() {
  return clipboardController_;
}

BrushQueue& EditorController::brushQueue() {
  return brushQueue_;
}

bool EditorController::canUndo() const {
  return undoStack_.canUndo();
}

bool EditorController::canRedo() const {
  return undoStack_.canRedo();
}

void EditorController::undo() {
  if (!canUndo()) {
    return;
  }
  applyTransaction(undoStack_.takeUndo(), true);
}

void EditorController::redo() {
  if (!canRedo()) {
    return;
  }
  applyTransaction(undoStack_.takeRedo(), false);
}

bool EditorController::toggleBold() {
  return stylizeController_.toggleBold();
}

bool EditorController::toggleItalic() {
  return stylizeController_.toggleItalic();
}

bool EditorController::toggleCode() {
  return stylizeController_.toggleCode();
}

bool EditorController::insertLink() {
  return stylizeController_.insertLink();
}

bool EditorController::insertTableRowBefore() {
  return tableController_.insertRowBefore();
}

bool EditorController::insertTableRowAfter() {
  return tableController_.insertRowAfter();
}

bool EditorController::deleteTableRow() {
  return tableController_.deleteCurrentRow();
}

bool EditorController::moveTableRowUp() {
  return tableController_.moveCurrentRowUp();
}

bool EditorController::moveTableRowDown() {
  return tableController_.moveCurrentRowDown();
}

bool EditorController::insertTableColumnBefore() {
  return tableController_.insertColumnBefore();
}

bool EditorController::insertTableColumnAfter() {
  return tableController_.insertColumnAfter();
}

bool EditorController::deleteTableColumn() {
  return tableController_.deleteCurrentColumn();
}

bool EditorController::moveTableColumnLeft() {
  return tableController_.moveCurrentColumnLeft();
}

bool EditorController::moveTableColumnRight() {
  return tableController_.moveCurrentColumnRight();
}

bool EditorController::setTableColumnAlignment(TableAlignment alignment) {
  return tableController_.setCurrentColumnAlignment(alignment);
}

bool EditorController::insertTable() {
  return tableController_.insertTable();
}

bool EditorController::enterCodeFenceEditMode() {
  return codeFenceController_.enterEditMode();
}

bool EditorController::exitCodeFenceEditMode() {
  return codeFenceController_.exitEditMode();
}

bool EditorController::setCodeFenceLanguage(QString language) {
  return codeFenceController_.setLanguage(std::move(language));
}

bool EditorController::setCodeFenceLanguage(NodeId codeId, QString language) {
  return codeFenceController_.setLanguageFor(codeId, std::move(language));
}

bool EditorController::enterHtmlBlockEditMode() {
  return htmlBlockController_.enterEditMode();
}

bool EditorController::exitHtmlBlockEditMode() {
  return htmlBlockController_.exitEditMode();
}

bool EditorController::setHtmlBlockSource(QString html) {
  return htmlBlockController_.setHtml(std::move(html));
}

bool EditorController::enterMathBlockEditMode() {
  return mathBlockController_.enterEditMode();
}

bool EditorController::exitMathBlockEditMode() {
  return mathBlockController_.exitEditMode();
}

bool EditorController::setMathBlockTex(QString tex) {
  return mathBlockController_.setTex(std::move(tex));
}

bool EditorController::copy() {
  return clipboardController_.copy();
}

bool EditorController::cut() {
  return clipboardController_.cut();
}

bool EditorController::paste() {
  return clipboardController_.paste();
}

void EditorController::clearHistoryAndSelection() {
  undoStack_.clear();
  selection_.clear();
  codeFenceController_.exitEditMode();
  htmlBlockController_.exitEditMode();
  mathBlockController_.exitEditMode();
}

void EditorController::activateHit(HitTestResult hit) {
  if (!hit.isValid()) {
    selection_.clear();
    codeFenceController_.exitEditMode();
    htmlBlockController_.exitEditMode();
    mathBlockController_.exitEditMode();
    return;
  }
  if (session_) {
    fillSourceOffsetForTextHit(*session_, hit);
  }

  switch (hit.zone) {
    case HitTestResult::Zone::Code:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (codeFenceController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    case HitTestResult::Zone::Html:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (htmlBlockController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    case HitTestResult::Zone::Math:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (mathBlockController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    default:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      break;
  }
}

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
    CursorPosition cursor = tableCursorForLocation(*session_, command, storedCursor);
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (applied) {
      QVector<NodeId> refreshNodes;
      if (command.tableId.isValid()) {
        refreshNodes.push_back(command.tableId);
      }
      if (cursor.isValid() && !refreshNodes.contains(cursor.blockId)) {
        refreshNodes.push_back(cursor.blockId);
      }
      if (refreshNodes.isEmpty()) {
        brushQueue_.requestFullRefresh();
      } else {
        brushQueue_.requestBlocksRefresh(std::move(refreshNodes));
      }
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
      QVector<NodeId> refreshNodes = command.affectedNodes;
      if (!undo && command.nodeId.isValid() && !refreshNodes.contains(command.nodeId)) {
        refreshNodes.push_back(command.nodeId);
      }
      if (cursor.isValid() && !refreshNodes.contains(cursor.blockId)) {
        refreshNodes.push_back(cursor.blockId);
      }
      if (refreshNodes.isEmpty()) {
        brushQueue_.requestFullRefresh();
      } else {
        brushQueue_.requestBlocksRefresh(std::move(refreshNodes));
      }
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
      QVector<NodeId> refreshNodes = command.affectedNodes;
      if (cursor.isValid() && !refreshNodes.contains(cursor.blockId)) {
        refreshNodes.push_back(cursor.blockId);
      }
      if (refreshNodes.isEmpty()) {
        brushQueue_.requestFullRefresh();
      } else {
        brushQueue_.requestBlocksRefresh(std::move(refreshNodes));
      }
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
    const QVector<NodeId>& affectedNodes = command.affectedNodes;
    if (affectedNodes.isEmpty()) {
      brushQueue_.requestFullRefresh();
    } else {
      QVector<NodeId> refreshNodes = affectedNodes;
      if (cursor.isValid() && !refreshNodes.contains(cursor.blockId)) {
        refreshNodes.push_back(cursor.blockId);
      }
      brushQueue_.requestBlocksRefresh(std::move(refreshNodes));
    }
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
