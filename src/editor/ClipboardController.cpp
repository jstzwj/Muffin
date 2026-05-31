#include "editor/ClipboardController.h"

#include "editor/InputController.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

namespace muffin {

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

void ClipboardController::setInputController(InputController* inputController) {
  inputController_ = inputController;
}

bool ClipboardController::copy() {
  if (!inputController_ || !inputController_->hasEditableSelection()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(inputController_->selectedText());
  const QString markdown = inputController_->selectedMarkdown();
  if (!markdown.isEmpty()) {
    mimeData->setData(QStringLiteral("text/markdown"), markdown.toUtf8());
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
