#include "blocks/literal/LiteralBlockController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

namespace muffin {

LiteralBlockController::LiteralBlockController(LiteralBlockSpec spec) : spec_(std::move(spec)) {}

void LiteralBlockController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
}

void LiteralBlockController::setRejectedHandler(RejectedFn handler) {
  rejectedHandler_ = std::move(handler);
}

BlockType LiteralBlockController::blockType() const {
  return spec_.blockType;
}

HitTestResult::Zone LiteralBlockController::hitZone() const {
  return spec_.hitZone;
}

QString LiteralBlockController::tabText() const {
  return spec_.tabText;
}

NodeId LiteralBlockController::currentBlockId() const {
  if (!ctx_.session || !ctx_.selection) {
    return {};
  }

  if (editingId_.isValid() && blockById(editingId_)) {
    return editingId_;
  }
  if (MarkdownNode* editingNode = blockByIndex(editingIndex_)) {
    return editingNode->id();
  }

  const HitTestResult hit = ctx_.selection->currentHit();
  if (hit.zone == spec_.hitZone && hit.blockId.isValid()) {
    return hit.blockId;
  }

  if (ctx_.selection->hasCursor()) {
    const CursorPosition cursor = ctx_.selection->cursorPosition();
    MarkdownNode* node = ctx_.session->document().node(cursor.blockId);
    if (node && node->type() == spec_.blockType) {
      return node->id();
    }
  }
  return {};
}

bool LiteralBlockController::isEditing() const {
  return editingId_.isValid() || editingIndex_ >= 0;
}

bool LiteralBlockController::enterEditMode() {
  const NodeId id = currentBlockId();
  if (!id.isValid()) {
    if (rejectedHandler_) {
      rejectedHandler_(spec_.rejectedReason);
    }
    return false;
  }

  MarkdownNode* node = blockById(id);
  if (!node) {
    return false;
  }
  editingId_ = id;
  editingIndex_ = blockIndexFor(id);
  if (ctx_.selection) {
    ctx_.selection->setCursorPosition(cursorFor(id, node->literal().size()));
  }
  return true;
}

bool LiteralBlockController::exitEditMode() {
  editingId_ = {};
  editingIndex_ = -1;
  return true;
}

bool LiteralBlockController::insertText(QString text) {
  if (text.isEmpty()) {
    return false;
  }
  return mutateCurrentBlock(spec_.editLabel, EditTransaction::Kind::InsertText,
                            [this, text = std::move(text)](MarkdownNode& node, qsizetype& offset) {
                              QString value = node.literal();
                              qsizetype selectionStart = 0;
                              qsizetype selectionEnd = 0;
                              if (currentSelectionRange(selectionStart, selectionEnd)) {
                                selectionStart = qBound<qsizetype>(0, selectionStart, value.size());
                                selectionEnd = qBound<qsizetype>(selectionStart, selectionEnd, value.size());
                                value.replace(selectionStart, selectionEnd - selectionStart, text);
                                offset = selectionStart + text.size();
                              } else {
                                offset = qBound<qsizetype>(0, offset, value.size());
                                value.insert(offset, text);
                                offset += text.size();
                              }
                              node.setLiteral(value);
                              return true;
                            });
}

bool LiteralBlockController::deleteBackward() {
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  return mutateCurrentBlock(spec_.backspaceLabel, EditTransaction::Kind::DeleteText,
                            [](MarkdownNode& node, qsizetype& offset) {
                              QString value = node.literal();
                              offset = qBound<qsizetype>(0, offset, value.size());
                              if (offset <= 0) {
                                return true;
                              }
                              value.remove(offset - 1, 1);
                              --offset;
                              node.setLiteral(value);
                              return true;
                            });
}

bool LiteralBlockController::deleteForward() {
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  return mutateCurrentBlock(spec_.deleteLabel, EditTransaction::Kind::DeleteText,
                            [](MarkdownNode& node, qsizetype& offset) {
                              QString value = node.literal();
                              offset = qBound<qsizetype>(0, offset, value.size());
                              if (offset >= value.size()) {
                                return true;
                              }
                              value.remove(offset, 1);
                              node.setLiteral(value);
                              return true;
                            });
}

bool LiteralBlockController::deleteSelection() {
  return mutateCurrentBlock(spec_.deleteSelectionLabel, EditTransaction::Kind::DeleteText,
                            [this](MarkdownNode& node, qsizetype& offset) {
                              qsizetype selectionStart = 0;
                              qsizetype selectionEnd = 0;
                              if (!currentSelectionRange(selectionStart, selectionEnd)) {
                                return false;
                              }
                              QString value = node.literal();
                              selectionStart = qBound<qsizetype>(0, selectionStart, value.size());
                              selectionEnd = qBound<qsizetype>(selectionStart, selectionEnd, value.size());
                              value.remove(selectionStart, selectionEnd - selectionStart);
                              offset = selectionStart;
                              node.setLiteral(value);
                              return true;
                            });
}

bool LiteralBlockController::setContent(QString content) {
  return mutateCurrentBlock(spec_.setContentLabel, EditTransaction::Kind::ReplaceDocumentText,
                            [content = std::move(content)](MarkdownNode& node, qsizetype& offset) {
                              node.setLiteral(content);
                              offset = qBound<qsizetype>(0, offset, content.size());
                              return true;
                            });
}

bool LiteralBlockController::mutateCurrentBlock(QString label, EditTransaction::Kind kind, const MutateFn& mutate) {
  const MarkdownNode* active = currentBlock();
  return active ? mutateBlock(active->id(), std::move(label), kind, mutate) : false;
}

bool LiteralBlockController::mutateBlock(NodeId requestedId, QString label, EditTransaction::Kind kind, const MutateFn& mutate) {
  if (!ctx_.session || !ctx_.selection) {
    return false;
  }

  MarkdownNode* active = blockById(requestedId);
  if (!active) {
    return false;
  }
  const bool wasEditing = isEditing();
  NodeId nodeId = active->id();
  const NodeId originalId = nodeId;
  const int nodeIndex = blockIndexFor(nodeId);
  const CursorPosition beforeCursor = ctx_.selection->hasCursor() ? ctx_.selection->cursorPosition() : cursorFor(nodeId, 0);
  const int beforeCursorIndex = blockIndexFor(beforeCursor.blockId);
  const bool cursorWasInTarget = beforeCursor.blockId == nodeId || (wasEditing && beforeCursor.blockId == editingId_);
  std::unique_ptr<MarkdownNode> beforeNode = active->clone(CloneMode::PreserveIds);
  std::unique_ptr<MarkdownNode> afterNode = active->clone(CloneMode::PreserveIds);

  qsizetype nextOffset = cursorWasInTarget ? beforeCursor.text.textOffset : afterNode->literal().size();
  if (!mutate(*afterNode, nextOffset)) {
    return false;
  }
  if (MarkdownSerializer().serializeBlock(*beforeNode) == MarkdownSerializer().serializeBlock(*afterNode)) {
    return true;
  }

  if (!ctx_.session->applyNodeSnapshot(nodeId, spec_.blockType, nodeIndex, *afterNode, true)) {
    return false;
  }
  if (MarkdownNode* reparsed = blockByIndex(nodeIndex)) {
    nodeId = reparsed->id();
    if (wasEditing) {
      editingId_ = nodeId;
      editingIndex_ = nodeIndex;
    }
  }
  CursorPosition nextCursor;
  if (cursorWasInTarget) {
    nextCursor = cursorFor(nodeId, nextOffset);
  } else if (MarkdownNode* reparsedCursorNode = blockByIndex(beforeCursorIndex)) {
    nextCursor = cursorFor(reparsedCursorNode->id(), beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedNode = ctx_.session->document().node(beforeCursor.blockId)) {
    nextCursor = beforeCursor;
    nextCursor.blockId = reparsedNode->id();
    nextCursor.text.nodeId = reparsedNode->id();
  }
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        kind,
        std::move(label),
        ReplaceNodeCommand{
            originalId,
            spec_.blockType,
            nodeIndex,
            std::move(beforeNode),
            std::move(afterNode),
            beforeCursor,
            nextCursor,
            QVector<NodeId>{originalId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(nodeId);
  }
  return true;
}

MarkdownNode* LiteralBlockController::currentBlock() const {
  if (editingId_.isValid()) {
    if (MarkdownNode* node = blockById(editingId_)) {
      return node;
    }
  }
  if (MarkdownNode* node = blockByIndex(editingIndex_)) {
    return node;
  }
  return blockById(currentBlockId());
}

MarkdownNode* LiteralBlockController::blockById(NodeId id) const {
  if (!ctx_.session || !id.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = ctx_.session->document().node(id);
  return node && node->type() == spec_.blockType ? node : nullptr;
}

MarkdownNode* LiteralBlockController::blockByIndex(int targetIndex) const {
  if (!ctx_.session || targetIndex < 0) {
    return nullptr;
  }
  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == spec_.blockType) {
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
  return visit(visit, ctx_.session->document().root());
}

int LiteralBlockController::blockIndexFor(NodeId id) const {
  if (!ctx_.session || !id.isValid()) {
    return -1;
  }
  int index = 0;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> int {
    if (node.type() == spec_.blockType) {
      if (node.id() == id) {
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
  return visit(visit, ctx_.session->document().root());
}

CursorPosition LiteralBlockController::cursorFor(NodeId id, qsizetype offset) const {
  CursorPosition cursor;
  MarkdownNode* node = blockById(id);
  if (!node) {
    return cursor;
  }
  cursor.blockId = id;
  cursor.text.nodeId = id;
  cursor.text.textOffset = qBound<qsizetype>(0, offset, node->literal().size());
  return cursor;
}

bool LiteralBlockController::currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const {
  if (!ctx_.selection || !ctx_.selection->hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }
  const SelectionRange range = ctx_.selection->selection();
  const NodeId id = currentBlockId();
  if (!id.isValid() || range.anchor.blockId != id || range.focus.blockId != id) {
    return false;
  }
  startOffset = range.startOffset();
  endOffset = range.endOffset();
  return startOffset < endOffset;
}

void LiteralBlockController::setEditingBlock(NodeId id, int index) {
  editingId_ = id;
  editingIndex_ = index;
}

}  // namespace muffin
