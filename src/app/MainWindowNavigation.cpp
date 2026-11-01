#include "app/MainWindow.h"
#include "app/NoteLinks.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include "editor/ResourceUrl.h"
#include "io/FilePathOps.h"
#include <QAction>
#include <QDesktopServices>
#include <QFileInfo>
#include <QStatusBar>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>

muffin::MainWindow::NavigationEntry muffin::MainWindow::navigationEntry() const {
  return {tab_, tab_->session.filePath(), tabViewState(tab_)};
}

void muffin::MainWindow::navigateToLink(const QString& href) {
  if (href.isEmpty() || workspaceWriteInProgress_ || tab_->session.isOpenInProgress())
    return;
  const auto url = resolvedUrlForDocumentResource(href, tab_->session.filePath());
  const bool sameFile = href.startsWith('#');
  if (!sameFile && (!url.isLocalFile() || !NoteLinks::isMarkdownPath(url.toLocalFile()))) {
    QDesktopServices::openUrl(url);
    return;
  }
  const QString path = sameFile ? tab_->session.filePath() : url.toLocalFile();
  if (!sameFile && !QFileInfo(path).isFile()) {
    statusBar()->showMessage(tr("Linked file not found: %1").arg(QDir::toNativeSeparators(path)), 6000);
    return;
  }
  const auto origin = navigationEntry();
  pendingNavigation_.reset();
  if (!sameFile && !openFile(path))
    return;
  pendingNavigation_ = PendingNavigation{tab_, origin, url.fragment(QUrl::FullyDecoded), url.hasFragment(), {}, 0};
  updateAllActions();
  QTimer::singleShot(0, tab_->windowConnections, [this, destination = QPointer<DocumentTab>(tab_)] {
    if (destination)
      applyLinkNavigation(destination);
  });
}

void muffin::MainWindow::navigateHistory(bool forward) {
  if (pendingNavigation_ || workspaceWriteInProgress_ || tab_->session.isOpenInProgress())
    return;
  auto& history = forward ? forwardHistory_ : backHistory_;
  const auto origin = navigationEntry();
  while (!history.isEmpty()) {
    const auto target = history.last();
    if (target.document && tabs_->indexOf(target.document) >= 0) {
      tabs_->setCurrentWidget(target.document);
    } else if (target.path.isEmpty() || !QFileInfo(target.path).isFile()) {
      history.removeLast();
      continue;
    } else if (!openFile(target.path)) {
      break;
    }
    pendingNavigation_ = PendingNavigation{tab_, origin, {}, false, target, forward ? 1 : -1};
    updateAllActions();
    QTimer::singleShot(0, tab_->windowConnections, [this, destination = QPointer<DocumentTab>(tab_)] {
      if (destination)
        applyLinkNavigation(destination);
    });
    return;
  }
  updateAllActions();
}

void muffin::MainWindow::applyLinkNavigation(DocumentTab* document) {
  if (!pendingNavigation_ || pendingNavigation_->destination != document || document != tab_ ||
      document->session.isOpenInProgress() || !document->isVisible() || sessionUpdatesPaused_)
    return;
  const auto request = *pendingNavigation_;
  if (document->renderViewDirty && !document->backend->isSourceMode()) {
    document->ensureRenderViewCurrent();
  }
  if (!document->backend->isSourceMode() && document->renderView->isLayoutPending())
    return;
  if (request.restore) {
    const bool source = request.restore->view.value(QStringLiteral("sourceMode")).toBool();
    if (source != document->backend->isSourceMode()) {
      const QSignalBlocker blocker(commands_.action(QStringLiteral("view.source_mode")));
      commands_.action(QStringLiteral("view.source_mode"))->setChecked(source);
      updateViewMode();
    }
    document->pendingViewState = request.restore->view;
    restoreTabViewState(document);
  } else if (request.hasFragment) {
    NodeId target = document->renderView->specialAnchorTarget('#' + request.fragment);
    if (!request.fragment.isEmpty() && !document->session.document().node(target))
      target = NoteLinks::headingTarget(document->session.document().outline(), request.fragment);
    const auto* node = document->session.document().node(target);
    if (node || request.fragment.isEmpty()) {
      const qsizetype offset = node && !node->inlines().isEmpty() && node->inlines().first().contentRange().isValid()
                                   ? node->inlines().first().contentRange().start + node->inlineAbsoluteDelta()
                               : node ? node->sourceRange().byteStart
                                      : 0;
      document->pendingViewState.reset();
      if (document->backend->isSourceMode()) {
        document->editor->setCursorPosition(offset);
        document->editor->restoreViewportPosition({offset, 0, 0, true, offset == 0});
      } else {
        document->editorController.setCursorForSourceOffset(offset);
        if (node)
          document->renderView->scrollToNode(target);
        else
          document->renderView->restoreViewportPosition({0, 0, 0, true, true});
      }
    } else {
      statusBar()->showMessage(tr("Heading not found: %1").arg(request.fragment), 6000);
      if (request.origin.document == document) {
        pendingNavigation_.reset();
        updateAllActions();
        return;
      }
    }
  }
  if (request.direction == 0) {
    backHistory_.append(request.origin);
    forwardHistory_.clear();
  } else {
    auto& from = request.direction > 0 ? forwardHistory_ : backHistory_;
    auto& to = request.direction > 0 ? backHistory_ : forwardHistory_;
    if (!from.isEmpty())
      from.removeLast();
    to.append(request.origin);
  }
  if (backHistory_.size() > 200)
    backHistory_.removeFirst();
  if (forwardHistory_.size() > 200)
    forwardHistory_.removeFirst();
  pendingNavigation_.reset();
  document->viewStack->currentWidget()->setFocus(Qt::OtherFocusReason);
  updateAllActions();
  scheduleSessionSave();
}

void muffin::MainWindow::remapNavigationPaths(const QString& oldPath, const QString& newPath) {
  const auto remap = [&](NavigationEntry& entry) {
    entry.path = FilePathOps::remapDescendant(entry.path, oldPath, newPath);
  };
  for (auto& entry : backHistory_)
    remap(entry);
  for (auto& entry : forwardHistory_)
    remap(entry);
  if (pendingNavigation_) {
    remap(pendingNavigation_->origin);
    if (pendingNavigation_->restore)
      remap(*pendingNavigation_->restore);
  }
}
