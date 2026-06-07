#include "app/RenderEditorBackend.h"

#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"

namespace muffin {

RenderEditorBackend::RenderEditorBackend(EditorController& controller, DocumentSession& session, EditorView* view)
    : controller_(controller), session_(session), view_(view) {}

void RenderEditorBackend::cut() {
  controller_.clipboardController().cut();
}

void RenderEditorBackend::copy() {
  controller_.clipboardController().copy();
}

void RenderEditorBackend::paste() {
  controller_.clipboardController().paste();
}

void RenderEditorBackend::deleteForward() {
  controller_.inputController().deleteForward();
}

void RenderEditorBackend::selectAll() {
  controller_.selectAll();
}

void RenderEditorBackend::selectLine() {
  controller_.selectCurrentBlock();
}

void RenderEditorBackend::selectFormatSpan() {
  controller_.selectCurrentFormatSpan();
}

void RenderEditorBackend::moveLineUp() {
  controller_.moveBlockUp();
}

void RenderEditorBackend::moveLineDown() {
  controller_.moveBlockDown();
}

bool RenderEditorBackend::canUndo() const {
  return controller_.canUndo();
}

bool RenderEditorBackend::canRedo() const {
  return controller_.canRedo();
}

void RenderEditorBackend::undo() {
  controller_.undo();
}

void RenderEditorBackend::redo() {
  controller_.redo();
}

void RenderEditorBackend::copyAsPlainText() {
  controller_.clipboardController().copyAsPlainText();
}

void RenderEditorBackend::copyAsMarkdown() {
  controller_.clipboardController().copyAsMarkdown();
}

void RenderEditorBackend::copyAsHtml() {
  controller_.clipboardController().copyAsHtml();
}

void RenderEditorBackend::pasteAsPlainText() {
  controller_.clipboardController().pasteAsPlainText();
}

void RenderEditorBackend::toggleBold() {
  controller_.stylizeController().toggleBold();
}

void RenderEditorBackend::toggleItalic() {
  controller_.stylizeController().toggleItalic();
}

void RenderEditorBackend::toggleCode() {
  controller_.stylizeController().toggleCode();
}

void RenderEditorBackend::insertLink() {
  controller_.stylizeController().insertLink();
}

bool RenderEditorBackend::hasSelection() const {
  return controller_.selection().hasCursor() && !controller_.selection().selection().isCollapsed();
}

bool RenderEditorBackend::isSourceMode() const {
  return false;
}

void RenderEditorBackend::find(const QString& text, bool forward) {
  // Render mode: search the full markdown source text
  const QString& markdown = session_.markdownText();
  qsizetype lastOffset = -1;

  qsizetype startPos = 0;
  if (controller_.selection().hasCursor()) {
    startPos = qMin<qsizetype>(controller_.selection().cursorPosition().text.sourceOffset, markdown.size());
    if (!forward && startPos == 0) {
      startPos = markdown.size() - 1;
    }
  }

  int idx = -1;
  if (forward) {
    startPos = qMax<qsizetype>(0, startPos);
    if (startPos < markdown.size()) {
      idx = markdown.indexOf(text, startPos);
    }
    if (idx < 0) {
      idx = markdown.indexOf(text, 0);
    }
  } else {
    startPos = qBound<qsizetype>(0, startPos, markdown.size() - 1);
    idx = markdown.lastIndexOf(text, startPos);
    if (idx < 0) {
      idx = markdown.lastIndexOf(text, markdown.size() - 1);
    }
  }

  if (idx >= 0 && view_) {
    MarkdownNode* block = session_.document().topLevelBlockAtOffset(idx);
    if (block) {
      view_->scrollToNode(block->id());
    }
  }
}

QString RenderEditorBackend::selectedText() const {
  // Render mode doesn't have a simple selected text extraction
  return {};
}

void RenderEditorBackend::replaceSelection(const QString& text) {
  Q_UNUSED(text);
  // Render mode replace is handled differently via source text
}

QString RenderEditorBackend::fullText() const {
  return session_.markdownText();
}

void RenderEditorBackend::setFullText(const QString& text) {
  session_.applyMarkdownText(text, true);
}

void RenderEditorBackend::centerCursor() {
  if (view_) {
    view_->scrollToCursorCentered();
  }
}

QString RenderEditorBackend::cursorStatusText() const {
  // The render-mode cursor status is set via updateRenderCursorStatus
  // and stored in MainWindow::renderCursorStatus_
  return {};
}

}  // namespace muffin
