#include "app/MainWindow.h"

#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace muffin {

void MainWindow::moveSourceLineUp() {
  QTextCursor cursor = editor_->editor()->textCursor();
  QTextBlock currentBlock = cursor.block();
  QTextBlock prevBlock = currentBlock.previous();
  if (!prevBlock.isValid()) {
    return;
  }

  const int cursorPosInBlock = cursor.position() - currentBlock.position();
  cursor.beginEditBlock();
  // Select previous block
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::PreviousBlock);
  cursor.movePosition(QTextCursor::StartOfBlock);
  // Select both blocks
  cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  const QString currentText = currentBlock.text();
  const QString prevText = prevBlock.text();
  cursor.insertText(currentText + QLatin1Char('\n') + prevText);
  cursor.endEditBlock();

  // Position cursor in the moved (now upper) block
  cursor.movePosition(QTextCursor::StartOfBlock);
  const int newPos = qMin(cursorPosInBlock, currentText.size());
  cursor.setPosition(cursor.block().position() + newPos);
  editor_->editor()->setTextCursor(cursor);
}

void MainWindow::moveSourceLineDown() {
  QTextCursor cursor = editor_->editor()->textCursor();
  QTextBlock currentBlock = cursor.block();
  QTextBlock nextBlock = currentBlock.next();
  if (!nextBlock.isValid()) {
    return;
  }

  const int cursorPosInBlock = cursor.position() - currentBlock.position();
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::StartOfBlock);
  // Select both blocks: current + next
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
  cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  const QString currentText = currentBlock.text();
  const QString nextText = nextBlock.text();
  cursor.insertText(nextText + QLatin1Char('\n') + currentText);
  cursor.endEditBlock();

  // Position cursor in the moved (now lower) block
  cursor.movePosition(QTextCursor::StartOfBlock);
  cursor.movePosition(QTextCursor::EndOfBlock);
  const int newPos = qMin(cursorPosInBlock, currentText.size());
  cursor.setPosition(cursor.block().position() + newPos);
  editor_->editor()->setTextCursor(cursor);
}

void MainWindow::showFindBar() {
  if (!findBar_) {
    return;
  }
  findBar_->setReplaceVisible(false);
  // Pre-fill with selected text if any
  if (sourceModeEnabled()) {
    const QTextCursor cursor = editor_->editor()->textCursor();
    if (cursor.hasSelection()) {
      findBar_->setSearchText(cursor.selectedText());
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
  // Pre-fill with selected text if any
  if (sourceModeEnabled()) {
    const QTextCursor cursor = editor_->editor()->textCursor();
    if (cursor.hasSelection()) {
      findBar_->setSearchText(cursor.selectedText());
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

  // Reset search state if the query changed
  if (text != lastFindText_) {
    lastFindText_ = text;
    lastFindOffset_ = -1;
  }

  if (sourceModeEnabled()) {
    QTextDocument::FindFlags flags;
    if (!forward) {
      flags |= QTextDocument::FindBackward;
    }
    editor_->editor()->find(text, flags);
    return;
  }

  // Render mode: search the full markdown source text
  const QString& markdown = session_.markdownText();

  // Determine start position
  int startPos = 0;
  if (lastFindOffset_ >= 0) {
    // Continue from last match position
    startPos = forward ? static_cast<int>(lastFindOffset_) + 1
                       : static_cast<int>(lastFindOffset_) - 1;
  } else {
    // First search: start from current cursor position
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
      // Wrap around from the beginning
      idx = markdown.indexOf(text, 0);
    }
  } else {
    startPos = qBound(0, startPos, markdown.size() - 1);
    idx = markdown.lastIndexOf(text, startPos);
    if (idx < 0) {
      // Wrap around from the end
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
  if (sourceModeEnabled()) {
    QTextCursor cursor = editor_->editor()->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
      cursor.insertText(replaceText);
      editor_->editor()->setTextCursor(cursor);
    }
    // Find next occurrence
    editor_->editor()->find(findText);
  } else {
    // Render mode: find and replace via source text
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
  if (sourceModeEnabled()) {
    QString text = editor_->editor()->toPlainText();
    text.replace(findText, replaceText);
    editor_->editor()->setPlainText(text);
  } else {
    QString text = session_.markdownText();
    text.replace(findText, replaceText);
    session_.applyMarkdownText(text, true);
  }
  lastFindOffset_ = -1;
}

}  // namespace muffin
