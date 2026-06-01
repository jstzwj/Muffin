#include "blocks/code/CodeFenceController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

namespace muffin {

CodeFenceController::CodeFenceController(QObject* parent) : QObject(parent) {}

void CodeFenceController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void CodeFenceController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void CodeFenceController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void CodeFenceController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

NodeId CodeFenceController::currentCodeFenceId() const {
  if (!session_ || !selection_) {
    return {};
  }

  if (editingCodeId_.isValid() && codeFenceById(editingCodeId_)) {
    return editingCodeId_;
  }
  if (MarkdownNode* editingCode = codeFenceByIndex(editingCodeIndex_)) {
    return editingCode->id();
  }

  const HitTestResult hit = selection_->currentHit();
  if (hit.zone == HitTestResult::Zone::Code && hit.blockId.isValid()) {
    return hit.blockId;
  }

  if (selection_->hasCursor()) {
    const CursorPosition cursor = selection_->cursorPosition();
    MarkdownNode* node = session_->document().node(cursor.blockId);
    if (node && node->type() == BlockType::CodeFence) {
      return node->id();
    }
  }
  return {};
}

bool CodeFenceController::isEditing() const {
  return editingCodeId_.isValid() || editingCodeIndex_ >= 0;
}

bool CodeFenceController::enterEditMode() {
  const NodeId id = currentCodeFenceId();
  if (!id.isValid()) {
    emit codeCommandRejected(QStringLiteral("No code fence is active."));
    return false;
  }

  MarkdownNode* code = codeFenceById(id);
  if (!code) {
    return false;
  }
  editingCodeId_ = id;
  editingCodeIndex_ = codeFenceIndexFor(id);
  if (selection_) {
    selection_->setCursorPosition(cursorFor(id, code->literal().size()));
  }
  return true;
}

bool CodeFenceController::exitEditMode() {
  editingCodeId_ = {};
  editingCodeIndex_ = -1;
  return true;
}

bool CodeFenceController::insertText(QString text) {
  if (text.isEmpty()) {
    return false;
  }

  return mutateCurrentCodeFence(QStringLiteral("Edit Code Fence"), EditTransaction::Kind::InsertText,
                                [this, text = std::move(text)](MarkdownNode& code, qsizetype& offset) {
                                  QString value = code.literal();
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
                                  code.setLiteral(value);
                                  return true;
                                });
}

bool CodeFenceController::deleteBackward() {
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
  }
  return mutateCurrentCodeFence(QStringLiteral("Backspace Code Fence"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& code, qsizetype& offset) {
                                  QString value = code.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset <= 0) {
                                    return true;
                                  }
                                  value.remove(offset - 1, 1);
                                  --offset;
                                  code.setLiteral(value);
                                  return true;
                                });
}

bool CodeFenceController::deleteForward() {
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
  }
  return mutateCurrentCodeFence(QStringLiteral("Delete Code Fence Text"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& code, qsizetype& offset) {
                                  QString value = code.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset >= value.size()) {
                                    return true;
                                  }
                                  value.remove(offset, 1);
                                  code.setLiteral(value);
                                  return true;
                                });
}

bool CodeFenceController::deleteSelection() {
  return mutateCurrentCodeFence(QStringLiteral("Delete Code Fence Selection"), EditTransaction::Kind::DeleteText,
                                [this](MarkdownNode& code, qsizetype& offset) {
                                  qsizetype selectionStart = 0;
                                  qsizetype selectionEnd = 0;
                                  if (!currentSelectionRange(selectionStart, selectionEnd)) {
                                    return false;
                                  }
                                  QString value = code.literal();
                                  selectionStart = qBound<qsizetype>(0, selectionStart, value.size());
                                  selectionEnd = qBound<qsizetype>(selectionStart, selectionEnd, value.size());
                                  value.remove(selectionStart, selectionEnd - selectionStart);
                                  offset = selectionStart;
                                  code.setLiteral(value);
                                  return true;
                                });
}

bool CodeFenceController::setLanguage(QString language) {
  return mutateCurrentCodeFence(QStringLiteral("Set Code Fence Language"), EditTransaction::Kind::ReplaceDocumentText,
                                [language = std::move(language).trimmed()](MarkdownNode& code, qsizetype&) {
                                  code.setCodeLanguage(language);
                                  return true;
                                });
}

bool CodeFenceController::setLanguageFor(NodeId codeId, QString language) {
  return mutateCodeFence(codeId, QStringLiteral("Set Code Fence Language"), EditTransaction::Kind::ReplaceDocumentText,
                         [language = std::move(language).trimmed()](MarkdownNode& code, qsizetype&) {
                           code.setCodeLanguage(language);
                           return true;
                         });
}

bool CodeFenceController::setContent(QString content) {
  return mutateCurrentCodeFence(QStringLiteral("Set Code Fence Content"), EditTransaction::Kind::ReplaceDocumentText,
                                [content = std::move(content)](MarkdownNode& code, qsizetype& offset) {
                                  code.setLiteral(content);
                                  offset = qBound<qsizetype>(0, offset, content.size());
                                  return true;
                                });
}

bool CodeFenceController::mutateCurrentCodeFence(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, qsizetype&)>& mutate) {
  const MarkdownNode* activeCode = currentCodeFence();
  return activeCode ? mutateCodeFence(activeCode->id(), std::move(label), kind, mutate) : false;
}

bool CodeFenceController::mutateCodeFence(
    NodeId requestedCodeId,
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, qsizetype&)>& mutate) {
  if (!session_ || !selection_) {
    return false;
  }

  MarkdownNode* activeCode = codeFenceById(requestedCodeId);
  if (!activeCode) {
    return false;
  }
  const bool wasEditing = isEditing();
  NodeId codeId = activeCode->id();
  const int codeIndex = codeFenceIndexFor(codeId);
  const CursorPosition beforeCursor = selection_->hasCursor() ? selection_->cursorPosition() : cursorFor(codeId, 0);
  const int beforeCursorCodeIndex = codeFenceIndexFor(beforeCursor.blockId);
  const bool cursorWasInTarget = beforeCursor.blockId == codeId || (wasEditing && beforeCursor.blockId == editingCodeId_);
  const QString beforeText = session_->markdownText();
  auto rootCopy = session_->document().root().clone(CloneMode::PreserveIds);
  MarkdownNode* target = findCodeFenceById(*rootCopy, codeId);
  if (!target && codeIndex >= 0) {
    int index = 0;
    const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
      if (node.type() == BlockType::CodeFence) {
        if (index == codeIndex) {
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

  qsizetype nextOffset = cursorWasInTarget ? beforeCursor.text.textOffset : target->literal().size();
  if (!mutate(*target, nextOffset)) {
    return false;
  }

  MarkdownDocument nextDocument;
  nextDocument.setMarkdownText(beforeText, std::move(rootCopy));
  MarkdownSerializer serializer;
  QString nextText = serializer.serializeDocument(nextDocument);

  session_->applyMarkdownText(nextText, true);
  if (MarkdownNode* reparsed = codeFenceByIndex(codeIndex)) {
    codeId = reparsed->id();
    if (wasEditing) {
      editingCodeId_ = codeId;
      editingCodeIndex_ = codeIndex;
    }
  }
  CursorPosition nextCursor;
  if (cursorWasInTarget) {
    nextCursor = cursorFor(codeId, nextOffset);
  } else if (MarkdownNode* reparsedCursorCode = codeFenceByIndex(beforeCursorCodeIndex)) {
    nextCursor = cursorFor(reparsedCursorCode->id(), beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedCursorNode = session_->document().node(beforeCursor.blockId)) {
    nextCursor = beforeCursor;
    nextCursor.blockId = reparsedCursorNode->id();
    nextCursor.text.nodeId = reparsedCursorNode->id();
  }
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

MarkdownNode* CodeFenceController::currentCodeFence() const {
  if (editingCodeId_.isValid()) {
    if (MarkdownNode* code = codeFenceById(editingCodeId_)) {
      return code;
    }
  }
  if (MarkdownNode* code = codeFenceByIndex(editingCodeIndex_)) {
    return code;
  }
  return codeFenceById(currentCodeFenceId());
}

MarkdownNode* CodeFenceController::codeFenceById(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = session_->document().node(id);
  return node && node->type() == BlockType::CodeFence ? node : nullptr;
}

MarkdownNode* CodeFenceController::codeFenceByIndex(int targetIndex) const {
  if (!session_ || targetIndex < 0) {
    return nullptr;
  }
  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::CodeFence) {
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

MarkdownNode* CodeFenceController::findCodeFenceById(MarkdownNode& node, NodeId id) const {
  if (node.id() == id && node.type() == BlockType::CodeFence) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = findCodeFenceById(*child, id)) {
      return found;
    }
  }
  return nullptr;
}

int CodeFenceController::codeFenceIndexFor(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return -1;
  }
  int index = 0;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> int {
    if (node.type() == BlockType::CodeFence) {
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

CursorPosition CodeFenceController::cursorFor(NodeId codeId, qsizetype offset) const {
  CursorPosition cursor;
  MarkdownNode* code = codeFenceById(codeId);
  if (!code) {
    return cursor;
  }
  cursor.blockId = codeId;
  cursor.text.nodeId = codeId;
  cursor.text.textOffset = qBound<qsizetype>(0, offset, code->literal().size());
  return cursor;
}

bool CodeFenceController::currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const {
  if (!selection_ || !selection_->hasCursor() || selection_->selection().isCollapsed()) {
    return false;
  }
  const SelectionRange range = selection_->selection();
  const NodeId codeId = currentCodeFenceId();
  if (!codeId.isValid() || range.anchor.blockId != codeId || range.focus.blockId != codeId) {
    return false;
  }
  startOffset = range.startOffset();
  endOffset = range.endOffset();
  return startOffset < endOffset;
}

}  // namespace muffin
