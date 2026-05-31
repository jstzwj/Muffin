#include "editor/ClipboardController.h"

#include "app/DocumentSession.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

namespace muffin {

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

void ClipboardController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void ClipboardController::setSelectionController(SelectionController* selectionController) {
  selectionController_ = selectionController;
}

void ClipboardController::setInputController(InputController* inputController) {
  inputController_ = inputController;
}

bool ClipboardController::copy() {
  if (!session_ || !selectionController_ || !selectionController_->hasCursor() ||
      selectionController_->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult markdown = selectionSerializer_.exportSelection(
      SelectionExportRequest{&session_->document(), selectionController_->selection(), SelectionExportFormat::Markdown});
  const SelectionExportResult plainText = selectionSerializer_.exportSelection(
      SelectionExportRequest{&session_->document(), selectionController_->selection(), SelectionExportFormat::PlainText});
  if (markdown.text.isEmpty() && plainText.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(markdown.text.isEmpty() ? plainText.text : markdown.text);
  if (!markdown.text.isEmpty()) {
    mimeData->setData(QStringLiteral("text/markdown"), markdown.mimeData);
  }
  if (markdown.text.isEmpty() && !plainText.text.isEmpty()) {
    mimeData->setData(QStringLiteral("text/plain"), plainText.mimeData);
  }
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::cut() {
  if (!copy()) {
    return false;
  }
  return inputController_->deleteSelection();
}

bool ClipboardController::paste() {
  if (!inputController_) {
    return false;
  }

  const QString text = QApplication::clipboard()->text();
  return text.isEmpty() ? false : inputController_->insertText(text);
}

}  // namespace muffin
