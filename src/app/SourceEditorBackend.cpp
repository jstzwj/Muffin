#include "app/SourceEditorBackend.h"

#include "editor/SourceEditorWidget.h"
#include "projection/SelectionSerializer.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>

namespace muffin {

SourceEditorBackend::SourceEditorBackend(SourceEditorWidget* editor) : editor_(editor) {}

bool SourceEditorBackend::maybeCopyWholeLine(bool cut) {
  if (!QSettings().value(QStringLiteral("editor/copyLineNoSelection"), false).toBool() ||
      editor_->hasSelection()) {
    return false;
  }
  const qsizetype saved = editor_->cursorPosition();
  editor_->selectLine();
  QApplication::clipboard()->setText(editor_->selectedText());
  if (cut) editor_->deleteLineContent();
  else editor_->setCursorPosition(saved);
  return true;
}

void SourceEditorBackend::cut() {
  if (editor_->isReadOnly()) return;
  if (maybeCopyWholeLine(true)) return;
  if (!editor_->hasSelection()) return;
  QApplication::clipboard()->setText(editor_->selectedText());
  editor_->deleteForward();
}

void SourceEditorBackend::copy() {
  if (maybeCopyWholeLine(false)) return;
  if (editor_->hasSelection()) QApplication::clipboard()->setText(editor_->selectedText());
}

void SourceEditorBackend::paste() {
  if (!editor_->isReadOnly()) editor_->insertText(QApplication::clipboard()->text());
}

void SourceEditorBackend::deleteRange(DeleteTarget target) {
  switch (target) {
    case DeleteTarget::Forward: editor_->deleteForward(); break;
    case DeleteTarget::Backward: editor_->deleteBackward(); break;
    case DeleteTarget::Word:
    case DeleteTarget::FormatSpan: editor_->deleteWord(); break;
    case DeleteTarget::Line: editor_->deleteLineContent(); break;
    case DeleteTarget::Block: editor_->deleteWholeLine(); break;
  }
}

void SourceEditorBackend::selectAll() { editor_->selectAll(); }
void SourceEditorBackend::selectLine() { editor_->selectLine(); }
void SourceEditorBackend::selectBlock() { editor_->selectLine(); }
void SourceEditorBackend::selectWord() { editor_->selectWord(); }
void SourceEditorBackend::selectFormatSpan() { editor_->selectWord(); }
void SourceEditorBackend::moveDocumentStart() { editor_->moveDocumentStart(); }
void SourceEditorBackend::moveDocumentEnd() { editor_->moveDocumentEnd(); }
void SourceEditorBackend::moveLineStart() { editor_->moveLineStart(); }
void SourceEditorBackend::moveLineEnd() { editor_->moveLineEnd(); }
void SourceEditorBackend::selectNextOccurrence() { editor_->selectNextOccurrence(); }
void SourceEditorBackend::moveLineUp() { editor_->moveCurrentLineUp(); }
void SourceEditorBackend::moveLineDown() { editor_->moveCurrentLineDown(); }
bool SourceEditorBackend::canUndo() const { return editor_->canUndo(); }
bool SourceEditorBackend::canRedo() const { return editor_->canRedo(); }
void SourceEditorBackend::undo() { editor_->undo(); }
void SourceEditorBackend::redo() { editor_->redo(); }

void SourceEditorBackend::copyAsPlainText() { copy(); }

void SourceEditorBackend::copyAsMarkdown() {
  if (!editor_->hasSelection()) return;
  const QString selected = editor_->selectedText();
  auto* mime = new QMimeData();
  mime->setText(selected);
  mime->setData(QStringLiteral("text/markdown"), selected.toUtf8());
  QApplication::clipboard()->setMimeData(mime);
}

void SourceEditorBackend::copyAsHtml() {
  if (!editor_->hasSelection()) return;
  const QString html = SelectionSerializer::renderMarkdownToHtml(editor_->selectedText());
  if (html.isEmpty()) return;
  auto* mime = new QMimeData();
  mime->setHtml(html);
  mime->setText(html);
  QApplication::clipboard()->setMimeData(mime);
}

void SourceEditorBackend::pasteAsPlainText() { paste(); }
void SourceEditorBackend::toggleBold() { editor_->insertText(QStringLiteral("****")); }
void SourceEditorBackend::toggleItalic() { editor_->insertText(QStringLiteral("**")); }
void SourceEditorBackend::toggleCode() { editor_->insertText(QStringLiteral("``")); }
void SourceEditorBackend::toggleStrikethrough() { editor_->insertText(QStringLiteral("~~~~")); }
void SourceEditorBackend::toggleInlineMath() { editor_->insertText(QStringLiteral("$$")); }
void SourceEditorBackend::toggleUnderline() { editor_->insertText(QStringLiteral("<u></u>")); }
void SourceEditorBackend::insertLink() { editor_->insertText(QStringLiteral("[](url)")); }
void SourceEditorBackend::insertImage() { editor_->insertText(QStringLiteral("![alt](url)")); }
void SourceEditorBackend::clearFormatting() {}
bool SourceEditorBackend::hasSelection() const { return editor_->hasSelection(); }
bool SourceEditorBackend::isSourceMode() const { return true; }

void SourceEditorBackend::find(const QString& text, bool forward) {
  if (text.isEmpty()) return;
  qsizetype found = forward
      ? editor_->findText(text, editor_->selectionEnd())
      : editor_->findTextBackward(text, qMax<qsizetype>(0, editor_->selectionStart() - 1));
  if (found < 0) {
    found = forward ? editor_->findText(text) : editor_->findTextBackward(text);
  }
  if (found >= 0) editor_->setSelection(found, found + text.size());
}

QString SourceEditorBackend::selectedText() const { return editor_->selectedText(); }
void SourceEditorBackend::replaceSelection(const QString& text) { editor_->replaceSelection(text); }
QString SourceEditorBackend::fullText() const { return editor_->text(); }

void SourceEditorBackend::setFullText(const QString& text) {
  editor_->selectAll();
  editor_->insertText(text);
}

void SourceEditorBackend::centerCursor() { editor_->centerCursor(); }

QString SourceEditorBackend::cursorStatusText() const {
  return QStringLiteral("%1:%2").arg(editor_->cursorLine()).arg(editor_->cursorColumn());
}

}  // namespace muffin
