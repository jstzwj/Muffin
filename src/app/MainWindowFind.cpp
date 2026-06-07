#include "app/MainWindow.h"

#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QPlainTextEdit>
#include <QTextCursor>

namespace muffin {

void MainWindow::showFindBar() {
  if (!findBar_) {
    return;
  }
  findBar_->setReplaceVisible(false);
  if (backend_->isSourceMode()) {
    const QString sel = backend_->selectedText();
    if (!sel.isEmpty()) {
      findBar_->setSearchText(sel);
    }
  }
  lastFindOffset_ = -1;
  lastFindText_.clear();
  findBar_->setVisible(true);
  findBar_->activateFind();
}

void MainWindow::showReplaceBar() {
  if (!findBar_) {
    return;
  }
  findBar_->setReplaceVisible(true);
  if (backend_->isSourceMode()) {
    const QString sel = backend_->selectedText();
    if (!sel.isEmpty()) {
      findBar_->setSearchText(sel);
    }
  }
  lastFindOffset_ = -1;
  lastFindText_.clear();
  findBar_->setVisible(true);
  findBar_->activateReplace();
}

void MainWindow::hideFindBar() {
  if (!findBar_) {
    return;
  }
  findBar_->setVisible(false);
}

void MainWindow::performFind(const QString& text, bool forward) {
  if (text.isEmpty()) {
    return;
  }

  if (text != lastFindText_) {
    lastFindText_ = text;
    lastFindOffset_ = -1;
  }

  if (backend_->isSourceMode()) {
    backend_->find(text, forward);
    return;
  }

  // Render mode: search the full markdown source text
  const QString& markdown = session_.markdownText();

  int startPos = 0;
  if (lastFindOffset_ >= 0) {
    startPos = forward ? static_cast<int>(lastFindOffset_) + 1
                       : static_cast<int>(lastFindOffset_) - 1;
  } else {
    if (editorController_.selection().hasCursor()) {
      startPos = static_cast<int>(
          qMin<qsizetype>(editorController_.selection().cursorPosition().text.sourceOffset,
                          markdown.size()));
    }
    if (!forward && startPos == 0) {
      startPos = markdown.size() - 1;
    }
  }

  int idx = -1;
  if (forward) {
    startPos = qMax(0, startPos);
    if (startPos < markdown.size()) {
      idx = markdown.indexOf(text, startPos);
    }
    if (idx < 0) {
      idx = markdown.indexOf(text, 0);
    }
  } else {
    startPos = qBound(0, startPos, markdown.size() - 1);
    idx = markdown.lastIndexOf(text, startPos);
    if (idx < 0) {
      idx = markdown.lastIndexOf(text, markdown.size() - 1);
    }
  }

  if (idx >= 0) {
    lastFindOffset_ = idx;
    MarkdownNode* block = session_.document().topLevelBlockAtOffset(idx);
    if (block) {
      renderView_->scrollToNode(block->id());
    }
    findBar_->setResultInfo(0, 0);
  } else {
    findBar_->setResultInfo(-1, 0);
  }
}

void MainWindow::performFindNext() {
  if (!findBar_) {
    return;
  }
  performFind(findBar_->searchText(), true);
}

void MainWindow::performFindPrevious() {
  if (!findBar_) {
    return;
  }
  performFind(findBar_->searchText(), false);
}

void MainWindow::performReplace(const QString& findText, const QString& replaceText) {
  if (findText.isEmpty()) {
    return;
  }
  if (backend_->isSourceMode()) {
    QTextCursor cursor = editor_->editor()->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
      cursor.insertText(replaceText);
      editor_->editor()->setTextCursor(cursor);
    }
    editor_->editor()->find(findText);
  } else {
    const QString& markdown = session_.markdownText();
    qsizetype startOffset = 0;
    if (editorController_.selection().hasCursor()) {
      startOffset = qMax<qsizetype>(0, editorController_.selection().cursorPosition().text.sourceOffset);
    }
    const int idx = markdown.indexOf(findText, startOffset);
    if (idx >= 0) {
      QString newText = markdown;
      newText.replace(idx, findText.size(), replaceText);
      session_.applyMarkdownText(newText, true);
      lastFindOffset_ = idx + replaceText.size();
    }
  }
}

void MainWindow::performReplaceAll(const QString& findText, const QString& replaceText) {
  if (findText.isEmpty()) {
    return;
  }
  QString text = backend_->fullText();
  text.replace(findText, replaceText);
  backend_->setFullText(text);
  lastFindOffset_ = -1;
}

}  // namespace muffin
