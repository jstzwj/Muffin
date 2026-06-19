#include "editor/ClipboardController.h"

#include "document/DocumentSession.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "image/ImageInsertionPolicy.h"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QMimeData>
#include <QSettings>

namespace muffin {

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

void ClipboardController::setInputController(InputController* inputController) {
  inputController_ = inputController;
}

bool ClipboardController::copy() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() ||
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

  // editor/copyAsMarkdown (default on): put the markdown source on the clipboard (as the primary
  // text and a text/markdown MIME part) so rich paste targets keep the markup. Off, copy only the
  // decoded plain text.
  const bool preferMarkdown = QSettings().value(QStringLiteral("editor/copyAsMarkdown"), true).toBool();
  auto* mimeData = new QMimeData();
  if (preferMarkdown) {
    mimeData->setText(markdown.text.isEmpty() ? plainText.text : markdown.text);
    if (!markdown.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/markdown"), markdown.mimeData);
    }
    if (markdown.text.isEmpty() && !plainText.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/plain"), plainText.mimeData);
    }
  } else {
    mimeData->setText(plainText.text.isEmpty() ? markdown.text : plainText.text);
    if (!plainText.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/plain"), plainText.mimeData);
    }
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

  // Image data on the clipboard goes through the centralized insertion policy
  // (image/insertAction + the syntax/apply checkboxes) instead of a hardcoded
  // save-to-document-dir. Non-image pastes fall through to plain-text insertion.
  const QMimeData* mimeData = QApplication::clipboard()->mimeData();
  if (mimeData && mimeData->hasImage() && ctx_.hasSession()) {
    const QImage image = qvariant_cast<QImage>(mimeData->imageData());
    if (!image.isNull()) {
      muffin::ImageInsertRequest req;
      req.pastedImage = image;
      req.documentPath = ctx_.session->filePath();
      req.documentText = ctx_.session->markdownText();
      QSettings settings;
      const muffin::ImageInsertResult res = muffin::ImageInsertionPolicy::resolveHref(req, settings, nullptr);
      if (res.ok) {
        return inputController_->insertText(QStringLiteral("![image](%1)").arg(res.href));
      }
    }
  }

  const QString text = QApplication::clipboard()->text();
  return text.isEmpty() ? false : inputController_->insertText(text);
}

bool ClipboardController::copyAsPlainText() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() ||
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
  if (!ctx_.hasSession() || !ctx_.hasCursor() ||
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
  if (!ctx_.hasSession() || !ctx_.hasCursor() ||
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
