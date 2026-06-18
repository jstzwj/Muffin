#include "app/MainWindow.h"

#include "app/MarkdownSettings.h"
#include "app/PreferencesDialog.h"
#include "app/SidebarWidget.h"
#include "editor/EditorView.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

namespace {

QString elidedPath(const QString& path) {
  const QString nativePath = QDir::toNativeSeparators(path);
  constexpr qsizetype maxLength = 72;
  if (nativePath.size() <= maxLength) {
    return nativePath;
  }
  return QStringLiteral("...") + nativePath.right(maxLength - 3);
}

}  // namespace

void muffin::MainWindow::rebuildRecentFilesMenu() {
  if (!recentFilesMenu_) {
    return;
  }

  recentFilesMenu_->clear();
  const QStringList paths = recentFiles();
  recentFilesMenu_->setEnabled(!paths.isEmpty());

  for (const QString& path : paths) {
    QAction* action = recentFilesMenu_->addAction(elidedPath(path));
    action->setToolTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path] {
      openFile(path);
    });
  }

  if (!paths.isEmpty()) {
    recentFilesMenu_->addSeparator();
    QAction* clearAction = recentFilesMenu_->addAction(tr("Clear Recent Files"));
    connect(clearAction, &QAction::triggered, this, [this] {
      setRecentFiles({});
      rebuildRecentFilesMenu();
    });
  }
}

void muffin::MainWindow::addRecentFile(QString path) {
  if (path.isEmpty()) {
    return;
  }
  // files/recordHistory gates whether opening/saving/moving a file records it
  // into the Open Recent list. Clearing the list (clearRecentFilesRequested) is
  // independent and always available.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/recordHistory"), true).toBool()) {
    return;
  }
  path = QFileInfo(path).absoluteFilePath();

  QStringList paths = recentFiles();
  paths.removeAll(path);
  paths.prepend(path);
  while (paths.size() > 10) {
    paths.removeLast();
  }
  setRecentFiles(paths);
  rebuildRecentFilesMenu();
}

QStringList muffin::MainWindow::recentFiles() const {
  QSettings settings;
  return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void muffin::MainWindow::setRecentFiles(const QStringList& paths) const {
  QSettings settings;
  settings.setValue(QStringLiteral("recentFiles"), paths);
}

void muffin::MainWindow::restoreStartupFile() {
  // files/startupBehavior: 0 = open new file, 1 = reopen last file.
  QSettings settings;
  if (settings.value(QStringLiteral("files/startupBehavior"), 0).toInt() != 1) {
    return;  // default empty document is already in place
  }

  const QStringList recent = recentFiles();
  if (recent.isEmpty()) {
    return;
  }

  const QString path = recent.first();
  // A stale entry (file deleted/moved since it was recorded) must not pop an
  // "Open Failed" error at launch; just fall back to the empty document.
  if (!QFileInfo(path).isFile()) {
    return;
  }

  openFile(path);
}

void muffin::MainWindow::showDocumentProperties() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QFileInfo info(session_.filePath());
  const QString message = tr(
      "Name: %1\n"
      "Location: %2\n"
      "Size: %3 bytes\n"
      "Modified: %4\n"
      "Words: %5\n"
      "Parse time: %6 ms")
                              .arg(
                                  info.fileName(),
                                  QDir::toNativeSeparators(info.absolutePath()),
                                  QString::number(info.size()),
                                  QLocale().toString(info.lastModified(), QLocale::ShortFormat),
                                  QString::number(MainWindow::countWords(session_.markdownText())),
                                  QString::number(session_.lastParseElapsedMs()));
  QMessageBox::information(this, tr("Properties"), message);
}

void muffin::MainWindow::showPreferences() {
  PreferencesDialog dialog(this);
  dialog.setAvailableThemes(themeManager_.availableThemes());
  dialog.setCurrentThemeName(themeManager_.currentThemeName());
  dialog.setStatusBarVisible(statusBar()->isVisible());
  dialog.setZoomPercent(zoomPercent());
  dialog.setFontSizePx(fontSizePx());

  connect(&dialog, &PreferencesDialog::themeRequested, this, [this](const QString& name) {
    if (themeManager_.setTheme(name)) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  connect(&dialog, &PreferencesDialog::statusBarVisibleRequested, this, [this](bool visible) {
    setStatusBarVisible(visible);
    saveAppearanceStatusBarVisible(visible);
  });
  connect(&dialog, &PreferencesDialog::zoomPercentRequested, this, [this](int percent) {
    setZoomPercent(percent);
    saveAppearanceZoomPercent(zoomPercent_);
  });
  connect(&dialog, &PreferencesDialog::fontSizePxRequested, this, [this](int px) {
    setFontSizePx(px);
    saveAppearanceFontSizePx(fontSizePx_);
  });
  connect(&dialog, &PreferencesDialog::clearRecentFilesRequested, this, [this] {
    setRecentFiles({});
    rebuildRecentFilesMenu();
  });
  connect(&dialog, &PreferencesDialog::outlineFoldableChanged, this, [this](bool foldable) {
    if (sidebar_) {
      sidebar_->setOutlineFoldable(foldable);
    }
  });
  connect(&dialog, &PreferencesDialog::restoreDraftsRequested, this, [this] {
    offerDraftRecovery();
  });
  connect(&dialog, &PreferencesDialog::disableTypewriterFocusRequested, this, [this] {
    setTypewriterMode(false);
    setFocusMode(false);
    saveAppearanceTypewriterMode(false);
    saveAppearanceFocusMode(false);
  });

  dialog.exec();

  // Re-apply markdown preferences now that the modal dialog has closed. Doing this AFTER exec()
  // (rather than live during the dialog) is deliberate: a full setDocument re-render while the
  // modal PreferencesDialog is open races its window-blocking and can blank the viewport (see the
  // spell-check note in MainWindowSignalBinder.cpp). setParseOptions is a no-op when the parse
  // options are unchanged; refreshVisibleBlocks picks up layout-only changes (e.g. codeBlockWrap)
  // without a re-parse.
  session_.setParseOptions(markdownParseOptions());
  if (renderView_) {
    renderView_->refreshVisibleBlocks(session_.document());
  }
}

void muffin::MainWindow::revealCurrentFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }
  const QFileInfo info(session_.filePath());
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

bool muffin::MainWindow::saveCurrentDocument() {
  if (fileController_.save(session_, this)) {
    addRecentFile(session_.filePath());
    drafts_.markClean(session_.filePath());
    return true;
  }
  return false;
}

void muffin::MainWindow::performAutoSave() {
  // Silent write of a pathed, modified document when files/autoSave is on. Untitled
  // documents (no filePath) are handled by draft-recovery snapshots, not here.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/autoSave"), false).toBool()) {
    return;
  }
  if (session_.filePath().isEmpty() || !session_.document().isModified()) {
    return;
  }
  if (fileController_.save(session_, this)) {
    drafts_.markClean(session_.filePath());
  }
}

void muffin::MainWindow::snapshotDraft() {
  // Persist a recovery snapshot while the document has unsaved content, so a
  // crash or forced exit can be restored on the next launch. Cleared on save.
  if (!session_.document().isModified()) {
    return;
  }
  const QString text = session_.markdownText();
  if (!text.isEmpty() && text != lastDraftSnapshotText_) {
    drafts_.snapshot(text, session_.filePath());
    lastDraftSnapshotText_ = text;
  }
  // Heartbeat: re-arm for as long as the document stays dirty. modifiedChanged
  // only fires on the clean↔dirty transition, so without this re-arm a single
  // snapshot would be taken seconds into an edit session and never refreshed —
  // leaving everything typed afterwards unrecoverable. The content check above
  // keeps the heartbeat's cost to a string comparison when the user pauses.
  draftTimer_->start();
}

bool muffin::MainWindow::offerDraftRecovery() {
  // Drop drafts whose source file vanished plus crash-leftover half-pairs first,
  // so the dialog only ever shows drafts that are genuinely restorable.
  drafts_.pruneOrphaned();
  const QVector<DraftRecovery::PendingDraft> drafts = drafts_.pendingDrafts();
  if (drafts.isEmpty()) {
    return false;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Unsaved Drafts Found"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* label = new QLabel(&dialog);
  label->setWordWrap(true);
  label->setText(tr("Muffin found %n unsaved draft(s) from a previous session. "
                    "Restore one into this window, discard all of them, or keep them for later.",
                    nullptr, drafts.size()));
  layout->addWidget(label);

  auto* list = new QListWidget(&dialog);
  // Single selection: Muffin is a single-document window, so only one draft can
  // be the active document. Leftover drafts stay on disk for the next launch.
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  for (const DraftRecovery::PendingDraft& d : drafts) {
    const QString time = QDateTime::fromMSecsSinceEpoch(d.timestamp)
                              .toString(QLocale().dateTimeFormat(QLocale::ShortFormat));
    const QString where = d.sourcePath.isEmpty() ? tr("Untitled") : QDir::toNativeSeparators(d.sourcePath);
    auto* item = new QListWidgetItem(
        tr("%1  —  %2  (%n char(s))", nullptr, int(d.charCount)).arg(where, time), list);
    item->setData(Qt::UserRole, QVariant::fromValue(d));
  }
  list->setCurrentRow(0);  // pre-select the newest draft for a one-click restore
  list->setMinimumHeight(qMax(120, 28 + drafts.size() * 26));
  layout->addWidget(list);

  auto* buttonRow = new QHBoxLayout;
  auto* restoreBtn = new QPushButton(tr("Restore"), &dialog);
  auto* discardBtn = new QPushButton(tr("Discard All"), &dialog);
  auto* laterBtn = new QPushButton(tr("Later"), &dialog);
  restoreBtn->setDefault(true);
  buttonRow->addWidget(restoreBtn);
  buttonRow->addWidget(discardBtn);
  buttonRow->addStretch(1);
  buttonRow->addWidget(laterBtn);
  layout->addLayout(buttonRow);

  enum Result { Restore = 10, Discard = 20 };
  QObject::connect(restoreBtn, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(Restore); });
  QObject::connect(discardBtn, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(Discard); });
  QObject::connect(laterBtn, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(int(QDialog::Rejected)); });

  const int result = dialog.exec();
  bool restoredAny = false;
  if (result == Restore) {
    if (list->currentItem() != nullptr) {
      restoreDraft(list->currentItem()->data(Qt::UserRole).value<DraftRecovery::PendingDraft>());
      restoredAny = true;
    }
  } else if (result == Discard) {
    for (const DraftRecovery::PendingDraft& d : drafts) {
      drafts_.discard(d);
    }
  }
  // Later / window close: leave the drafts in place for the next launch.
  return restoredAny;
}

void muffin::MainWindow::restoreDraft(const DraftRecovery::PendingDraft& draft) {
  const QString content = drafts_.loadDraft(draft);
  if (content.isEmpty()) {
    return;
  }
  if (!draft.sourcePath.isEmpty() && QFileInfo(draft.sourcePath).isFile()) {
    // Reopen the on-disk file (restores path and undo history), then overlay the
    // recovered content and mark it modified — the recovered text is the newer version.
    openFile(draft.sourcePath);
    editorController_.clearHistoryAndSelection();
    session_.setMarkdownText(content, true);
  } else {
    // Untitled draft, or the source file no longer exists: load into a new untitled doc.
    fileController_.newFile(session_, this);
    editorController_.clearHistoryAndSelection();
    session_.setMarkdownText(content, true);
  }
  // The restored draft becomes the active document; its key is reused by the next
  // snapshot, so we don't discard here.
}

bool muffin::MainWindow::isDocumentModified() const {
  return session_.document().isModified();
}

void muffin::MainWindow::buildReopenEncodingMenu() {
  if (!reopenEncodingMenu_) {
    return;
  }

  reopenEncodingMenu_->clear();

  // name = ICU canonical name, label = display text
  struct Entry {
    const char* name;
    const char* label;
  };

  struct Group {
    const char* title;
    std::vector<Entry> entries;
  };

  static const Group groups[] = {
      {QT_TRANSLATE_NOOP("MainWindow", "Unicode"),
       {
           {"UTF-8", QT_TRANSLATE_NOOP("MainWindow", "UTF-8")},
           {"UTF-16LE", QT_TRANSLATE_NOOP("MainWindow", "UTF-16 LE")},
           {"UTF-16BE", QT_TRANSLATE_NOOP("MainWindow", "UTF-16 BE")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Western"),
       {
           {"windows-1252", QT_TRANSLATE_NOOP("MainWindow", "Western (Windows-1252)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Cyrillic"),
       {
           {"windows-1251", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (Windows-1251)")},
           {"ISO-8859-5", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (ISO-8859-5)")},
           {"IBM866", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (IBM866)")},
           {"IBM855", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (IBM855)")},
           {"KOI8-R", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (KOI8-R)")},
           {"x-mac-cyrillic", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (Mac)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Central European"),
       {
           {"windows-1250", QT_TRANSLATE_NOOP("MainWindow", "Central European (Windows-1250)")},
           {"ISO-8859-2", QT_TRANSLATE_NOOP("MainWindow", "Central European (ISO-8859-2)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Greek"),
       {
           {"windows-1253", QT_TRANSLATE_NOOP("MainWindow", "Greek (Windows-1253)")},
           {"ISO-8859-7", QT_TRANSLATE_NOOP("MainWindow", "Greek (ISO-8859-7)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Hebrew"),
       {
           {"windows-1255", QT_TRANSLATE_NOOP("MainWindow", "Hebrew (Windows-1255)")},
           {"ISO-8859-8", QT_TRANSLATE_NOOP("MainWindow", "Hebrew (ISO-8859-8)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified"),
       {
           {"GB2312", QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified (GB2312)")},
           {"GB18030", QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified (GB18030)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Chinese Traditional"),
       {
           {"Big5", QT_TRANSLATE_NOOP("MainWindow", "Chinese Traditional (Big5)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Japanese"),
       {
           {"Shift_JIS", QT_TRANSLATE_NOOP("MainWindow", "Japanese (Shift_JIS)")},
           {"EUC-JP", QT_TRANSLATE_NOOP("MainWindow", "Japanese (EUC-JP)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Korean"),
       {
           {"EUC-KR", QT_TRANSLATE_NOOP("MainWindow", "Korean (EUC-KR)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Thai"),
       {
           {"TIS-620", QT_TRANSLATE_NOOP("MainWindow", "Thai (TIS-620)")},
       }},
  };

  for (const auto& group : groups) {
    for (const auto& entry : group.entries) {
      QAction* action = reopenEncodingMenu_->addAction(tr(entry.label));
      const QString encodingName = QString::fromLatin1(entry.name);
      connect(action, &QAction::triggered, this, [this, encodingName] {
        reopenWithEncoding(encodingName);
      });
    }
    reopenEncodingMenu_->addSeparator();
  }
}

void muffin::MainWindow::reopenWithEncoding(const QString& encodingName) {
  if (session_.filePath().isEmpty()) {
    return;
  }

  if (fileController_.reopenWithEncoding(session_, this, encodingName)) {
    editorController_.clearHistoryAndSelection();
  }
}

void muffin::MainWindow::moveToFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QString oldPath = session_.filePath();
  if (!fileController_.moveTo(session_, this)) {
    return;
  }

  QStringList recent = recentFiles();
  recent.removeAll(QFileInfo(oldPath).absoluteFilePath());
  setRecentFiles(recent);
  addRecentFile(session_.filePath());
  editorController_.clearHistoryAndSelection();
}

void muffin::MainWindow::saveAllOpenFiles() {
  int savedCount = 0;
  int failedCount = 0;

  for (QWidget* widget : QApplication::topLevelWidgets()) {
    auto* window = qobject_cast<MainWindow*>(widget);
    if (!window) {
      continue;
    }
    if (window->isDocumentModified()) {
      if (window->saveCurrentDocument()) {
        ++savedCount;
      } else {
        ++failedCount;
      }
    }
  }

  if (failedCount > 0) {
    QMessageBox::warning(this, tr("Save All"),
                         tr("Saved %1 file(s). %2 file(s) could not be saved.")
                             .arg(savedCount)
                             .arg(failedCount));
  }
}

void muffin::MainWindow::showInSidebar() {
  if (session_.filePath().isEmpty()) {
    return;
  }
  setSidebarPanel(SidebarWidget::Panel::Files);
  sidebar_->setCurrentDocument(
      session_.displayName(), session_.filePath(),
      session_.document().isModified());
}

void muffin::MainWindow::deleteFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QString filePath = session_.filePath();
  const QString fileName = QFileInfo(filePath).fileName();

  const QMessageBox::StandardButton confirm = QMessageBox::warning(
      this, tr("Delete File"),
      tr("Are you sure you want to move \"%1\" to the trash?\n\n"
         "This action cannot be undone.")
          .arg(fileName),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No);

  if (confirm != QMessageBox::Yes) {
    return;
  }

  bool trashed = QFile::moveToTrash(filePath);

  if (!trashed) {
    const QMessageBox::StandardButton confirm2 = QMessageBox::critical(
        this, tr("Delete File"),
        tr("Could not move to trash. Permanently delete \"%1\"?\n\n"
           "This action cannot be undone.")
            .arg(fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (confirm2 != QMessageBox::Yes) {
      return;
    }

    if (!QFile::remove(filePath)) {
      QMessageBox::critical(this, tr("Delete Failed"),
                            tr("Could not delete file:\n%1").arg(filePath));
      return;
    }
  }

  fileController_.newFile(session_, this);
  editorController_.clearHistoryAndSelection();

  QStringList recent = recentFiles();
  recent.removeAll(QFileInfo(filePath).absoluteFilePath());
  setRecentFiles(recent);
  rebuildRecentFilesMenu();
}

bool muffin::MainWindow::maybeSaveChanges() {
  if (!session_.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      this,
      tr("Muffin"),
      tr("The current document has unsaved changes."),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return fileController_.save(session_, this);  // save() emits documentBecameClean.
  }
  // Discard: the user explicitly abandoned the unsaved work — drop its recovery
  // draft so the next launch doesn't offer to restore what was just thrown away.
  drafts_.markClean(session_.filePath());
  return true;
}
