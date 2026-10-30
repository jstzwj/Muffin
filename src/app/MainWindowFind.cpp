#include "app/MainWindow.h"

#include "editor/DocumentSearch.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

namespace {

QPair<qsizetype, qsizetype> renderSelectionSourceRange(const muffin::EditorController& controller) {
  if (!controller.selection().hasCursor()) {
    return {-1, -1};
  }
  const muffin::SelectionRange range = controller.selection().selection();
  return {qMin(range.anchor.text.sourceOffset, range.focus.text.sourceOffset),
          qMax(range.anchor.text.sourceOffset, range.focus.text.sourceOffset)};
}

} // namespace

void muffin::MainWindow::showFindBar() {
  if (!tab_->findBar) {
    return;
  }
  tab_->findBar->setReplaceVisible(false);
  if (tab_->backend->isSourceMode()) {
    const QString selected = tab_->backend->selectedText();
    if (!selected.isEmpty()) {
      tab_->findBar->setSearchText(selected);
    }
  }
  tab_->lastFindOffset = -1;
  tab_->lastFindText.clear();
  tab_->findBar->setVisible(true);
  tab_->findBar->activateFind();
}

void muffin::MainWindow::showReplaceBar() {
  if (!tab_->findBar) {
    return;
  }
  tab_->findBar->setReplaceVisible(true);
  if (tab_->backend->isSourceMode()) {
    const QString selected = tab_->backend->selectedText();
    if (!selected.isEmpty()) {
      tab_->findBar->setSearchText(selected);
    }
  }
  tab_->lastFindOffset = -1;
  tab_->lastFindText.clear();
  tab_->findBar->setVisible(true);
  tab_->findBar->activateReplace();
}

void muffin::MainWindow::hideFindBar() {
  if (tab_->findBar) {
    tab_->findBar->setVisible(false);
  }
}

void muffin::MainWindow::performFind(const QString& text, bool forward, bool regularExpression, bool caseSensitive) {
  if (text.isEmpty()) {
    return;
  }
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results = DocumentSearch::findAll(tab_->session.markdownText(), text, options);
  if (!results.valid) {
    tab_->findBar->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }
  if (results.matches.isEmpty()) {
    tab_->findBar->setResultInfo(-1, 0);
    return;
  }

  const bool queryChanged = text != tab_->lastFindText || regularExpression != tab_->lastFindRegularExpression ||
                            caseSensitive != tab_->lastFindCaseSensitive;
  if (queryChanged) {
    tab_->lastFindText = text;
    tab_->lastFindRegularExpression = regularExpression;
    tab_->lastFindCaseSensitive = caseSensitive;
    tab_->lastFindOffset = -1;
  }

  int selectedIndex = -1;
  if (tab_->lastFindOffset >= 0) {
    for (int i = 0; i < results.matches.size(); ++i) {
      if (results.matches.at(i).start == tab_->lastFindOffset) {
        selectedIndex =
            forward ? (i + 1) % results.matches.size() : (i - 1 + results.matches.size()) % results.matches.size();
        break;
      }
    }
  }

  if (selectedIndex < 0) {
    qsizetype cursorOffset = 0;
    if (tab_->backend->isSourceMode()) {
      cursorOffset = forward ? tab_->editor->selectionEnd() : tab_->editor->selectionStart();
    } else if (tab_->editorController.selection().hasCursor()) {
      cursorOffset = tab_->editorController.selection().cursorPosition().text.sourceOffset;
    }
    if (forward) {
      selectedIndex = 0;
      for (int i = 0; i < results.matches.size(); ++i) {
        if (results.matches.at(i).start >= cursorOffset) {
          selectedIndex = i;
          break;
        }
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
  tab_->lastFindOffset = match.start;
  if (tab_->backend->isSourceMode()) {
    tab_->editor->setSelection(match.start, match.start + match.length);
    tab_->editor->ensureCursorVisible();
  } else {
    tab_->editorController.inputController().selectSourceRange(match.start, match.start + match.length);
    // A table, wrapped paragraph or code fence can span many screens. Reveal
    // the selected text row rather than scrolling back to its block's top.
    tab_->renderView->scrollToCursorCentered();
  }
  tab_->findBar->setResultInfo(selectedIndex, results.matches.size());
}

void muffin::MainWindow::performFindNext() {
  if (!tab_->findBar) {
    return;
  }
  performFind(tab_->findBar->searchText(), true, tab_->findBar->regularExpressionEnabled(),
              tab_->findBar->caseSensitiveEnabled());
}

void muffin::MainWindow::performFindPrevious() {
  if (!tab_->findBar) {
    return;
  }
  performFind(tab_->findBar->searchText(), false, tab_->findBar->regularExpressionEnabled(),
              tab_->findBar->caseSensitiveEnabled());
}

void muffin::MainWindow::performReplace(const QString& findText, const QString& replaceText, bool regularExpression,
                                        bool caseSensitive) {
  if (!tab_->session.isEditingAllowed())
    return;
  if (findText.isEmpty()) {
    return;
  }
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results = DocumentSearch::findAll(tab_->session.markdownText(), findText, options);
  if (!results.valid) {
    tab_->findBar->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }

  QPair<qsizetype, qsizetype> selected{-1, -1};
  if (tab_->backend->isSourceMode()) {
    selected = {tab_->editor->selectionStart(), tab_->editor->selectionEnd()};
  } else {
    selected = renderSelectionSourceRange(tab_->editorController);
  }
  const SearchMatch* current = nullptr;
  for (const SearchMatch& match : results.matches) {
    if (match.start == selected.first && match.start + match.length == selected.second) {
      current = &match;
      break;
    }
  }
  if (!current) {
    tab_->lastFindOffset = -1;
    performFind(findText, true, regularExpression, caseSensitive);
    return;
  }

  const QString replacement = DocumentSearch::expandReplacement(replaceText, *current, regularExpression);
  if (tab_->backend->isSourceMode()) {
    tab_->editor->replaceSelection(replacement);
  } else {
    QString changed = tab_->backend->fullText();
    changed.replace(current->start, current->length, replacement);
    tab_->editorController.applyMarkdownTextWithUndo(changed, tr("Replace"));
  }
  tab_->lastFindOffset = -1;
  performFind(findText, true, regularExpression, caseSensitive);
}

void muffin::MainWindow::performReplaceAll(const QString& findText, const QString& replaceText, bool regularExpression,
                                           bool caseSensitive) {
  if (!tab_->session.isEditingAllowed())
    return;
  if (findText.isEmpty()) {
    return;
  }
  QString changed;
  const SearchOptions options{regularExpression, caseSensitive};
  const SearchResults results =
      DocumentSearch::replaceAll(tab_->backend->fullText(), findText, replaceText, options, &changed);
  if (!results.valid) {
    tab_->findBar->setErrorText(tr("Invalid regular expression: %1").arg(results.error));
    return;
  }
  if (results.matches.isEmpty()) {
    tab_->findBar->setResultInfo(-1, 0);
    return;
  }
  if (tab_->backend->isSourceMode()) {
    tab_->editor->selectAll();
    tab_->editor->insertText(changed);
  } else {
    tab_->editorController.applyMarkdownTextWithUndo(changed, tr("Replace All"));
  }
  tab_->lastFindOffset = -1;
  tab_->findBar->setResultInfo(results.matches.size() - 1, results.matches.size());
}
