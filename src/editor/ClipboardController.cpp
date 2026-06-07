#include "editor/ClipboardController.h"

#include "app/DocumentSession.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

namespace muffin {

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

void ClipboardController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
}

void ClipboardController::setInputController(InputController* inputController) {
  inputController_ = inputController;
}

bool ClipboardController::copy() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() ||
      ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult markdown = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Markdown});
  const SelectionExportResult plainText = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::PlainText});
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

bool ClipboardController::copyAsPlainText() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() ||
      ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::PlainText});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(result.text);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::copyAsMarkdown() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() ||
      ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Markdown});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(result.text);
  mimeData->setData(QStringLiteral("text/markdown"), result.mimeData);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::copyAsHtml() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() ||
      ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Html});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setHtml(result.text);
  mimeData->setText(result.text);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::pasteAsPlainText() {
  if (!inputController_) {
    return false;
  }

  const QString text = QApplication::clipboard()->text();
  return text.isEmpty() ? false : inputController_->insertText(text);
}

}  // namespace muffin
