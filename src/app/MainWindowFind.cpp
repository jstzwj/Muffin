#include "app/MainWindow.h"

#include "editor/DocumentSearch.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

namespace {

QPair<qsizetype, qsizetype> renderSelectionSourceRange(
    const muffin::EditorController& controller) {
  if (!controller.selection().hasCursor()) { return {-1, -1}; }
  const muffin::SelectionRange range = controller.selection().selection();
  return {qMin(range.anchor.text.sourceOffset, range.focus.text.sourceOffset),
          qMax(range.anchor.text.sourceOffset, range.focus.text.sourceOffset)};
}

}  // namespace

void muffin::MainWindow::showFindBar() {
  if (!findBar_) { return; }
  findBar_->setReplaceVisible(false);
  if (backend_->isSourceMode()) {
    const QString selected = backend_->selectedText();
    if (!selected.isEmpty()) { findBar_->setSearchText(selected); }
  }
  lastFindOffset_ = -1;
  lastFindText_.clear();
  findBar_->setVisible(true);
  findBar_->activateFind();
}

void muffin::MainWindow::showReplaceBar() {
  if (!findBar_) { return; }
  findBar_->setReplaceVisible(true);
  if (backend_->isSourceMode()) {
    const QString selected = backend_->selectedText();
    if (!selected.isEmpty()) { findBar_->setSearchText(selected); }
  }
  lastFindOffset_ = -1;
  lastFindText_.clear();
  findBar_->setVisible(true);
  findBar_->activateReplace();
}

void muffin::MainWindow::hideFindBar() {
  if (findBar_) { findBar_->setVisible(false); }
}

void muffin::MainWindow::performFind(const QString& text, bool forward,
                                     bool regularExpression, bool caseSensitive) {
  if (text.isEmpty()) { return; }
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results = DocumentSearch::findAll(session_.markdownText(), text, options);
  if (!results.valid) {
    findBar_->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }
  if (results.matches.isEmpty()) {
    findBar_->setResultInfo(-1, 0);
    return;
  }

  const bool queryChanged = text != lastFindText_
      || regularExpression != lastFindRegularExpression_
      || caseSensitive != lastFindCaseSensitive_;
  if (queryChanged) {
    lastFindText_ = text;
    lastFindRegularExpression_ = regularExpression;
    lastFindCaseSensitive_ = caseSensitive;
    lastFindOffset_ = -1;
  }

  int selectedIndex = -1;
  if (lastFindOffset_ >= 0) {
    for (int i = 0; i < results.matches.size(); ++i) {
      if (results.matches.at(i).start == lastFindOffset_) {
        selectedIndex = forward ? (i + 1) % results.matches.size()
                                : (i - 1 + results.matches.size()) % results.matches.size();
        break;
      }
    }
  }

  if (selectedIndex < 0) {
    qsizetype cursorOffset = 0;
    if (backend_->isSourceMode()) {
      cursorOffset = forward ? editor_->selectionEnd() : editor_->selectionStart();
    } else if (editorController_.selection().hasCursor()) {
      cursorOffset = editorController_.selection().cursorPosition().text.sourceOffset;
    }
    if (forward) {
      selectedIndex = 0;
      for (int i = 0; i < results.matches.size(); ++i) {
        if (results.matches.at(i).start >= cursorOffset) { selectedIndex = i; break; }
      }
    } else {
      selectedIndex = results.matches.size() - 1;
      for (int i = results.matches.size() - 1; i >= 0; --i) {
        if (results.matches.at(i).start + results.matches.at(i).length <= cursorOffset) {
          selectedIndex = i;
          break;
        }
      }
    }
  }

  const SearchMatch& match = results.matches.at(selectedIndex);
  lastFindOffset_ = match.start;
  if (backend_->isSourceMode()) {
    editor_->setSelection(match.start, match.start + match.length);
    editor_->ensureCursorVisible();
  } else {
    editorController_.inputController().selectSourceRange(match.start, match.start + match.length);
    if (MarkdownNode* block = session_.document().topLevelBlockAtOffset(match.start)) {
      renderView_->scrollToNode(block->id());
    }
  }
  findBar_->setResultInfo(selectedIndex, results.matches.size());
}

void muffin::MainWindow::performFindNext() {
  if (!findBar_) { return; }
  performFind(findBar_->searchText(), true, findBar_->regularExpressionEnabled(),
              findBar_->caseSensitiveEnabled());
}

void muffin::MainWindow::performFindPrevious() {
  if (!findBar_) { return; }
  performFind(findBar_->searchText(), false, findBar_->regularExpressionEnabled(),
              findBar_->caseSensitiveEnabled());
}

void muffin::MainWindow::performReplace(const QString& findText, const QString& replaceText,
                                        bool regularExpression, bool caseSensitive) {
  if (findText.isEmpty()) { return; }
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results = DocumentSearch::findAll(session_.markdownText(), findText, options);
  if (!results.valid) {
    findBar_->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }

  QPair<qsizetype, qsizetype> selected{-1, -1};
  if (backend_->isSourceMode()) {
    selected = {editor_->selectionStart(), editor_->selectionEnd()};
  } else {
    selected = renderSelectionSourceRange(editorController_);
  }
  const SearchMatch* current = nullptr;
  for (const SearchMatch& match : results.matches) {
    if (match.start == selected.first && match.start + match.length == selected.second) {
      current = &match;
      break;
    }
  }
  if (!current) {
    lastFindOffset_ = -1;
    performFind(findText, true, regularExpression, caseSensitive);
    return;
  }

  const QString replacement = DocumentSearch::expandReplacement(
      replaceText, *current, regularExpression);
  if (backend_->isSourceMode()) {
    editor_->replaceSelection(replacement);
  } else {
    QString changed = backend_->fullText();
    changed.replace(current->start, current->length, replacement);
    editorController_.applyMarkdownTextWithUndo(changed, tr("Replace"));
  }
  lastFindOffset_ = -1;
  performFind(findText, true, regularExpression, caseSensitive);
}

void muffin::MainWindow::performReplaceAll(const QString& findText, const QString& replaceText,
                                           bool regularExpression, bool caseSensitive) {
  if (findText.isEmpty()) { return; }
  QString changed;
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results = DocumentSearch::replaceAll(
      backend_->fullText(), findText, replaceText, options, &changed);
  if (!results.valid) {
    findBar_->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }
  if (results.matches.isEmpty()) {
    findBar_->setResultInfo(-1, 0);
    return;
  }
  if (backend_->isSourceMode()) {
    editor_->selectAll();
    editor_->insertText(changed);
  } else {
    editorController_.applyMarkdownTextWithUndo(changed, tr("Replace All"));
  }
  lastFindOffset_ = -1;
  findBar_->setResultInfo(results.matches.size() - 1, results.matches.size());
}
