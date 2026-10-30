#include "app/MainWindow.h"
#include "app/SearchPanelWidget.h"
#include "editor/SourceEditorWidget.h"
#include "editor/EditorView.h"
#include "editor/BlockEditContext.h"
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QStatusBar>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QTimer>
#include <QtConcurrentRun>

namespace {
QString recoveryRoot() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
      .filePath("directory-replacements");
}

bool renderedSearchRange(muffin::DocumentTab& document, qsizetype start, qsizetype length,
                         muffin::SelectionRange& range) {
  if (start < 0 || length <= 0 || start > document.session.markdownText().size() - length)
    return false;
  const auto end = start + length;
  range = {document.editorController.cursorForSourceOffset(start),
           document.editorController.cursorForSourceOffset(end)};
  if (!range.anchor.isValid() || !range.focus.isValid() || !range.isSingleTextNode() || range.anchor.afterBlock ||
      range.focus.afterBlock)
    return false;
  const auto id = range.anchor.text.nodeId;
  const auto* node = document.session.document().node(id);
  if (!node)
    return false;
  // Ordinary fenced code is editable inside the live view. Diagram source and
  // other literal blocks may instead be displayed as rendered objects.
  muffin::BlockEditContextResolver resolver(&document.session, nullptr);
  if (node->type() == muffin::BlockType::CodeFence) {
    const auto content = resolver.literalContentStartOffset(*node);
    return node->codeLanguage().compare(QLatin1String("mermaid"), Qt::CaseInsensitive) != 0 && start >= content &&
           end <= content + node->literal().size() &&
           document.session.markdownText().mid(start, length) == node->literal().mid(start - content, length);
  }
  muffin::BlockEditContext context;
  if (!resolver.forBlock(id, context) || !context.inlineProjection.isValid() ||
      start < context.contentRange.byteStart || end > context.contentRange.byteEnd)
    return false;
  const auto localEnd = end - context.contentRange.byteStart;
  auto covered = start - context.contentRange.byteStart;
  for (const auto& span : context.inlineProjection.spans()) {
    if (span.contentSourceEnd <= covered)
      continue;
    if (span.contentSourceStart > covered)
      return false;
    if ((span.kind != muffin::InlineSpanKind::Text && span.kind != muffin::InlineSpanKind::HtmlContent) ||
        span.type == muffin::InlineType::InlineMath || !span.editable ||
        span.visibleEnd - span.visibleStart != span.contentSourceEnd - span.contentSourceStart)
      return false;
    covered = qMin(localEnd, span.contentSourceEnd);
    if (covered == localEnd)
      return true;
  }
  return false;
}
}

void muffin::MainWindow::showWorkspaceSearch(bool replace, const QString& directory) {
  setSidebarPanel(SidebarWidget::Panel::Search);
  sidebar_->searchPanel()->setRoot(directory.isEmpty() ? sidebar_->folderRoot() : directory);
  sidebar_->searchPanel()->activate(replace);
}
muffin::OpenSearchBuffers muffin::MainWindow::openSearchBuffers() const {
  OpenSearchBuffers buffers;
  for (auto* widget : QApplication::topLevelWidgets()) {
    const auto* window = qobject_cast<MainWindow*>(widget);
    if (!window)
      continue;
    for (const auto* document : window->documentTabs()) {
      const auto path = document->session.filePath();
      if (path.isEmpty())
        continue;
      const auto key = WorkspaceFileIndex::pathKey(path);
      const bool modified = document->session.document().isModified();
      const bool busy = document->session.isOpenInProgress();
      const bool readOnly = document->session.editingReadOnly();
      const bool large = document->session.markdownText().size() * 2 > 8 * 1024 * 1024;
      const auto text = busy || large ? QString() : document->session.markdownText().toString();
      auto snapshot = !busy && large ? std::make_shared<const PieceTable>(document->session.markdownText()) : nullptr;
      const auto existing = buffers.constFind(key);
      const bool anyReadOnly = readOnly || (existing != buffers.cend() && existing->readOnly);
      if (existing != buffers.cend())
        buffers[key].readOnly = anyReadOnly;
      if (existing != buffers.cend() && existing->busy)
        continue;
      if (existing != buffers.cend() && existing->modified && modified &&
          (existing->snapshot ? WorkspaceSearch::fingerprint(*existing->snapshot)
                              : WorkspaceSearch::fingerprint(existing->text)) !=
              (snapshot ? WorkspaceSearch::fingerprint(*snapshot) : WorkspaceSearch::fingerprint(text))) {
        buffers[key].busy = true;
        continue;
      }
      if (existing == buffers.cend() || modified || busy)
        buffers.insert(key, {text, modified, busy, std::move(snapshot), anyReadOnly});
    }
  }
  return buffers;
}
void muffin::MainWindow::navigateSearchResult(const QString& path, const QByteArray& fingerprint, qsizetype offset,
                                              qsizetype length) {
  // A dirty buffer in another window supplied this result; navigate to its
  // owner rather than opening an older disk copy in this window.
  const auto key = WorkspaceFileIndex::pathKey(path);
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* owner = qobject_cast<MainWindow*>(widget);
    if (!owner || owner == this)
      continue;
    for (auto* document : owner->documentTabs()) {
      if (WorkspaceFileIndex::pathKey(document->session.filePath()) != key ||
          !document->session.document().isModified() || document->session.isOpenInProgress() ||
          WorkspaceSearch::fingerprint(document->session.markdownText()) != fingerprint)
        continue;
      owner->tabs_->setCurrentWidget(document);
      document->pendingSearchFingerprint = fingerprint;
      document->pendingSearchOffset = offset;
      document->pendingSearchLength = length;
      owner->show();
      owner->raise();
      owner->activateWindow();
      owner->applySearchNavigation(document);
      return;
    }
  }
  if (!openFile(path))
    return;
  tab_->pendingSearchFingerprint = fingerprint;
  tab_->pendingSearchOffset = offset;
  tab_->pendingSearchLength = length;
  applySearchNavigation(tab_);
}
void muffin::MainWindow::applySearchNavigation(DocumentTab* document) {
  if (document != tab_ || !document->pendingSearchFingerprint || document->session.isOpenInProgress())
    return;
  const auto expected = std::move(*document->pendingSearchFingerprint);
  document->pendingSearchFingerprint.reset();
  if (WorkspaceSearch::fingerprint(document->session.markdownText()) != expected) {
    statusBar()->showMessage(tr("This file changed. Search again to update its matches."), 6000);
    return;
  }
  document->pendingViewState.reset();
  const bool preferRender = !document->backend->isSourceMode() || document->searchSourceFallback;
  SelectionRange range;
  const bool render = preferRender && renderedSearchRange(*document, document->pendingSearchOffset,
                                                          document->pendingSearchLength, range);
  document->searchSourceFallback = preferRender && !render;
  const bool sourceMode = !render;
  if (document->backend->isSourceMode() != sourceMode) {
    if (auto* action = commands_.action(QStringLiteral("view.source_mode")))
      action->setChecked(sourceMode);
    updateViewMode();
  }
  if (render) {
    if (document->renderViewDirty) {
      document->renderView->setDocument(document->session.document(), document->session.filePath());
      document->renderViewDirty = false;
    }
    document->renderView->scrollToNode(range.focus.blockId);
    const auto hit = document->renderView->hitForCursorPosition(range.focus);
    if (hit.zone == HitTestResult::Zone::Code)
      document->editorController.activateHit(hit);
    document->editorController.selection().setSelection(range);
    document->renderView->ensureCursorVisible();
    document->renderView->setFocus();
    statusBar()->clearMessage();
  } else {
    document->editor->setSelection(document->pendingSearchOffset,
                                   document->pendingSearchOffset + document->pendingSearchLength);
    document->editor->ensureCursorVisible();
    document->editor->setFocus();
    if (document->searchSourceFallback)
      statusBar()->showMessage(
          tr("This match cannot be selected precisely in live preview. Showing it in source mode."), 6000);
  }
  sidebar_->setCurrentPath(document->session.filePath());
  scheduleSessionSave();
}

void muffin::MainWindow::previewWorkspaceReplace() {
  auto* panel = sidebar_->searchPanel();
  const auto result = panel->result();
  const QString replacement = panel->replacement();
  if (!result.complete || !result.error.isEmpty() || result.matches == 0)
    return;
  QStringList skipped = result.skipped;
  auto changes = WorkspaceReplace::prepare(result, replacement, &skipped);
  const auto buffers = openSearchBuffers();
  changes.removeIf([&](const WorkspaceChange& change) {
    const auto buffer = buffers.constFind(WorkspaceFileIndex::pathKey(change.path));
    if (buffer != buffers.cend() &&
        (buffer->modified || buffer->busy || !WorkspaceSearch::bufferMatches(*buffer, change.beforeText))) {
      skipped.append(change.path);
      return true;
    }
    return false;
  });
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Preview Folder Replacement"));
  dialog.resize(880, 650);
  auto* layout = new QVBoxLayout(&dialog);
  auto* description =
      new QLabel(tr("Folder: %1\nFind: %2\nReplace with: %3")
                     .arg(QDir::toNativeSeparators(result.query.root), result.query.pattern, replacement),
                 &dialog);
  description->setTextFormat(Qt::PlainText);
  description->setWordWrap(true);
  layout->addWidget(description);
  auto* filters = new QLabel(QStringLiteral("%1: %2 · %3: %4")
                                 .arg(muffin::SearchPanelWidget::tr("Match case"),
                                      result.query.options.caseSensitive ? tr("Yes") : tr("No"),
                                      muffin::SearchPanelWidget::tr("Regular expression"),
                                      result.query.options.regularExpression ? tr("Yes") : tr("No")),
                             &dialog);
  filters->setTextFormat(Qt::PlainText);
  filters->setWordWrap(true);
  layout->addWidget(filters);
  auto* splitter = new QSplitter(Qt::Vertical, &dialog);
  layout->addWidget(splitter, 1);
  auto* files = new QTreeWidget(splitter);
  files->setObjectName("FolderReplaceFiles");
  files->setHeaderLabels({tr("File"), tr("Matches")});
  files->setColumnWidth(0, 580);
  for (qsizetype i = 0; i < changes.size(); ++i) {
    const auto& change = changes[i];
    auto* item = new QTreeWidgetItem(
        files, {QDir(result.query.root).relativeFilePath(change.path), QString::number(change.matches)});
    item->setData(0, Qt::UserRole, i);
    item->setCheckState(0, Qt::Checked);
    item->setToolTip(0, change.path);
  }
  for (const auto& path : skipped) {
    auto* item = new QTreeWidgetItem(files, {QDir(result.query.root).relativeFilePath(path), tr("Skipped")});
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    item->setToolTip(0, tr("Unsaved, changed, unreadable, oversized, mixed line endings, or not losslessly writable."));
  }
  auto* diff = new QPlainTextEdit(splitter);
  diff->setReadOnly(true);
  diff->setAccessibleName(tr("Replacement preview"));
  connect(files, &QTreeWidget::currentItemChanged, &dialog, [&, diff](QTreeWidgetItem* item) {
    if (!item || !item->data(0, Qt::UserRole).isValid())
      return;
    const auto& change = changes[item->data(0, Qt::UserRole).toInt()];
    const auto clipped = [&](const QString& text) {
      return text.size() > 100000 ? text.left(100000) + tr("\n… Preview truncated; the full file will be processed.")
                                  : text;
    };
    diff->setPlainText(tr("Before:\n%1\n\nAfter:\n%2").arg(clipped(change.beforeText), clipped(change.afterText)));
  });
  if (!changes.isEmpty())
    files->setCurrentItem(files->topLevelItem(0));
  auto* warning = new QLabel(
      tr("Files with unsaved changes are skipped. Originals are backed up for recovery. Files changed since this preview will not be overwritten."),
      &dialog);
  warning->setWordWrap(true);
  layout->addWidget(warning);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  auto* apply = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  apply->setObjectName("FolderReplaceConfirm");
  const auto update = [&] {
    int count = 0, matches = 0;
    for (int i = 0; i < changes.size(); ++i)
      if (files->topLevelItem(i)->checkState(0) == Qt::Checked) {
        ++count;
        matches += changes[i].matches;
      }
    apply->setText(tr("Replace %1 matches in %2 files").arg(matches).arg(count));
    apply->setEnabled(count > 0);
  };
  connect(files, &QTreeWidget::itemChanged, &dialog, update);
  update();
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted)
    return;
  panel->invalidate();
  QVector<WorkspaceChange> selected;
  for (int i = 0; i < changes.size(); ++i)
    if (files->topLevelItem(i)->checkState(0) == Qt::Checked)
      selected.append(changes[i]);
  const auto report = runWorkspaceWrite(
      [root = result.query.root, selected](const OpenSearchBuffers& current, const SearchCancellation& cancel) {
        QVector<WorkspaceChange> safe;
        QStringList skipped;
        for (const auto& change : selected) {
          const auto buffer = current.constFind(WorkspaceFileIndex::pathKey(change.path));
          if (buffer != current.cend() && (buffer->modified || buffer->busy || buffer->readOnly ||
                                           !WorkspaceSearch::bufferMatches(*buffer, change.beforeText)))
            skipped.append(change.path);
          else
            safe.append(change);
        }
        auto report = WorkspaceReplace::apply(root, safe, recoveryRoot(), cancel);
        report.skipped.append(skipped);
        return report;
      });
  QMessageBox message(
      QMessageBox::Information, tr("Folder Replacement"),
      tr("%1 files changed; %2 skipped.\n%3").arg(report.changed.size()).arg(report.skipped.size()).arg(report.error),
      QMessageBox::Ok, this);
  message.setObjectName("FolderReplaceReport");
  message.setDetailedText(report.skipped.join('\n') + tr("\nRecovery folder: %1").arg(report.backupDirectory));
  message.exec();
  panel->startSearch(openSearchBuffers());
}

void muffin::MainWindow::undoWorkspaceReplace() {
  const QString directory = WorkspaceReplace::latestRecovery(recoveryRoot());
  if (directory.isEmpty()) {
    QMessageBox::information(this, tr("Undo Folder Replacement"), tr("No folder replacement is available to restore."));
    return;
  }
  QMessageBox confirm(
      QMessageBox::Question, tr("Undo Folder Replacement"),
      tr("Restore the original files from this replacement?\n%1\n\nFiles edited afterward or containing unsaved changes will be skipped.")
          .arg(QDir::toNativeSeparators(directory)),
      QMessageBox::Cancel, this);
  auto* restore = confirm.addButton(tr("Restore Originals"), QMessageBox::AcceptRole);
  auto* open = confirm.addButton(tr("Open Recovery Folder"), QMessageBox::ActionRole);
  confirm.exec();
  if (confirm.clickedButton() == open) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(recoveryRoot()));
    return;
  }
  if (confirm.clickedButton() != restore)
    return;
  sidebar_->searchPanel()->invalidate();
  const auto report =
      runWorkspaceWrite([directory](const OpenSearchBuffers& buffers, const SearchCancellation& cancel) {
        return WorkspaceReplace::restore(directory, buffers, cancel);
      });
  QMessageBox message(
      QMessageBox::Information, tr("Undo Folder Replacement"),
      tr("%1 files restored; %2 skipped.\n%3").arg(report.changed.size()).arg(report.skipped.size()).arg(report.error),
      QMessageBox::Ok, this);
  message.setObjectName("FolderRestoreReport");
  message.setDetailedText(report.skipped.join('\n'));
  message.exec();
  sidebar_->searchPanel()->startSearch(openSearchBuffers());
}

muffin::WorkspaceReplaceReport muffin::MainWindow::runWorkspaceWrite(
    const std::function<WorkspaceReplaceReport(const OpenSearchBuffers&, const SearchCancellation&)>& operation) {
  // Freeze all Muffin windows while a worker writes. Modal cancellation stops
  // at a file boundary; it must not re-enable editing before the worker exits.
  struct Frozen {
    MainWindow* window;
    bool centralEnabled, menuEnabled;
  };
  struct PausedTimer {
    QTimer* timer;
    int remaining;
  };
  QVector<Frozen> frozen;
  QVector<PausedTimer> pausedTimers;
  std::vector<std::unique_ptr<DocumentSession::SelfWriteGuard>> guards;
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* window = qobject_cast<MainWindow*>(widget);
    if (!window)
      continue;
    frozen.append({window, window->centralWidget()->isEnabled(), window->menuBar()->isEnabled()});
    window->workspaceWriteInProgress_ = true;
    window->centralWidget()->setEnabled(false);
    window->menuBar()->setEnabled(false);
    for (auto* document : window->documentTabs()) {
      guards.push_back(std::make_unique<DocumentSession::SelfWriteGuard>(document->session));
      for (auto* timer : {document->autoSaveTimer, document->draftTimer}) {
        if (timer && timer->isActive()) {
          pausedTimers.append({timer, qMax(1, timer->remainingTime())});
          timer->stop();
        }
      }
    }
  }
  const auto buffers = openSearchBuffers();
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  QProgressDialog progress(tr("Updating files…"), tr("Stop after current file"), 0, 0, this);
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);
  connect(&progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
  QFutureWatcher<WorkspaceReplaceReport> watcher;
  QEventLoop loop;
  connect(&watcher, &QFutureWatcher<WorkspaceReplaceReport>::finished, &loop, &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run([operation, buffers, cancel] { return operation(buffers, cancel); }));
  progress.show();
  loop.exec();
  progress.close();
  const auto report = watcher.result();
  for (const auto& path : report.changed) {
    QFile file(path);
    QString text;
    TextFileFormat format;
    if (!file.open(QIODevice::ReadOnly) || !FileController::decodeTextBytes(file.readAll(), &text, &format))
      continue;
    const auto key = WorkspaceFileIndex::pathKey(path);
    for (const auto& item : frozen)
      for (auto* document : item.window->documentTabs()) {
        if (WorkspaceFileIndex::pathKey(document->session.filePath()) != key ||
            document->session.document().isModified() || document->session.isOpenInProgress())
          continue;
        document->pendingSearchFingerprint.reset();
        document->pendingViewState = item.window->tabViewState(document);
        document->session.replaceDocument(text);
        document->session.setFileFormat(format);
        document->session.recordFileBaseline();
        item.window->clearDraftSnapshot(path, document->draftKey);
        item.window->restoreTabViewState(document);
      }
  }
  guards.clear();
  for (const auto& item : pausedTimers)
    item.timer->start(item.remaining);
  for (const auto& item : frozen) {
    item.window->workspaceWriteInProgress_ = false;
    item.window->centralWidget()->setEnabled(item.centralEnabled);
    item.window->menuBar()->setEnabled(item.menuEnabled);
  }
  return report;
}
