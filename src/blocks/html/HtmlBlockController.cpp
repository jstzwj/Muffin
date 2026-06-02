#include "blocks/html/HtmlBlockController.h"

#include "app/DocumentSession.h"
#include "blocks/html/HtmlSanitizer.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

namespace muffin {

HtmlBlockController::HtmlBlockController(QObject* parent) : QObject(parent) {}

void HtmlBlockController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void HtmlBlockController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void HtmlBlockController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void HtmlBlockController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

NodeId HtmlBlockController::currentHtmlBlockId() const {
  if (!session_ || !selection_) {
    return {};
  }

  if (editingHtmlId_.isValid() && htmlBlockById(editingHtmlId_)) {
    return editingHtmlId_;
  }
  if (MarkdownNode* editingHtml = htmlBlockByIndex(editingHtmlIndex_)) {
    return editingHtml->id();
  }

  const HitTestResult hit = selection_->currentHit();
  if (hit.zone == HitTestResult::Zone::Html && hit.blockId.isValid()) {
    return hit.blockId;
  }

  if (selection_->hasCursor()) {
    const CursorPosition cursor = selection_->cursorPosition();
    MarkdownNode* node = session_->document().node(cursor.blockId);
    if (node && node->type() == BlockType::HtmlBlock) {
      return node->id();
    }
  }
  return {};
}

bool HtmlBlockController::isEditing() const {
  return editingHtmlId_.isValid() || editingHtmlIndex_ >= 0;
}

bool HtmlBlockController::enterEditMode() {
  const NodeId id = currentHtmlBlockId();
  if (!id.isValid()) {
    emit htmlCommandRejected(QStringLiteral("No HTML block is active."));
    return false;
  }

  MarkdownNode* html = htmlBlockById(id);
  if (!html) {
    return false;
  }
  editingHtmlId_ = id;
  editingHtmlIndex_ = htmlBlockIndexFor(id);
  if (selection_) {
    selection_->setCursorPosition(cursorFor(id, html->literal().size()));
  }
  return true;
}

bool HtmlBlockController::exitEditMode() {
  editingHtmlId_ = {};
  editingHtmlIndex_ = -1;
  return true;
}

bool HtmlBlockController::insertText(QString text) {
  if (text.isEmpty()) {
    return false;
  }

  return mutateCurrentHtmlBlock(QStringLiteral("Edit HTML Block"), EditTransaction::Kind::InsertText,
                                [text = std::move(text)](MarkdownNode& html, qsizetype& offset) {
                                  QString value = html.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  value.insert(offset, text);
                                  offset += text.size();
                                  html.setLiteral(value);
                                  return true;
                                });
}

bool HtmlBlockController::deleteBackward() {
  return mutateCurrentHtmlBlock(QStringLiteral("Backspace HTML Block"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& html, qsizetype& offset) {
                                  QString value = html.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset <= 0) {
                                    return true;
                                  }
                                  value.remove(offset - 1, 1);
                                  --offset;
                                  html.setLiteral(value);
                                  return true;
                                });
}

bool HtmlBlockController::deleteForward() {
  return mutateCurrentHtmlBlock(QStringLiteral("Delete HTML Block Text"), EditTransaction::Kind::DeleteText,
                                [](MarkdownNode& html, qsizetype& offset) {
                                  QString value = html.literal();
                                  offset = qBound<qsizetype>(0, offset, value.size());
                                  if (offset >= value.size()) {
                                    return true;
                                  }
                                  value.remove(offset, 1);
                                  html.setLiteral(value);
                                  return true;
                                });
}

bool HtmlBlockController::setHtml(QString html) {
  return mutateCurrentHtmlBlock(QStringLiteral("Set HTML Block"), EditTransaction::Kind::ReplaceDocumentText,
                                [html = std::move(html)](MarkdownNode& node, qsizetype& offset) {
                                  node.setLiteral(html);
                                  offset = qBound<qsizetype>(0, offset, html.size());
                                  return true;
                                });
}

QString HtmlBlockController::sanitizedPreview() const {
  MarkdownNode* html = currentHtmlBlock();
  return html ? HtmlSanitizer().sanitizedPreview(html->literal()) : QString();
}

bool HtmlBlockController::mutateCurrentHtmlBlock(
    QString label,
    EditTransaction::Kind kind,
    const std::function<bool(MarkdownNode&, qsizetype&)>& mutate) {
  if (!session_ || !selection_) {
    return false;
  }

  MarkdownNode* activeHtml = currentHtmlBlock();
  if (!activeHtml) {
    return false;
  }
  const bool wasEditing = isEditing();
  NodeId htmlId = activeHtml->id();
  const NodeId originalHtmlId = htmlId;
  const int htmlIndex = htmlBlockIndexFor(htmlId);

  const CursorPosition beforeCursor = selection_->hasCursor() ? selection_->cursorPosition() : cursorFor(htmlId, 0);
  std::unique_ptr<MarkdownNode> beforeNode = activeHtml->clone(CloneMode::PreserveIds);
  std::unique_ptr<MarkdownNode> afterNode = activeHtml->clone(CloneMode::PreserveIds);

  qsizetype nextOffset =
      beforeCursor.blockId == htmlId || (wasEditing && beforeCursor.blockId == editingHtmlId_)
          ? beforeCursor.text.textOffset
          : afterNode->literal().size();
  if (!mutate(*afterNode, nextOffset)) {
    return false;
  }
  if (MarkdownSerializer().serializeBlock(*beforeNode) == MarkdownSerializer().serializeBlock(*afterNode)) {
    return true;
  }

  if (!session_->applyNodeSnapshot(htmlId, BlockType::HtmlBlock, htmlIndex, *afterNode, true)) {
    return false;
  }
  if (MarkdownNode* reparsed = htmlBlockByIndex(htmlIndex)) {
    htmlId = reparsed->id();
    if (wasEditing) {
      editingHtmlId_ = htmlId;
      editingHtmlIndex_ = htmlIndex;
    }
  }
  const CursorPosition nextCursor = cursorFor(htmlId, nextOffset);
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(
        kind,
        std::move(label),
        ReplaceNodeCommand{
            originalHtmlId,
            BlockType::HtmlBlock,
            htmlIndex,
            std::move(beforeNode),
            std::move(afterNode),
            beforeCursor,
            nextCursor,
            QVector<NodeId>{originalHtmlId}}));
  }
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(htmlId);
  }
  return true;
}

MarkdownNode* HtmlBlockController::currentHtmlBlock() const {
  if (editingHtmlId_.isValid()) {
    if (MarkdownNode* html = htmlBlockById(editingHtmlId_)) {
      return html;
    }
  }
  if (MarkdownNode* html = htmlBlockByIndex(editingHtmlIndex_)) {
    return html;
  }
  return htmlBlockById(currentHtmlBlockId());
}

MarkdownNode* HtmlBlockController::htmlBlockById(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return nullptr;
  }
  MarkdownNode* node = session_->document().node(id);
  return node && node->type() == BlockType::HtmlBlock ? node : nullptr;
}

MarkdownNode* HtmlBlockController::htmlBlockByIndex(int targetIndex) const {
  if (!session_ || targetIndex < 0) {
    return nullptr;
  }
  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::HtmlBlock) {
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

MarkdownNode* HtmlBlockController::findHtmlBlockById(MarkdownNode& node, NodeId id) const {
  if (node.id() == id && node.type() == BlockType::HtmlBlock) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = findHtmlBlockById(*child, id)) {
      return found;
    }
  }
  return nullptr;
}

int HtmlBlockController::htmlBlockIndexFor(NodeId id) const {
  if (!session_ || !id.isValid()) {
    return -1;
  }
  int index = 0;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> int {
    if (node.type() == BlockType::HtmlBlock) {
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

CursorPosition HtmlBlockController::cursorFor(NodeId htmlId, qsizetype offset) const {
  CursorPosition cursor;
  MarkdownNode* html = htmlBlockById(htmlId);
  if (!html) {
    return cursor;
  }
  cursor.blockId = htmlId;
  cursor.text.nodeId = htmlId;
  cursor.text.textOffset = qBound<qsizetype>(0, offset, html->literal().size());
  return cursor;
}

}  // namespace muffin
