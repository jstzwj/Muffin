#include "app/MainWindow.h"
#include "editor/DocumentSearch.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace {
using namespace muffin;
QPair<qsizetype, qsizetype> selectedSourceRange(DocumentTab* tab) {
  if (tab->backend->isSourceMode())
    return {tab->editor->selectionStart(), tab->editor->selectionEnd()};
  if (!tab->editorController.selection().hasCursor())
    return {0, 0};
  const auto range = tab->editorController.selection().selection();
  return {qMin(range.anchor.text.sourceOffset, range.focus.text.sourceOffset),
          qMax(range.anchor.text.sourceOffset, range.focus.text.sourceOffset)};
}

void showMatch(DocumentTab* tab, const SearchResults& results) {
  if (results.selectedIndex < 0) {
    tab->lastFindOffset = -1;
    tab->findBar->setResultInfo(-1, 0);
    return;
  }
  const auto& match = results.selectedMatch;
  tab->lastFindOffset = match.start;
  tab->lastFindLength = match.length;
  if (tab->backend->isSourceMode()) {
    tab->editor->setSelection(match.start, match.start + match.length);
    tab->editor->ensureCursorVisible();
  } else {
    tab->editorController.selectSourceRange(match.start, match.start + match.length);
    tab->renderView->scrollToCursorCentered();
  }
  tab->findBar->setResultInfo(results.selectedIndex, results.totalMatches);
}

struct SearchJobResult {
  SearchResults search;
  QString replacement;
};

// No worker touches a session, widget or mutable text buffer. Lifetime,
// cancellation and revision checks protect tab moves, closure and edits while
// a result is being computed. Replacement is committed only on the GUI thread.
template <typename Work, typename Finish> void runSearch(DocumentTab* tab, Work work, Finish finish) {
  if (tab->findCancellation)
    tab->findCancellation->store(true);
  const auto cancel = tab->findCancellation = std::make_shared<std::atomic_bool>(false);
  const quint64 revision = tab->session.document().revision();
  auto* watcher = new QFutureWatcher<SearchJobResult>(tab);
  const auto invalidate = [tab, cancel] {
    cancel->store(true);
    if (tab->findCancellation == cancel)
      tab->findBar->setResultInfo(0, 0);
  };
  QObject::connect(&tab->session, &DocumentSession::documentTextChanged, watcher, invalidate);
  QObject::connect(&tab->session, &DocumentSession::documentLocallyEdited, watcher, invalidate);
  QObject::connect(&tab->session, &DocumentSession::documentAboutToBeReplaced, watcher, invalidate);
  QObject::connect(tab->windowConnections, &QObject::destroyed, watcher, [cancel] { cancel->store(true); });
  QObject::connect(watcher, &QFutureWatcher<SearchJobResult>::finished, watcher, &QObject::deleteLater);
  QObject::connect(watcher, &QFutureWatcher<SearchJobResult>::finished, tab->windowConnections,
                   [tab, watcher, cancel, revision, finish = std::move(finish)] {
                     if (cancel->load() || tab->session.document().revision() != revision ||
                         tab->session.isOpenInProgress())
                       return;
                     auto result = watcher->result();
                     if (!result.search.canceled)
                       finish(std::move(result));
                   });
  tab->findBar->setSearchBusy();
  const PieceTable snapshot = tab->session.markdownText();
  watcher->setFuture(QtConcurrent::run([snapshot, cancel, work = std::move(work)] { return work(snapshot, cancel); }));
}
} // namespace

void muffin::MainWindow::showFindBar() {
  if (!tab_->findBar)
    return;
  tab_->findBar->setReplaceVisible(false);
  const auto range = selectedSourceRange(tab_);
  if (range.first >= 0 && range.second > range.first && range.second - range.first <= 4096) {
    const auto selected = tab_->backend->selectedText();
    if (!selected.isEmpty())
      tab_->findBar->setSearchText(selected);
  }
  tab_->lastFindOffset = -1;
  tab_->findBar->setVisible(true);
  tab_->findBar->activateFind();
}

void muffin::MainWindow::showReplaceBar() {
  showFindBar();
  tab_->findBar->setReplaceVisible(true);
  tab_->findBar->activateReplace();
}

void muffin::MainWindow::hideFindBar() {
  if (tab_->findCancellation)
    tab_->findCancellation->store(true);
  if (tab_->findBar)
    tab_->findBar->setVisible(false);
}

void muffin::MainWindow::performFind(const QString& text, bool forward, bool regularExpression, bool caseSensitive) {
  if (text.isEmpty() || tab_->session.isOpenInProgress())
    return;
  auto* tab = tab_;
  const auto selected = selectedSourceRange(tab);
  const bool sameQuery = text == tab->lastFindText && regularExpression == tab->lastFindRegularExpression &&
                         caseSensitive == tab->lastFindCaseSensitive;
  const qsizetype exclude =
      sameQuery && selected.first == tab->lastFindOffset && selected.second == tab->lastFindOffset + tab->lastFindLength
          ? tab->lastFindOffset
          : -1;
  const qsizetype offset = qMax<qsizetype>(0, forward ? selected.second : selected.first);
  if (tab->findCancellation)
    tab->findCancellation->store(true);
  if (sameQuery && tab->findCacheValid && tab->findRevision == tab->session.document().revision() &&
      tab->findCache.totalMatches == tab->findCache.matches.size()) {
    DocumentSearch::selectMatch(tab->findCache, offset, forward, exclude);
    showMatch(tab, tab->findCache);
    return;
  }
  runSearch(
      tab,
      [=](const PieceTable& snapshot, const DocumentSearch::Cancellation& cancel) {
        return SearchJobResult{DocumentSearch::findAll(snapshot, text, {regularExpression, caseSensitive}, cancel,
                                                       offset, forward, exclude),
                               {}};
      },
      [=, this](SearchJobResult result) {
        if (tab_ != tab || selectedSourceRange(tab) != selected || tab->findBar->searchText() != text ||
            tab->findBar->regularExpressionEnabled() != regularExpression ||
            tab->findBar->caseSensitiveEnabled() != caseSensitive)
          return;
        if (!result.search.valid) {
          tab->findBar->setErrorText(tr("Invalid regular expression: %1").arg(result.search.error));
          return;
        }
        tab->lastFindText = text;
        tab->lastFindRegularExpression = regularExpression;
        tab->lastFindCaseSensitive = caseSensitive;
        tab->findRevision = tab->session.document().revision();
        tab->findCacheValid = true;
        tab->findCache = std::move(result.search);
        showMatch(tab, tab->findCache);
      });
}

void muffin::MainWindow::performFindNext() {
  if (tab_->findBar)
    performFind(tab_->findBar->searchText(), true, tab_->findBar->regularExpressionEnabled(),
                tab_->findBar->caseSensitiveEnabled());
}

void muffin::MainWindow::performFindPrevious() {
  if (tab_->findBar)
    performFind(tab_->findBar->searchText(), false, tab_->findBar->regularExpressionEnabled(),
                tab_->findBar->caseSensitiveEnabled());
}

void muffin::MainWindow::performReplace(const QString& findText, const QString& replaceText, bool regularExpression,
                                        bool caseSensitive) {
  if (!tab_->session.isEditingAllowed() || findText.isEmpty())
    return;
  auto* tab = tab_;
  const auto selected = selectedSourceRange(tab);
  runSearch(
      tab,
      [=](const PieceTable& snapshot, const DocumentSearch::Cancellation& cancel) {
        return SearchJobResult{
            DocumentSearch::findAll(snapshot, findText, {regularExpression, caseSensitive}, cancel, selected.first),
            {}};
      },
      [=, this](SearchJobResult result) {
        if (tab_ != tab || !tab->session.isEditingAllowed() || selectedSourceRange(tab) != selected ||
            tab->findBar->searchText() != findText || tab->findBar->regularExpressionEnabled() != regularExpression ||
            tab->findBar->caseSensitiveEnabled() != caseSensitive)
          return;
        if (!result.search.valid) {
          tab->findBar->setErrorText(tr("Invalid regular expression: %1").arg(result.search.error));
          return;
        }
        const auto& match = result.search.selectedMatch;
        tab->lastFindText = findText;
        tab->lastFindRegularExpression = regularExpression;
        tab->lastFindCaseSensitive = caseSensitive;
        if (match.start < 0 || match.start != selected.first || match.start + match.length != selected.second) {
          showMatch(tab, result.search);
          return;
        }
        const QString replacement = DocumentSearch::expandReplacement(replaceText, match, regularExpression);
        if (tab->backend->isSourceMode())
          tab->editor->insertText(replacement);
        else
          tab->editorController.replaceSourceRangeWithUndo(match.start, match.length, replacement, tr("Replace"));
        tab->lastFindOffset = match.length == 0 && replacement.isEmpty() ? match.start : -1;
        tab->lastFindLength = 0;
        performFind(findText, true, regularExpression, caseSensitive);
      });
}

void muffin::MainWindow::performReplaceAll(const QString& findText, const QString& replaceText, bool regularExpression,
                                           bool caseSensitive) {
  if (!tab_->session.isEditingAllowed() || findText.isEmpty())
    return;
  auto* tab = tab_;
  runSearch(
      tab,
      [=](const PieceTable& snapshot, const DocumentSearch::Cancellation& cancel) {
        SearchJobResult result;
        result.search = DocumentSearch::replaceAll(snapshot.toString(), findText, replaceText,
                                                   {regularExpression, caseSensitive}, &result.replacement, cancel);
        return result;
      },
      [=, this](SearchJobResult result) {
        if (tab_ != tab || !tab->session.isEditingAllowed() || tab->findBar->searchText() != findText ||
            tab->findBar->regularExpressionEnabled() != regularExpression ||
            tab->findBar->caseSensitiveEnabled() != caseSensitive)
          return;
        if (!result.search.valid) {
          tab->findBar->setErrorText(tr("Invalid regular expression: %1").arg(result.search.error));
          return;
        }
        if (result.search.totalMatches > 0) {
          if (tab->backend->isSourceMode())
            tab->editor->replaceAllWithUndo(result.replacement);
          else
            tab->editorController.applyMarkdownTextWithUndo(std::move(result.replacement), tr("Replace All"));
        }
        tab->lastFindOffset = -1;
        tab->findBar->setResultInfo(result.search.totalMatches - 1, result.search.totalMatches);
      });
}
