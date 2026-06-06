#include "blocks/html/HtmlBlockController.h"

#include "blocks/html/HtmlSanitizer.h"
#include "document/MarkdownNode.h"

namespace muffin {

namespace {

LiteralBlockSpec htmlSpec() {
  return LiteralBlockSpec{
      BlockType::HtmlBlock,
      HitTestResult::Zone::Html,
      QStringLiteral("No HTML block is active."),
      QStringLiteral("Edit HTML Block"),
      QStringLiteral("Backspace HTML Block"),
      QStringLiteral("Delete HTML Block Text"),
      QStringLiteral("Delete HTML Block Selection"),
      QStringLiteral("Set HTML Block"),
      QStringLiteral("  ")};
}

}  // namespace

HtmlBlockController::HtmlBlockController(QObject* parent) : QObject(parent), literal_(htmlSpec()) {
  literal_.setRejectedHandler([this](QString reason) { emit htmlCommandRejected(std::move(reason)); });
}

void HtmlBlockController::setDocumentSession(DocumentSession* session) {
  literal_.setDocumentSession(session);
}

void HtmlBlockController::setSelectionController(SelectionController* selection) {
  literal_.setSelectionController(selection);
}

void HtmlBlockController::setUndoStack(UndoStack* undoStack) {
  literal_.setUndoStack(undoStack);
}

void HtmlBlockController::setBrushQueue(BrushQueue* brushQueue) {
  literal_.setBrushQueue(brushQueue);
}

NodeId HtmlBlockController::currentHtmlBlockId() const {
  return literal_.currentBlockId();
}

bool HtmlBlockController::isEditing() const {
  return literal_.isEditing();
}

bool HtmlBlockController::enterEditMode() {
  return literal_.enterEditMode();
}

bool HtmlBlockController::exitEditMode() {
  return literal_.exitEditMode();
}

bool HtmlBlockController::insertText(QString text) {
  return literal_.insertText(std::move(text));
}

bool HtmlBlockController::deleteBackward() {
  return literal_.deleteBackward();
}

bool HtmlBlockController::deleteForward() {
  return literal_.deleteForward();
}

bool HtmlBlockController::deleteSelection() {
  return literal_.deleteSelection();
}

bool HtmlBlockController::setHtml(QString html) {
  return literal_.setContent(std::move(html));
}

QString HtmlBlockController::sanitizedPreview() const {
  MarkdownNode* html = literal_.currentBlock();
  return html ? HtmlSanitizer().sanitizedPreview(html->literal()) : QString();
}

QString HtmlBlockController::tabText() const {
  return literal_.tabText();
}

}  // namespace muffin
