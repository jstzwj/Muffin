#include "blocks/math/MathBlockController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

namespace muffin {

MathBlockController::MathBlockController(QObject* parent) : QObject(parent) {}

void MathBlockController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void MathBlockController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void MathBlockController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void MathBlockController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

NodeId MathBlockController::currentMathBlockId() const {
  if (!session_ || !selection_) {
    return {};
  }

  if (editingMathId_.isValid() && mathBlockById(editingMathId_)) {
    return editingMathId_;
  }
  if (MarkdownNode* editingMath = mathBlockByIndex(editingMathIndex_)) {
    return editingMath->id();
  }

  const HitTestResult hit = selection_->currentHit();
  if (hit.zone == HitTestResult::Zone::Math && hit.blockId.isValid()) {
    return hit.blockId;
  }

  if (selection_->hasCursor()) {
    const CursorPosition cursor = selection_->cursorPosition();
    MarkdownNode* node = session_->document().node(cursor.blockId);
    if (node && node->type() == BlockType::MathBlock) {
      return node->id();
    }
  }
  return {};
}

bool MathBlockController::isEditing() const {
  return editingMathId_.isValid() || editingMathIndex_ >= 0;
}

bool MathBlockController::enterEditMode() {
  const NodeId id = currentMathBlockId();
  if (!id.isValid()) {
    emit mathCommandRejected(QStringLiteral("No math block is active."));
    return false;
  }

  MarkdownNode* math = mathBlockById(id);
  if (!math) {
    return false;
  }
  editingMathId_ = id;
  editingMathIndex_ = mathBlockIndexFor(id);
  if (selection_) {
    selection_->setCursorPosition(cursorFor(id, math->literal().size()));
  }
  return true;
}

bool MathBlockController::exitEditMode() {
  editingMathId_ = {};
  editingMathIndex_ = -1;
  return true;
}

bool MathBlockController::insertText(QString text) {
  if (text.isEmpty()) {
    return false;
  }

  return mutateCurrentMathBlock(QStringLiteral("Edit Math Block"), EditTransaction::Kind::InsertText,
                                [text = std::move(text)](MarkdownNode& math, qsizetype& offset) {
                                  QString value = math.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  value.insert(offset, text);
                                  offset += text.size();
                                  math.setLiteral(value);
                                  return true;
                                });
}

bool MathBlockController::deleteBackward() {
  return mutateCurrentMathBlock(QStringLiteral("Backspace Math Block"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& math, qsizetype& offset) {
                                  QString value = math.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset <= 0) {
                                    return true;
                                  }
                                  value.remove(offset - 1, 1);
                                  --offset;
                                  math.setLiteral(value);
                                  return true;
                                });
}

bool MathBlockController::deleteForward() {
  return mutateCurrentMathBlock(QStringLiteral("Delete Math Block Text"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& math, qsizetype& offset) {
                                  QString value = math.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset >= value.size()) {
                                    return true;
                                  }
                                  value.remove(offset, 1);
                                  math.setLiteral(value);
                                  return true;
                                });
}

bool MathBlockController::setTex(QString tex) {
  return mutateCurrentMathBlock(QStringLiteral("Set Math Block TeX"), EditTransaction::Kind::ReplaceDocumentText,
                                [tex = std::move(tex)](MarkdownNode& math, qsizetype& offset) {
                                  math.setLiteral(tex);
                                  offset = qBound<qsizetype>(0, offset, tex.size());
                                  return true;
                                });
}

bool MathBlockController::mutateCurrentMathBlock(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, qsizetype&)>& mutate) {
  if (!session_ || !selection_) {
    return false;
  }

  MarkdownNode* activeMath = currentMathBlock();
  if (!activeMath) {
    return false;
  }
  const bool wasEditing = isEditing();
  NodeId mathId = activeMath->id();
  const int mathIndex = mathBlockIndexFor(mathId);

  const CursorPosition beforeCursor = selection_->hasCursor() ? selection_->cursorPosition() : cursorFor(mathId, 0);
  const QString beforeText = session_->markdownText();
  auto rootCopy = session_->document().root().clone(CloneMode::PreserveIds);
  MarkdownNode* target = findMathBlockById(*rootCopy, mathId);
  if (!target && mathIndex >= 0) {
    int index = 0;
    const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
      if (node.type() == BlockType::MathBlock) {
        if (index == mathIndex) {
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
    target = visit(visit, *rootCopy);
  }
  if (!target) {
    return false;
  }

  qsizetype nextOffset =
      beforeCursor.blockId == mathId || (wasEditing && beforeCursor.blockId == editingMathId_)
          ? beforeCursor.text.textOffset
          : target->literal().size();
  if (!mutate(*target, nextOffset)) {
    return false;
  }

  MarkdownDocument nextDocument;
  nextDocument.setMarkdownText(beforeText, std::move(rootCopy));
  MarkdownSerializer serializer;
  QString nextText = serializer.serializeDocument(nextDocument);

  session_->applyMarkdownText(nextText, true);
  if (MarkdownNode* reparsed = mathBlockByIndex(mathIndex)) {
    mathId = reparsed->id();
    if (wasEditing) {
      editingMathId_ = mathId;
      editingMathIndex_ = mathIndex;
    }
  }
  const CursorPosition nextCursor = cursorFor(mathId, nextOffset);
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(kind, std::move(label), {beforeText, beforeCursor}, {nextText, nextCursor}));
  }
  if (brushQueue_) {
    brushQueue_->requestFullRefresh();
  }
  return true;
}

MarkdownNode* MathBlockController::currentMathBlock() const {
  if (editingMathId_.isValid()) {
    if (MarkdownNode* math = mathBlockById(editingMathId_)) {
      return math;
    }
  }
  if (MarkdownNode* math = mathBlockByIndex(editingMathIndex_)) {
    return math;
  }
  return mathBlockById(currentMathBlockId());
}

MarkdownNode* MathBlockController::mathBlockById(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = session_->document().node(id);
  return node && node->type() == BlockType::MathBlock ? node : nullptr;
}

MarkdownNode* MathBlockController::mathBlockByIndex(int targetIndex) const {
  if (!session_ || targetIndex < 0) {
    return nullptr;
  }
  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::MathBlock) {
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
  return visit(visit, session_->document().root());
}

MarkdownNode* MathBlockController::findMathBlockById(MarkdownNode& node, NodeId id) const {
  if (node.id() == id && node.type() == BlockType::MathBlock) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = findMathBlockById(*child, id)) {
      return found;
    }
  }
  return nullptr;
}

int MathBlockController::mathBlockIndexFor(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return -1;
  }
  int index = 0;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> int {
    if (node.type() == BlockType::MathBlock) {
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
  return visit(visit, session_->document().root());
}

CursorPosition MathBlockController::cursorFor(NodeId mathId, qsizetype offset) const {
  CursorPosition cursor;
  MarkdownNode* math = mathBlockById(mathId);
  if (!math) {
    return cursor;
  }
  cursor.blockId = mathId;
  cursor.text.nodeId = mathId;
  cursor.text.textOffset = qBound<qsizetype>(0, offset, math->literal().size());
  return cursor;
}

}  // namespace muffin
