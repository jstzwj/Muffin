#include "app/SourceEditorBackend.h"

#include "document/SelectionSerializer.h"
#include "editor/SourceEditorWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace muffin {

SourceEditorBackend::SourceEditorBackend(SourceEditorWidget* editor) : editor_(editor) {}

QPlainTextEdit* SourceEditorBackend::plainEdit() const {
  return editor_->editor();
}

void SourceEditorBackend::cut() {
  plainEdit()->cut();
}

void SourceEditorBackend::copy() {
  plainEdit()->copy();
}

void SourceEditorBackend::paste() {
  plainEdit()->paste();
}

void SourceEditorBackend::deleteForward() {
  QTextCursor cursor = plainEdit()->textCursor();
  if (cursor.hasSelection()) {
    cursor.removeSelectedText();
  } else {
    cursor.deleteChar();
  }
  plainEdit()->setTextCursor(cursor);
}

void SourceEditorBackend::selectAll() {
  plainEdit()->selectAll();
}

void SourceEditorBackend::selectLine() {
  QTextCursor cursor = plainEdit()->textCursor();
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  plainEdit()->setTextCursor(cursor);
}

void SourceEditorBackend::selectFormatSpan() {
  QTextCursor cursor = plainEdit()->textCursor();
  cursor.select(QTextCursor::WordUnderCursor);
  plainEdit()->setTextCursor(cursor);
}

void SourceEditorBackend::moveLineUp() {
  QTextCursor cursor = plainEdit()->textCursor();
  QTextBlock currentBlock = cursor.block();
  QTextBlock prevBlock = currentBlock.previous();
  if (!prevBlock.isValid()) {
    return;
  }

  const int cursorPosInBlock = cursor.position() - currentBlock.position();
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::PreviousBlock);
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  const QString currentText = currentBlock.text();
  const QString prevText = prevBlock.text();
  cursor.insertText(currentText + QLatin1Char('\n') + prevText);
  cursor.endEditBlock();

  cursor.movePosition(QTextCursor::StartOfBlock);
  const int newPos = qMin(cursorPosInBlock, currentText.size());
  cursor.setPosition(cursor.block().position() + newPos);
  plainEdit()->setTextCursor(cursor);
}

void SourceEditorBackend::moveLineDown() {
  QTextCursor cursor = plainEdit()->textCursor();
  QTextBlock currentBlock = cursor.block();
  QTextBlock nextBlock = currentBlock.next();
  if (!nextBlock.isValid()) {
    return;
  }

  const int cursorPosInBlock = cursor.position() - currentBlock.position();
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  const QString currentText = currentBlock.text();
  const QString nextText = nextBlock.text();
  cursor.insertText(nextText + QLatin1Char('\n') + currentText);
  cursor.endEditBlock();

  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::EndOfBlock);
  const int newPos = qMin(cursorPosInBlock, currentText.size());
  cursor.setPosition(cursor.block().position() + newPos);
  plainEdit()->setTextCursor(cursor);
}

bool SourceEditorBackend::canUndo() const {
  return plainEdit()->document()->isUndoAvailable();
}

bool SourceEditorBackend::canRedo() const {
  return plainEdit()->document()->isRedoAvailable();
}

void SourceEditorBackend::undo() {
  plainEdit()->undo();
}

void SourceEditorBackend::redo() {
  plainEdit()->redo();
}

void SourceEditorBackend::copyAsPlainText() {
  const QTextCursor cursor = plainEdit()->textCursor();
  if (cursor.hasSelection()) {
    QApplication::clipboard()->setText(cursor.selectedText());
  }
}

void SourceEditorBackend::copyAsMarkdown() {
  const QTextCursor cursor = plainEdit()->textCursor();
  if (cursor.hasSelection()) {
    auto* mimeData = new QMimeData();
    mimeData->setText(cursor.selectedText());
    mimeData->setData(QStringLiteral("text/markdown"), cursor.selectedText().toUtf8());
    QApplication::clipboard()->setMimeData(mimeData);
  }
}

void SourceEditorBackend::copyAsHtml() {
  const QTextCursor cursor = plainEdit()->textCursor();
  if (cursor.hasSelection()) {
    const QString html = SelectionSerializer::renderMarkdownToHtml(cursor.selectedText());
    if (!html.isEmpty()) {
      auto* mimeData = new QMimeData();
      mimeData->setHtml(html);
      mimeData->setText(html);
      QApplication::clipboard()->setMimeData(mimeData);
    }
  }
}

void SourceEditorBackend::pasteAsPlainText() {
  const QString text = QApplication::clipboard()->text();
  if (!text.isEmpty()) {
    plainEdit()->insertPlainText(text);
  }
}

void SourceEditorBackend::toggleBold() {
  plainEdit()->insertPlainText(QStringLiteral("****"));
}

void SourceEditorBackend::toggleItalic() {
  plainEdit()->insertPlainText(QStringLiteral("**"));
}

void SourceEditorBackend::toggleCode() {
  plainEdit()->insertPlainText(QStringLiteral("``"));
}

void SourceEditorBackend::insertLink() {
  plainEdit()->insertPlainText(QStringLiteral("[](url)"));
}

bool SourceEditorBackend::hasSelection() const {
  return plainEdit()->textCursor().hasSelection();
}

bool SourceEditorBackend::isSourceMode() const {
  return true;
}

void SourceEditorBackend::find(const QString& text, bool forward) {
  QTextDocument::FindFlags flags;
  if (!forward) {
    flags |= QTextDocument::FindBackward;
  }
  plainEdit()->find(text, flags);
}

QString SourceEditorBackend::selectedText() const {
  const QTextCursor cursor = plainEdit()->textCursor();
  return cursor.hasSelection() ? cursor.selectedText() : QString();
}

void SourceEditorBackend::replaceSelection(const QString& text) {
  QTextCursor cursor = plainEdit()->textCursor();
  if (cursor.hasSelection()) {
    cursor.insertText(text);
    plainEdit()->setTextCursor(cursor);
  }
}

QString SourceEditorBackend::fullText() const {
  return plainEdit()->toPlainText();
}

void SourceEditorBackend::setFullText(const QString& text) {
  plainEdit()->setPlainText(text);
}

void SourceEditorBackend::centerCursor() {
  plainEdit()->centerCursor();
}

QString SourceEditorBackend::cursorStatusText() const {
  const QTextCursor cursor = plainEdit()->textCursor();
  return QStringLiteral("%1:%2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
}

}  // namespace muffin
