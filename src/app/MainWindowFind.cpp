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
  // Pre-fill with selected text if any
  if (sourceModeEnabled()) {
    const QTextCursor cursor = editor_->editor()->textCursor();
    if (cursor.hasSelection()) {
      findBar_->setSearchText(cursor.selectedText());
    }
  }
  findBar_->setVisible(true);
  findBar_->activateFind();
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
  if (sourceModeEnabled()) {
    QTextDocument::FindFlags flags;
    if (!forward) {
      flags |= QTextDocument::FindBackward;
    }
    editor_->editor()->find(text, flags);
  } else {
    // Render mode: search the markdown source text
    const QString& markdown = session_.markdownText();
    QTextDocument doc(markdown);
    QTextCursor searchCursor(&doc);

    // Start from current cursor's source offset
    qsizetype startOffset = 0;
    if (editorController_.selection().hasCursor()) {
      startOffset = qMax<qsizetype>(0, editorController_.selection().cursorPosition().text.sourceOffset);
    }
    searchCursor.setPosition(static_cast<int>(qMin<qsizetype>(startOffset, markdown.size())));

    QTextDocument::FindFlags flags;
    if (!forward) {
      flags |= QTextDocument::FindBackward;
    }

    const QTextCursor found = doc.find(text, searchCursor, flags);
    if (!found.isNull()) {
      const qsizetype matchOffset = found.selectionStart();
      MarkdownNode* block = session_.document().topLevelBlockAtOffset(matchOffset);
      if (block) {
        renderView_->scrollToNode(block->id());
      }
      findBar_->setResultInfo(0, 0);
    } else {
      findBar_->setResultInfo(-1, 0);
    }
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
}

}  // namespace muffin
