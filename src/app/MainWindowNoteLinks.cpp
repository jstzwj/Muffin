#include "app/MainWindow.h"
#include "app/NoteLinks.h"
#include "io/FilePathOps.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

void muffin::MainWindow::movePath(QString path) {
  const auto directory = QFileDialog::getExistingDirectory(this, tr("Move To"), QFileInfo(path).absolutePath());
  if (!directory.isEmpty())
    renameOrMoveWithLinks(path, QDir(directory).filePath(QFileInfo(path).fileName()));
}

bool muffin::MainWindow::renameOrMoveWithLinks(const QString& oldPath, const QString& newPath) {
  if (oldPath == newPath)
    return true;
  if (workspaceWriteInProgress_ || pendingNavigation_)
    return false;
  if ((QFileInfo(newPath).exists() && WorkspaceFileIndex::pathKey(oldPath) != WorkspaceFileIndex::pathKey(newPath)) ||
      (QFileInfo(oldPath).isDir() && FilePathOps::isSameOrDescendant(QFileInfo(newPath).absolutePath(), oldPath))) {
    QMessageBox::warning(this, tr("Rename / Move"),
                         tr("The destination already exists or is inside the folder being moved."));
    return false;
  }
  const QString root = WorkspaceFileIndex::contains(sidebarFolderRoot_, oldPath) ? sidebarFolderRoot_
                                                                                 : QFileInfo(oldPath).absolutePath();
  const auto buffers = openSearchBuffers();
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  QProgressDialog progress(tr("Checking links…"), tr("Cancel"), 0, 0, this);
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);
  connect(&progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
  QFutureWatcher<NoteLinkPlan> watcher;
  QEventLoop loop;
  connect(&watcher, &QFutureWatcher<NoteLinkPlan>::finished, &loop, &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run([=] { return NoteLinks::prepare(root, oldPath, newPath, buffers, cancel); }));
  progress.show();
  loop.exec();
  disconnect(&progress, nullptr, this, nullptr); // Closing a QProgressDialog emits canceled too.
  progress.close();
  if (cancel->load())
    return false;
  auto plan = watcher.result();
  bool update = false;
  if (!plan.changes.isEmpty() || !plan.skipped.isEmpty() || !plan.complete) {
    QDialog dialog(this);
    dialog.setObjectName("NoteLinkPreview");
    dialog.setWindowTitle(tr("Preview Link Updates"));
    dialog.resize(880, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(tr("%1 → %2\nSearch folder: %3")
                                       .arg(QDir::toNativeSeparators(QDir(root).relativeFilePath(oldPath)),
                                            QDir::toNativeSeparators(QDir(root).relativeFilePath(newPath)),
                                            QDir::toNativeSeparators(root)),
                                   &dialog);
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* splitter = new QSplitter(Qt::Vertical, &dialog);
    layout->addWidget(splitter, 1);
    auto* files = new QTreeWidget(splitter);
    files->setObjectName("NoteLinkFiles");
    files->setHeaderLabels({tr("File"), tr("Links")});
    files->setColumnWidth(0, 600);
    for (qsizetype i = 0; i < plan.changes.size(); ++i) {
      const auto& change = plan.changes[i];
      auto* item = new QTreeWidgetItem(
          files, {QDir(root).relativeFilePath(change.file.path) + (change.unsaved ? tr(" (unsaved)") : QString()),
                  QString::number(change.file.matches)});
      item->setData(0, Qt::UserRole, i);
      item->setCheckState(0, Qt::Checked);
      item->setToolTip(0, change.file.path);
    }
    for (const auto& path : plan.skipped) {
      auto* item = new QTreeWidgetItem(files, {QDir(root).relativeFilePath(path), tr("Skipped")});
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    auto* preview = new QPlainTextEdit(splitter);
    preview->setObjectName("NoteLinkDiff");
    preview->setReadOnly(true);
    connect(files, &QTreeWidget::currentItemChanged, &dialog, [&, preview](QTreeWidgetItem* item) {
      if (!item || !item->data(0, Qt::UserRole).isValid()) {
        preview->clear();
        return;
      }
      const auto& change = plan.changes[item->data(0, Qt::UserRole).toInt()].file;
      // Show changed lines only, so a long note opens directly on its affected links.
      const auto before = change.beforeText.split('\n'), after = change.afterText.split('\n');
      QStringList diff;
      for (qsizetype i = 0; i < before.size() && i < after.size(); ++i) {
        if (before[i] != after[i])
          diff << QStringLiteral("%1  − %2\n%1  + %3").arg(i + 1).arg(before[i], after[i]);
        if (diff.size() >= 200)
          break;
      }
      preview->setPlainText(diff.join(QStringLiteral("\n\n")));
    });
    if (!plan.changes.isEmpty())
      files->setCurrentItem(files->topLevelItem(0));
    auto* warning = new QLabel(
        tr("Updates Markdown links, reference definitions and Markdown images in this folder. Code and external URLs are excluded. Unsaved notes stay unsaved and can be undone. Disk originals are backed up.\nHidden folders, build folders, symbolic links, unreadable files and files over 8 MB are excluded; links outside this folder are not scanned."),
        &dialog);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    if (!plan.complete || !plan.skipped.isEmpty()) {
      auto* incomplete =
          new QLabel(tr("Some files could not be checked. Their links may need manual updates."), &dialog);
      incomplete->setWordWrap(true);
      layout->addWidget(incomplete);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* only = buttons->addButton(tr("Rename / Move Only"), QDialogButtonBox::ActionRole);
    only->setObjectName("NoteLinkOnly");
    auto* apply = buttons->addButton(tr("Update Links"), QDialogButtonBox::AcceptRole);
    apply->setObjectName("NoteLinkApply");
    apply->setEnabled(plan.complete && !plan.changes.isEmpty());
    connect(only, &QPushButton::clicked, &dialog, [&] { dialog.done(2); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    const int result = dialog.exec();
    if (result == QDialog::Rejected)
      return false;
    update = result == QDialog::Accepted;
    if (update)
      for (int i = int(plan.changes.size()) - 1; i >= 0; --i)
        if (files->topLevelItem(i)->checkState(0) != Qt::Checked)
          plan.changes.removeAt(i);
  }
  if (!update)
    plan.changes.clear();
  const QString recovery =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("note-link-updates");
  bool moved = false;
  auto report = runWorkspaceWrite([&](const OpenSearchBuffers& current, const SearchCancellation& writeCancel) {
    WorkspaceReplaceReport result;
    QVector<WorkspaceChange> disk;
    // Preview snapshots are checked again while editing and autosave are frozen.
    for (const auto& change : plan.changes) {
      const auto key = WorkspaceFileIndex::pathKey(change.file.path);
      const auto buffer = current.constFind(key);
      if (buffer != current.cend() &&
          (buffer->busy || buffer->text != change.file.beforeText || buffer->modified != change.unsaved)) {
        result.error = tr("A note changed after the preview. Please try again.");
        return result;
      }
      if (change.unsaved && buffer == current.cend()) {
        result.error = tr("A note changed after the preview. Please try again.");
        return result;
      }
      QFile file(change.file.path);
      if (!file.open(QIODevice::ReadOnly) || file.readAll() != change.file.before) {
        result.error = tr("A file changed on disk after the preview. Please try again.");
        return result;
      }
      if (!change.unsaved)
        disk.append(change.file);
    }
    result = WorkspaceReplace::apply(root, disk, recovery, writeCancel);
    const auto rollback = [&] {
      if (result.backupDirectory.isEmpty())
        return;
      const auto restored = WorkspaceReplace::restore(result.backupDirectory, {}, {});
      if (!restored.skipped.isEmpty() || !restored.error.isEmpty())
        result.error += tr("\nSome originals could not be restored. Backups: %1").arg(result.backupDirectory);
      result.changed.removeIf([&](const QString& path) { return restored.changed.contains(path); });
    };
    if (!result.error.isEmpty() || result.changed.size() != disk.size() || writeCancel->load()) {
      if (result.error.isEmpty())
        result.error = tr("Link updates could not be completed. The path was not changed.");
      rollback();
      return result;
    }
    if (!result.backupDirectory.isEmpty()) {
      QSaveFile journal(QDir(result.backupDirectory).filePath("rename.json"));
      const auto metadata = QJsonDocument(QJsonObject{{"oldPath", oldPath}, {"newPath", newPath}}).toJson();
      if (!journal.open(QIODevice::WriteOnly) || journal.write(metadata) != metadata.size() || !journal.commit()) {
        result.error = journal.errorString();
        rollback();
        return result;
      }
    }
    QString error;
    if (!FilePathOps::renamePath(oldPath, newPath, &error)) {
      result.error = tr("Could not rename or move: %1").arg(error);
      rollback();
      return result;
    }
    moved = true;
    for (auto& path : result.changed)
      path = FilePathOps::remapDescendant(path, oldPath, newPath);
    return result;
  });
  if (!moved) {
    QMessageBox::warning(this, tr("Rename / Move"), report.error);
    return false;
  }
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* window = qobject_cast<MainWindow*>(widget);
    if (!window)
      continue;
    window->remapNavigationPaths(oldPath, newPath);
    for (auto* document : window->documentTabs()) {
      if (!document->pendingOpenPath.isEmpty())
        document->pendingOpenPath = FilePathOps::remapDescendant(document->pendingOpenPath, oldPath, newPath);
      const auto previous = document->session.filePath();
      const auto remapped = FilePathOps::remapDescendant(previous, oldPath, newPath);
      if (previous != remapped) {
        document->session.setFilePath(remapped);
        document->session.recordFileBaseline();
      }
      for (const auto& change : plan.changes) {
        if (WorkspaceFileIndex::pathKey(remapped) !=
            WorkspaceFileIndex::pathKey(FilePathOps::remapDescendant(change.file.path, oldPath, newPath)))
          continue;
        if (change.unsaved) {
          if (document->session.markdownText().toString() != change.file.beforeText)
            continue;
          document->pendingViewState = window->tabViewState(document);
          if (document->backend->isSourceMode()) {
            document->editor->replaceAllWithUndo(change.file.afterText);
          } else {
            document->editorController.applyMarkdownTextWithUndo(change.file.afterText, tr("Update Note Links"));
          }
        } else if (previous != remapped) {
          document->pendingViewState = window->tabViewState(document);
          document->session.replaceDocument(change.file.afterText);
          document->session.recordFileBaseline();
        }
      }
      window->restoreTabViewState(document);
    }
    auto recent = window->recentFiles();
    for (auto& path : recent)
      path = FilePathOps::remapDescendant(path, oldPath, newPath);
    window->setRecentFiles(recent);
    auto folders = window->recentFolders();
    for (auto& path : folders)
      path = FilePathOps::remapDescendant(path, oldPath, newPath);
    window->setRecentFolders(folders);
    if (FilePathOps::isSameOrDescendant(window->sidebarFolderRoot_, oldPath)) {
      const bool pinned = window->sidebar_->folderPinned();
      window->openFolderAtPath(FilePathOps::remapDescendant(window->sidebarFolderRoot_, oldPath, newPath));
      window->sidebar_->setFolderPinned(pinned);
    }
    if (window->sidebar_)
      window->sidebar_->setCurrentPath(newPath);
    window->rebuildRecentFilesMenu();
    window->saveSessionState();
  }
  if (!report.backupDirectory.isEmpty())
    statusBar()->showMessage(
        tr("Links updated. Originals backed up in %1").arg(QDir::toNativeSeparators(report.backupDirectory)), 10000);
  return true;
}
