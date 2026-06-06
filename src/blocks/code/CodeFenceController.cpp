#include "blocks/code/CodeFenceController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"

namespace muffin {

namespace {

LiteralBlockSpec codeSpec() {
  return LiteralBlockSpec{
      BlockType::CodeFence,
      HitTestResult::Zone::Code,
      QStringLiteral("No code fence is active."),
      QStringLiteral("Edit Code Fence"),
      QStringLiteral("Backspace Code Fence"),
      QStringLiteral("Delete Code Fence Text"),
      QStringLiteral("Delete Code Fence Selection"),
      QStringLiteral("Set Code Fence Content"),
      QStringLiteral("\t")};
}

}  // namespace

CodeFenceController::CodeFenceController(QObject* parent) : QObject(parent), literal_(codeSpec()) {
  literal_.setRejectedHandler([this](QString reason) { emit codeCommandRejected(std::move(reason)); });
}

void CodeFenceController::setDocumentSession(DocumentSession* session) {
  session_ = session;
  literal_.setDocumentSession(session);
}

void CodeFenceController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
  literal_.setSelectionController(selection);
}

void CodeFenceController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
  literal_.setUndoStack(undoStack);
}

void CodeFenceController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
  literal_.setBrushQueue(brushQueue);
}

NodeId CodeFenceController::currentCodeFenceId() const {
  return literal_.currentBlockId();
}

bool CodeFenceController::isEditing() const {
  return literal_.isEditing();
}

bool CodeFenceController::enterEditMode() {
  return literal_.enterEditMode();
}

bool CodeFenceController::exitEditMode() {
  return literal_.exitEditMode();
}

bool CodeFenceController::insertText(QString text) {
  return literal_.insertText(std::move(text));
}

bool CodeFenceController::deleteBackward() {
  return literal_.deleteBackward();
}

bool CodeFenceController::deleteForward() {
  return literal_.deleteForward();
}

bool CodeFenceController::deleteSelection() {
  return literal_.deleteSelection();
}

bool CodeFenceController::setLanguage(QString language) {
  const MarkdownNode* activeCode = literal_.currentBlock();
  return activeCode ? setLanguageForCodeFence(activeCode->id(), std::move(language)) : false;
}

bool CodeFenceController::setLanguageFor(NodeId codeId, QString language) {
  return setLanguageForCodeFence(codeId, std::move(language));
}

bool CodeFenceController::setContent(QString content) {
  return literal_.setContent(std::move(content));
}

QString CodeFenceController::tabText() const {
  return literal_.tabText();
}

bool CodeFenceController::setLanguageForCodeFence(NodeId requestedCodeId, QString language) {
  if (!session_ || !selection_) {
    return false;
  }

  MarkdownNode* activeCode = literal_.blockById(requestedCodeId);
  if (!activeCode) {
    return false;
  }
  language = language.trimmed();
  const QString beforeLanguage = activeCode->codeLanguage();
  if (beforeLanguage == language) {
    return true;
  }

  const bool wasEditing = literal_.isEditing();
  NodeId codeId = activeCode->id();
  const NodeId originalCodeId = codeId;
  const int codeIndex = literal_.blockIndexFor(codeId);
  const CursorPosition beforeCursor = selection_->hasCursor() ? selection_->cursorPosition() : literal_.cursorFor(codeId, 0);
  const int beforeCursorCodeIndex = literal_.blockIndexFor(beforeCursor.blockId);
  const bool cursorWasInTarget = beforeCursor.blockId == codeId || (wasEditing && beforeCursor.blockId == literal_.currentBlockId());
  auto afterNode = activeCode->clone(CloneMode::PreserveIds);
  afterNode->setCodeLanguage(language);

  if (!session_->applyNodeSnapshot(codeId, BlockType::CodeFence, codeIndex, *afterNode, true)) {
    return false;
  }
  if (MarkdownNode* reparsed = literal_.blockByIndex(codeIndex)) {
    codeId = reparsed->id();
    if (wasEditing) {
      literal_.setEditingBlock(codeId, codeIndex);
    }
  }

  CursorPosition nextCursor;
  if (cursorWasInTarget) {
    nextCursor = literal_.cursorFor(codeId, beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedCursorCode = literal_.blockByIndex(beforeCursorCodeIndex)) {
    nextCursor = literal_.cursorFor(reparsedCursorCode->id(), beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedCursorNode = session_->document().node(beforeCursor.blockId)) {
    nextCursor = beforeCursor;
    nextCursor.blockId = reparsedCursorNode->id();
    nextCursor.text.nodeId = reparsedCursorNode->id();
  }
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_ && nextCursor.isValid()) {
    undoStack_->push(EditTransaction(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Set Code Fence Language"),
        SetNodeAttrCommand{
            originalCodeId,
            BlockType::CodeFence,
            codeIndex,
            NodeAttribute::CodeLanguage,
            beforeLanguage,
            language,
            beforeCursor,
            nextCursor,
            QVector<NodeId>{originalCodeId}}));
  }
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(codeId);
  }
  return true;
}

}  // namespace muffin
