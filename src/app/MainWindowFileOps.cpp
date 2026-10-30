#include "app/MainWindow.h"
#include "diagnostics/ScopedPerfProbe.h"

#include "app/MarkdownSettings.h"
#include "app/PreferencesDialog.h"
#include "app/QuickOpenDialog.h"
#include "app/SidebarWidget.h"
#include "app/RenderEditorBackend.h"
#include "app/SourceEditorBackend.h"
#include "document/TextStats.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include "export/HtmlExporter.h"
#include "export/PandocRunner.h"
#include "export/ExportOptions.h"
#include "export/NativeExporter.h"
#include "io/FilePathOps.h"
#include "theme/ThemeDefinition.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTabWidget>
#include <QScopedValueRollback>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(fileOpsPerf, "muffin.perf.detail", QtWarningMsg)

QString elidedPath(const QString& path) {
  const QString nativePath = QDir::toNativeSeparators(path);
  constexpr qsizetype maxLength = 72;
  if (nativePath.size() <= maxLength) {
    return nativePath;
  }
  return QStringLiteral("...") + nativePath.right(maxLength - 3);
}

} // namespace

void muffin::MainWindow::rebuildRecentFilesMenu() {
  if (!recentFilesMenu_) {
    return;
  }

  recentFilesMenu_->clear();
  const QStringList files = recentFiles();
  const QStringList folders = recentFolders();
  recentFilesMenu_->setEnabled(!files.isEmpty() || !folders.isEmpty());

  // The menu is split by a separator: recent files on top, recent folders below.
  bool addedAny = false;

  // ---- Recent files ----
  for (const QString& path : files) {
    QAction* action = recentFilesMenu_->addAction(elidedPath(path));
    action->setToolTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
    addedAny = true;
  }
  if (!files.isEmpty()) {
    recentFilesMenu_->addSeparator();
    QAction* clearAction = recentFilesMenu_->addAction(tr("Clear Recent Files"));
    connect(clearAction, &QAction::triggered, this, [this] {
      setRecentFiles({});
      rebuildRecentFilesMenu();
    });
    addedAny = true;
  }

  // ---- Recent folders ----
  if (!folders.isEmpty()) {
    if (addedAny) {
      recentFilesMenu_->addSeparator();
    }
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon, nullptr, this);
    for (const QString& path : folders) {
      QAction* action = recentFilesMenu_->addAction(folderIcon, elidedPath(path));
      action->setToolTip(QDir::toNativeSeparators(path));
      connect(action, &QAction::triggered, this, [this, path] {
        // Prune stale entries (folder removed since it was recorded).
        if (!QFileInfo::exists(path) || !QFileInfo(path).isDir()) {
          QStringList kept = recentFolders();
          kept.removeAll(path);
          setRecentFolders(kept);
          rebuildRecentFilesMenu();
          return;
        }
        openFolderAtPath(path);
      });
    }
    recentFilesMenu_->addSeparator();
    QAction* clearAction = recentFilesMenu_->addAction(tr("Clear Recent Folders"));
    connect(clearAction, &QAction::triggered, this, [this] {
      setRecentFolders({});
      rebuildRecentFilesMenu();
    });
  }
}

void muffin::MainWindow::rebuildThemesMenu() {
  if (!themesMenu_) {
    return;
  }
  // Enumerate every theme the manager knows (built-ins + imported customs) so
  // the menu reflects the registry, not a fixed list. Theme names are proper
  // nouns, so (like the recent-files submenu) this isn't rebuilt on locale
  // change — see the heap-corruption note in retranslateUi.
  themesMenu_->clear();
  const QString current = themeManager_.currentThemeName();
  for (const auto& d : themeManager_.themes()) {
    QAction* action = themesMenu_->addAction(d.label.isEmpty() ? d.id : d.label);
    action->setCheckable(true);
    action->setChecked(d.id == current);
    action->setData(d.id);
    connect(action, &QAction::triggered, this, [this, id = d.id] { setThemeByName(id); });
  }
  themesMenu_->addSeparator();
  QAction* importAction = themesMenu_->addAction(tr("Import Theme..."));
  connect(importAction, &QAction::triggered, this, [this] { importTheme(); });
  QAction* folderAction = themesMenu_->addAction(tr("Open Themes Folder"));
  connect(folderAction, &QAction::triggered, this, [this] { openThemesFolder(); });
}

void muffin::MainWindow::updateThemeChecks() {
  if (!themesMenu_) {
    return;
  }
  // The theme actions are the checkable ones (each carries its theme id as
  // data()); the Import/Open-folder actions below them are not checkable, so
  // isCheckable() filters them out.
  const QString current = themeManager_.currentThemeName();
  for (QAction* action : themesMenu_->actions()) {
    if (action->isCheckable()) {
      action->setChecked(action->data().toString() == current);
    }
  }
}

void muffin::MainWindow::setThemeByName(const QString& name) {
  if (themeManager_.setTheme(name)) {
    saveAppearanceTheme(themeManager_.currentThemeName());
    rebuildThemesMenu(); // refresh the radio-style checks
  }
}

void muffin::MainWindow::importTheme() {
  const QString src =
      QFileDialog::getOpenFileName(this, tr("Import Theme"), QString(), tr("Theme Files (*.css *.json)"));
  if (src.isEmpty()) {
    return;
  }
  const QString id = QFileInfo(src).baseName().toLower();
  // A custom file whose id matches a built-in is intentionally ignored by the
  // loader (built-ins stay canonical), so refuse it up front with a clear reason.
  if (ThemeDefinition::builtIn(id).has_value()) {
    QMessageBox::information(this, tr("Import Theme"),
                             tr("A built-in theme named \"%1\" already exists; choose a different file name.").arg(id));
    return;
  }
  // Validate before copying so a malformed file never lands in the themes dir.
  // CSS themes go through the community-CSS interpreter; JSON themes use the
  // native Muffin schema.
  ThemeDefinition probe;
  if (src.endsWith(QStringLiteral(".css"), Qt::CaseInsensitive)) {
    probe = ThemeDefinition::fromCss(src, id);
  } else {
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
      QMessageBox::warning(this, tr("Import Theme"), tr("Could not read the selected file."));
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) {
      QMessageBox::warning(this, tr("Import Theme"), tr("The selected file is not a valid theme."));
      return;
    }
    probe = ThemeDefinition::fromJson(doc.object(), id);
  }
  if (!probe.valid()) {
    QMessageBox::warning(this, tr("Import Theme"),
                         tr("The theme file is missing required colours (background and text)."));
    return;
  }

  const QString dir = ThemeManager::themesDirectory();
  QDir().mkpath(dir);
  if (src.endsWith(QStringLiteral(".css"), Qt::CaseInsensitive)) {
    // Install as a multi-file mirror: the top .css verbatim + every local file
    // it transitively references (@import'd base sheets, @font-face fonts, url()
    // images) copied next to it with relative paths preserved. The theme then
    // resolves identically to its source folder — e.g. my-theme.css paired with
    // a my-theme/ folder carrying the base CSS and .ttf fonts. This
    // replaces the former @import-inlining import, which flattened the theme to
    // one file and broke the @font-face font urls in the @import'd base.
    if (!ThemeManager::installCssTheme(src, dir)) {
      QMessageBox::warning(this, tr("Import Theme"), tr("Could not read the selected file."));
      return;
    }
  } else {
    const QString dest = dir + QDir::separator() + QFileInfo(src).fileName().toLower();
    if (QFileInfo::exists(dest)) {
      QFile::remove(dest);
    }
    if (!QFile::copy(src, dest)) {
      QMessageBox::warning(this, tr("Import Theme"), tr("Could not copy the theme into the themes folder."));
      return;
    }
  }

  themeManager_.reloadCustomThemes();
  rebuildThemesMenu();
  setThemeByName(id); // apply immediately so the user sees the imported theme
}

void muffin::MainWindow::openThemesFolder() {
  const QString dir = ThemeManager::themesDirectory();
  QDir().mkpath(dir);
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
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

QStringList muffin::MainWindow::recentFolders() const {
  QSettings settings;
  return settings.value(QStringLiteral("recentFolders")).toStringList();
}

void muffin::MainWindow::setRecentFolders(const QStringList& paths) const {
  QSettings settings;
  settings.setValue(QStringLiteral("recentFolders"), paths);
}

void muffin::MainWindow::addRecentFolder(QString path) {
  if (path.isEmpty()) {
    return;
  }
  // Same files/recordHistory gate as recent files.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/recordHistory"), true).toBool()) {
    return;
  }
  path = QFileInfo(path).absoluteFilePath();

  QStringList paths = recentFolders();
  paths.removeAll(path);
  paths.prepend(path);
  while (paths.size() > 10) {
    paths.removeLast();
  }
  setRecentFolders(paths);
  rebuildRecentFilesMenu();
}

QString muffin::MainWindow::defaultSaveDirectory() const {
  return sidebarFolderRoot_;
}

void muffin::MainWindow::restoreStartupFile() {
  // 0 = always start fresh, 1 = restore the actual previous session (default).
  QSettings settings;
  if (settings.value(QStringLiteral("files/startupBehavior"), 1).toInt() != 1)
    return;
  const auto snapshot = settings.value(QStringLiteral("session/window")).toMap();
  restoreSessionState(snapshot);
}

void muffin::MainWindow::restoreSessionState(const QVariantMap& snapshot) {
  if (snapshot.value(QStringLiteral("version")).toInt() != 1)
    return;
  {
    const QScopedValueRollback<bool> guard(sessionUpdatesPaused_, true);
    // Explicitly suppress the automatic parent-folder fallback: an empty saved
    // workspace means the user closed it, even if some files remain open.
    sidebar_->setFolderRoot({});
    const QString folder = snapshot.value(QStringLiteral("folder")).toString();
    if (QFileInfo(folder).isDir())
      openFolderAtPath(folder);
    if (snapshot.contains(QStringLiteral("folderState")))
      sidebar_->restoreFolderState(snapshot.value(QStringLiteral("folderState")).toMap());
    const QStringList files = snapshot.value(QStringLiteral("files")).toStringList();
    const QVariantList views = snapshot.value(QStringLiteral("views")).toList();
    const int active = snapshot.value(QStringLiteral("active"), -1).toInt();
    DocumentTab* selected = nullptr;
    bool restoredAny = false;
    for (int i = 0; i < files.size(); ++i) {
      const QString& path = files[i];
      if (path.isEmpty()) {
        if (restoredAny)
          startNewDocument();
      } else {
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable())
          continue;
        if (restoredAny && tab_->session.filePath().isEmpty() && tab_->pendingOpenPath.isEmpty())
          startNewDocument();
        if (!openFile(path))
          continue;
      }
      restoredAny = true;
      if (i < views.size()) {
        const auto view = views[i].toMap();
        if (view.value(QStringLiteral("version")).toInt() == 1) {
          tab_->pinned = view.value(QStringLiteral("pinned")).toBool();
          setupTabButton(tab_);
          tab_->pendingViewState = view;
          const bool source = view.value(QStringLiteral("sourceMode")).toBool();
          if (source)
            tab_->backend = std::make_unique<SourceEditorBackend>(tab_->editor);
          else
            tab_->backend =
                std::make_unique<RenderEditorBackend>(tab_->editorController, tab_->session, tab_->renderView);
          tab_->viewStack->setCurrentWidget(source ? static_cast<QWidget*>(tab_->editor)
                                                   : static_cast<QWidget*>(tab_->renderView));
        }
      }
      if (i == active)
        selected = tab_;
    }
    normalizePinnedTabs();
    if (selected)
      tabs_->setCurrentWidget(selected);
    updateTabBarVisibility();
  }
  refreshSidebarDocumentInfo();
  if (snapshot.contains(QStringLiteral("folderState")))
    sidebar_->restoreFolderState(snapshot.value(QStringLiteral("folderState")).toMap());
  else
    sidebar_->restoreExpandedFolders(snapshot.value(QStringLiteral("expanded")).toStringList());
  activateTab(tabs_->currentIndex());
  saveSessionState();
}

void muffin::MainWindow::showDocumentProperties() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }

  const QFileInfo info(tab_->session.filePath());
  const QString message =
      tr("Name: %1\n"
         "Location: %2\n"
         "Size: %3 bytes\n"
         "Modified: %4\n"
         "Words: %5\n"
         "Parse time: %6 ms")
          .arg(info.fileName(), QDir::toNativeSeparators(info.absolutePath()), QString::number(info.size()),
               QLocale().toString(info.lastModified(), QLocale::ShortFormat),
               QString::number(text_stats::countWords(tab_->session.markdownText())),
               QString::number(tab_->session.lastParseElapsedMs()));
  QMessageBox::information(this, tr("Properties"), message);
}

void muffin::MainWindow::showPreferences() {
  PreferencesDialog dialog(this);
  // Build (id, label) pairs from the registry so custom themes show their
  // authored label. Captured as a lambda so the import handler can rebuild the
  // same list after the registry changes.
  auto themeOptions = [this]() {
    QVector<QPair<QString, QString>> options;
    for (const auto& d : themeManager_.themes()) {
      options.append({d.id, d.label.isEmpty() ? d.id : d.label});
    }
    return options;
  };
  dialog.setAvailableThemes(themeOptions());
  dialog.setCurrentThemeName(themeManager_.currentThemeName());
  dialog.setThemeDefinition(themeManager_.currentDefinition());
  dialog.setStatusBarVisible(statusBar()->isVisible());
  dialog.setZoomPercent(zoomPercent());
  dialog.setFontSizePx(fontSizePx());
  dialog.setContentWidthPx(contentWidthPx());

  connect(&dialog, &PreferencesDialog::themeRequested, this, [this, &dialog](const QString& name) {
    if (themeManager_.setTheme(name)) {
      saveAppearanceTheme(themeManager_.currentThemeName());
      // Live-preview: re-theme the open dialog too so picking a theme here shows
      // the result immediately (the "preferences should follow the theme" link).
      dialog.setThemeDefinition(themeManager_.currentDefinition());
    }
  });
  connect(&dialog, &PreferencesDialog::importThemeRequested, this, [this, &dialog, themeOptions] {
    importTheme();
    // importTheme() reloads the registry (and applies the new theme); refresh
    // the dropdown so the imported theme appears and the selection tracks it.
    dialog.setAvailableThemes(themeOptions());
    dialog.setCurrentThemeName(themeManager_.currentThemeName());
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
  connect(&dialog, &PreferencesDialog::contentWidthPxRequested, this, [this](int px) {
    setContentWidthPx(px);
    saveAppearanceContentWidthPx(contentWidthPx_);
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
  connect(&dialog, &PreferencesDialog::restoreDraftsRequested, this, [this] { offerDraftRecovery(); });
  connect(&dialog, &PreferencesDialog::lockEditingSettingsChanged, this, [this] { applyLockEditingSettings(); });
  connect(&dialog, &PreferencesDialog::disableTypewriterFocusRequested, this, [this] {
    setTypewriterMode(false);
    setFocusMode(false);
    saveAppearanceTypewriterMode(false);
    saveAppearanceFocusMode(false);
  });

  dialog.exec();

  showBlockSourceEnabled_ = QSettings().value(QStringLiteral("editor/showBlockSource"), false).toBool();
  scheduleEditorStateRefresh();

  // Re-apply markdown preferences now that the modal dialog has closed. Doing this AFTER exec()
  // (rather than live during the dialog) is deliberate: a full setDocument re-render while the
  // modal PreferencesDialog is open races its window-blocking and can blank the viewport (see the
  // spell-check note in MainWindowSignalBinder.cpp). setParseOptions is a no-op when the parse
  // options are unchanged; refreshVisibleBlocks picks up layout-only changes (e.g. codeBlockWrap)
  // without a re-parse.
  for (DocumentTab* document : documentTabs()) {
    document->session.setParseOptions(markdownParseOptions());
    document->renderView->refreshVisibleBlocks(document->session.document());
  }
}

void muffin::MainWindow::revealCurrentFile() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }
  revealPathInManager(tab_->session.filePath());
}

bool muffin::MainWindow::saveCurrentDocument() {
  if (tab_->fileController.save(tab_->session, this) == SaveOutcome::Saved) {
    addRecentFile(tab_->session.filePath());
    clearDraftSnapshot(tab_->session.filePath(), tab_->draftKey);
    return true;
  }
  return false;
}

int muffin::MainWindow::draftSnapshotIntervalMs(const DocumentTab* tab) const {
  return qMax(3000, static_cast<int>(tab->session.markdownText().size() / 2048));
}

void muffin::MainWindow::performAutoSave(DocumentTab* tab) {
  if (updateClosePrepared_)
    return;
  // Silent write of a pathed, modified document when files/autoSave is on. Untitled
  // documents (no filePath) are handled by draft-recovery snapshots, not here.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/autoSave"), false).toBool()) {
    return;
  }
  if (tab->session.filePath().isEmpty() || !tab->session.document().isModified()) {
    return;
  }
  if (tab->pendingExternalFileChangePath == tab->session.filePath()) {
    return; // Resolve the deferred reload decision before any background write.
  }
  const SaveOutcome outcome = tab->fileController.save(tab->session, this);
  if (outcome == SaveOutcome::Saved) {
    clearDraftSnapshot(tab->session.filePath(), tab->draftKey);
  } else if (outcome == SaveOutcome::SkippedBusy) {
    tab->autoSaveTimer->start(); // async parse still in flight — retry once it settles
  }
}

void muffin::MainWindow::snapshotDraft(DocumentTab* tab) {
  muffin::diag::ScopedPerfProbe perf("main.snapshotDraft", fileOpsPerf());
  // Persist a recovery snapshot while the document has unsaved content, so a
  // crash or forced exit can be restored on the next launch. Cleared on save.
  if (!tab->session.document().isModified()) {
    return;
  }
  // Only a successful worker completion advances the acknowledged revision.
  // The writer coalesces pending revisions and captures independent buffers.
  const quint64 revision = tab->session.document().revision();
  if (revision != tab->lastDraftSnapshotRevision)
    draftWriter_.request(tab->session, tab->draftKey);
  // Heartbeat: re-arm for as long as the document stays dirty. modifiedChanged
  // only fires on the clean↔dirty transition, so without this re-arm a single
  // snapshot would be taken seconds into an edit session and never refreshed —
  // leaving everything typed afterwards unrecoverable. The revision check above
  // keeps the heartbeat's cost to a revision read when the user pauses. The
  // interval scales with document size so the encode+write doesn't hitch typing.
  tab->draftTimer->start(draftSnapshotIntervalMs(tab));
}

void muffin::MainWindow::clearDraftSnapshot(const QString& path, const QString& key) {
  draftWriter_.markClean(path, key);
}

bool muffin::MainWindow::offerDraftRecovery() {
  // Drop crash-leftover half-pairs; complete drafts with missing sources can
  // still be recovered into untitled documents.
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
  list->setAccessibleName(tr("Unsaved Drafts Found"));
  // Restore one selected draft at a time; the others remain available for later recovery.
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  for (const DraftRecovery::PendingDraft& d : drafts) {
    const QString time =
        QDateTime::fromMSecsSinceEpoch(d.timestamp).toString(QLocale().dateTimeFormat(QLocale::ShortFormat));
    const QString where = d.sourcePath.isEmpty() ? tr("Untitled") : QDir::toNativeSeparators(d.sourcePath);
    auto* item = new QListWidgetItem(tr("%1  —  %2  (%n char(s))", nullptr, int(d.charCount)).arg(where, time), list);
    item->setData(Qt::UserRole, QVariant::fromValue(d));
  }
  list->setCurrentRow(0); // pre-select the newest draft for a one-click restore
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
      restoredAny = restoreDraft(list->currentItem()->data(Qt::UserRole).value<DraftRecovery::PendingDraft>());
    }
  } else if (result == Discard) {
    for (const DraftRecovery::PendingDraft& d : drafts) {
      drafts_.discard(d);
    }
  }
  // Later / window close: leave the drafts in place for the next launch.
  return restoredAny;
}

bool muffin::MainWindow::restoreDraft(const DraftRecovery::PendingDraft& draft) {
  bool loaded = false;
  const QString content = drafts_.loadDraft(draft, &loaded);
  if (!loaded) {
    return false;
  }
  if (!draft.sourcePath.isEmpty() && QFileInfo(draft.sourcePath).isFile() && !isPathOpen(draft.sourcePath)) {
    // Reopen the on-disk file (restores path and undo history), then overlay the
    // recovered content and mark it modified — the recovered text is the newer version.
    if (!openFile(draft.sourcePath)) {
      return false;
    }
    if (!tab_->pendingOpenPath.isEmpty()) {
      // Keep the recovery snapshot intact if the user cancels the file warning.
      tab_->pendingRecoveredDraftText = content;
      tab_->pendingRecoveredDraftKey = draft.key;
      return true;
    }
    tab_->session.setMarkdownText(content, true);
  } else {
    // Untitled draft, or the source file no longer exists: load into a new untitled doc.
    if (!startNewDocument()) {
      return false;
    }
    tab_->session.setMarkdownText(content, true);
  }
  tab_->draftKey = draft.key;
  // The restored draft becomes the active document; its key is reused by the next
  // snapshot, so we don't discard here.
  return true;
}

bool muffin::MainWindow::isDocumentModified() const {
  return tab_->session.document().isModified();
}

// ---- Quick Open -----------------------------------------------------------

QStringList muffin::MainWindow::quickOpenCandidates() const {
  QStringList candidates;
  if (!tab_->session.filePath().isEmpty())
    candidates.append(tab_->session.filePath());
  candidates.append(recentFiles());
  for (const auto* document : documentTabs()) {
    if (!document->session.filePath().isEmpty())
      candidates.append(document->session.filePath());
  }
  return candidates;
}

void muffin::MainWindow::quickOpen() {
  const QStringList candidates = quickOpenCandidates();
  if (candidates.isEmpty() && sidebar_->folderRoot().isEmpty()) {
    statusBar()->showMessage(tr("No files to open"), 4000);
    return;
  }
  QuickOpenDialog dialog(this);
  dialog.setThemeDefinition(themeManager_.currentDefinition());
  dialog.setCandidates(candidates);
  dialog.setWorkspace(sidebar_->folderRoot(), WorkspaceFileIndex::textExtensions());
  if (dialog.exec() == QDialog::Accepted) {
    const QString path = dialog.selectedPath();
    if (!path.isEmpty()) {
      openFile(path);
    }
  }
}

// ---- Import (Pandoc → markdown) -------------------------------------------

void muffin::MainWindow::importFile() {
  const QString dir =
      tab_->session.filePath().isEmpty() ? QString() : QFileInfo(tab_->session.filePath()).absolutePath();
  const QString sourcePath = QFileDialog::getOpenFileName(
      this, tr("Import"), dir,
      tr("Word (*.docx);;OpenDocument (*.odt);;RTF (*.rtf);;EPUB (*.epub);;HTML (*.html *.htm);;"
         "LaTeX (*.tex *.latex);;MediaWiki (*.wiki *.mediawini);;reStructuredText (*.rst);;"
         "Textile (*.textile);;OPML (*.opml);;All files (*)"));
  if (sourcePath.isEmpty()) {
    return;
  }

  if (!PandocRunner::isAvailable()) {
    QMessageBox::warning(this, tr("Import Failed"),
                         tr("Pandoc was not found. Install Pandoc or set its path in Preferences → Export."));
    return;
  }

  const QString importingMsg = tr("Importing %1…").arg(QFileInfo(sourcePath).fileName());
  statusBar()->showMessage(importingMsg, 0);
  // Pandoc infers the input format from the file extension; GFM output matches
  // Muffin's cmark-gfm parser best.
  const PandocResult result =
      PandocRunner::run(this, {QStringLiteral("-t"), QStringLiteral("gfm"), sourcePath},
                        {.workDir = QFileInfo(sourcePath).absolutePath(), .progressLabel = importingMsg});
  statusBar()->clearMessage();
  if (result.canceled) {
    return;
  }
  if (!result.ran || result.exitCode != 0) {
    QMessageBox::critical(this, tr("Import Failed"),
                          tr("Pandoc could not convert the file.") + QStringLiteral("\n\n") +
                              QString::fromUtf8(result.err));
    return;
  }

  if (!startNewDocument()) {
    return;
  }
  tab_->session.setMarkdownText(QString::fromUtf8(result.out), true); // imported content is unsaved
  statusBar()->showMessage(tr("Imported %1").arg(QFileInfo(sourcePath).fileName()), 4000);
}

// ---- Export ---------------------------------------------------------------

namespace {

// Per-format file extension. PDF/HTML are native; the rest are Pandoc outputs.
QString exportExtension(muffin::ExportFormat format) {
  switch (format) {
  case muffin::ExportFormat::Pdf:
    return QStringLiteral(".pdf");
  case muffin::ExportFormat::Html:
  case muffin::ExportFormat::HtmlPlain:
    return QStringLiteral(".html");
  case muffin::ExportFormat::Image:
    return QStringLiteral(".png");
  case muffin::ExportFormat::Docx:
    return QStringLiteral(".docx");
  case muffin::ExportFormat::Odt:
    return QStringLiteral(".odt");
  case muffin::ExportFormat::Rtf:
    return QStringLiteral(".rtf");
  case muffin::ExportFormat::Epub:
    return QStringLiteral(".epub");
  case muffin::ExportFormat::Latex:
    return QStringLiteral(".tex");
  case muffin::ExportFormat::MediaWiki:
    return QStringLiteral(".wiki");
  case muffin::ExportFormat::Rst:
    return QStringLiteral(".rst");
  case muffin::ExportFormat::Textile:
    return QStringLiteral(".textile");
  case muffin::ExportFormat::Opml:
    return QStringLiteral(".opml");
  }
  return QStringLiteral(".txt");
}

// Resolves the suggested destination directory from export/defaultFolder:
// 0 Auto / 1 Same folder → the source file's folder (Documents if untitled);
// 2 Custom → the last-used custom folder (export/lastCustomFolder).
QString exportSuggestedDir(const QString& sourcePath) {
  QSettings settings;
  const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const int mode = settings.value(QStringLiteral("export/defaultFolder"), 0).toInt();
  const QString sourceDir = sourcePath.isEmpty() ? QString() : QFileInfo(sourcePath).absolutePath();
  switch (mode) {
  case 2:
    return settings
        .value(QStringLiteral("export/customFolder"),
               settings.value(QStringLiteral("export/lastCustomFolder"), documents))
        .toString();
  case 1:
    return sourceDir.isEmpty() ? documents : sourceDir;
  case 0:
  default:
    return sourceDir.isEmpty() ? documents : sourceDir;
  }
}

// Writes bytes verbatim (no trailing-newline / line-ending transforms, unlike
// FileController::writeTextFile) via an atomic QSaveFile.
bool writeExportBytes(const QString& path, const QByteArray& bytes, QString* error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

} // namespace

void muffin::MainWindow::exportAs(ExportFormat format) {
  const QString markdown = tab_->session.markdownText().toString();
  if (markdown.trimmed().isEmpty()) {
    statusBar()->showMessage(tr("Nothing to export"), 4000);
    return;
  }

  const QString sourcePath = tab_->session.filePath();
  const QString baseName = sourcePath.isEmpty() ? tr("Untitled") : QFileInfo(sourcePath).completeBaseName();
  const QString ext = exportExtension(format);
  const QString suggestedPath = QDir(exportSuggestedDir(sourcePath)).absoluteFilePath(baseName + ext);

  const QString target =
      QFileDialog::getSaveFileName(this, tr("Export As"), suggestedPath, tr("%1 files (*%2)").arg(baseName, ext));
  if (target.isEmpty()) {
    return;
  }

  QSettings settings;
  // Convert into a sibling temporary directory. A canceled/failed conversion
  // must preserve an existing destination even after overwrite was accepted.
  QTemporaryDir staging(QDir(QFileInfo(target).absolutePath()).filePath(QStringLiteral(".muffin-export-XXXXXX")));
  if (!staging.isValid()) {
    QMessageBox::critical(this, tr("Export Failed"), tr("Could not create a temporary export file."));
    return;
  }
  const auto stagedTarget = staging.filePath(QStringLiteral("document") + ext);
  const auto prefix = exportSettingsPrefix(format);
  const auto nativeTheme = [&](const QString& name, int fontSize) {
    return name == QStringLiteral("github")
               ? RenderTheme::fromDefinition(themeManager_.definition(QStringLiteral("github")), 100, fontSize)
               : themeManager_.currentTheme(100, fontSize);
  };
  auto* cache = tab_->renderView ? tab_->renderView->mermaidRenderCache() : nullptr;

  bool ok = false;
  QString error;
  switch (format) {
  case ExportFormat::Pdf: {
    const auto options = pdfExportOptions();
    ok = exportNativePdf(stagedTarget, tab_->session.document(), nativeTheme(options.theme, options.fontSize),
                         sourcePath, options, cache);
    if (!ok) {
      error = tr("PDF export failed: the printer device could not be opened.");
    }
    break;
  }
  case ExportFormat::Image: {
    const auto options = imageExportOptions();
    ok = exportNativeImage(stagedTarget, tab_->session.document(), nativeTheme(options.theme, options.fontSize),
                           sourcePath, options, cache);
    if (!ok)
      error = tr(
          "Image export failed. The image must fit within 64 million pixels and 32,767 pixels in height. Try a smaller width or scale, or export as PDF.");
    break;
  }
  case ExportFormat::Html:
  case ExportFormat::HtmlPlain: {
    const bool styled = (format == ExportFormat::Html);
    // Export the selected theme; the default light option must stay light
    // even when the browser or operating system prefers dark colours.
    const QString themeCss = !styled ? QString()
                             : settings.value(prefix + "theme", "current").toString() == "current"
                                 ? themeManager_.currentThemeCss()
                                 : ThemeManager().currentThemeCss();
    const QString html = muffin::renderDocumentHtml(markdown, baseName, styled, themeCss,
                                                    {.outline = settings.value(prefix + "outline", false).toBool(),
                                                     .head = settings.value(prefix + "head").toString(),
                                                     .body = settings.value(prefix + "body").toString()});
    ok = writeExportBytes(stagedTarget, html.toUtf8(), &error);
    break;
  }
  default: {
    if (!PandocRunner::isAvailable()) {
      error = tr("Pandoc was not found. Install Pandoc or set its path in Preferences → Export.");
      break;
    }
    const QString exportingMsg = tr("Exporting %1…").arg(baseName + ext);
    statusBar()->showMessage(exportingMsg, 0);
    // markdown/breakOnSingleNewline (default on): render soft breaks as line breaks, matching the
    // editor view. pandoc's hard_line_breaks extension does for gfm input what the view does.
    const QString inputFormat = QSettings().value(QStringLiteral("markdown/breakOnSingleNewline"), true).toBool()
                                    ? QStringLiteral("gfm+hard_line_breaks")
                                    : QStringLiteral("gfm");
    const auto options = pandocExportOptions(format);
    for (const auto& path : {options.referenceDocument, options.css}) {
      if (!path.isEmpty() && !QFileInfo(path).isFile()) {
        error = tr("Export resource was not found: %1").arg(path);
        break;
      }
    }
    if (!error.isEmpty()) {
      statusBar()->clearMessage();
      break;
    }
    QStringList arguments;
    if (!pandocExportArguments(format, inputFormat, stagedTarget, options, &arguments)) {
      error =
          tr("Additional arguments cannot override the input format, output format, output path, or defaults file.");
      statusBar()->clearMessage();
      break;
    }
    const PandocResult result =
        PandocRunner::run(this, arguments,
                          {.stdinData = markdown.toUtf8(),
                           .workDir = sourcePath.isEmpty() ? QDir::homePath() : QFileInfo(sourcePath).absolutePath(),
                           .progressLabel = exportingMsg});
    statusBar()->clearMessage();
    if (result.canceled) {
      return;
    }
    if (result.ran && result.exitCode == 0) {
      ok = true;
    } else {
      error = tr("Pandoc failed:") + QStringLiteral("\n\n") + QString::fromUtf8(result.err);
    }
    break;
  }
  }

  if (ok) {
    QFile staged(stagedTarget);
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    ok = staged.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly);
    while (ok && !staged.atEnd()) {
      const auto bytes = staged.read(64 * 1024);
      ok = !bytes.isEmpty() && output.write(bytes) == bytes.size();
    }
    if (ok)
      ok = output.commit();
    if (!ok)
      error = output.errorString().isEmpty() ? staged.errorString() : output.errorString();
  }
  if (!ok) {
    QMessageBox::critical(this, tr("Export Failed"), error);
    return;
  }

  statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(target)), 6000);
  if (settings.value(prefix + "openDirectory", settings.value(QStringLiteral("export/openAfterExport"), false))
          .toBool()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(target).absolutePath()));
  }
  if (settings.value(prefix + "openFile", false).toBool())
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
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
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Unicode"),
       {
           {"UTF-8", QT_TRANSLATE_NOOP("muffin::MainWindow", "UTF-8")},
           {"UTF-16LE", QT_TRANSLATE_NOOP("muffin::MainWindow", "UTF-16 LE")},
           {"UTF-16BE", QT_TRANSLATE_NOOP("muffin::MainWindow", "UTF-16 BE")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Western"),
       {
           {"windows-1252", QT_TRANSLATE_NOOP("muffin::MainWindow", "Western (Windows-1252)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic"),
       {
           {"windows-1251", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (Windows-1251)")},
           {"ISO-8859-5", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (ISO-8859-5)")},
           {"IBM866", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (IBM866)")},
           {"IBM855", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (IBM855)")},
           {"KOI8-R", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (KOI8-R)")},
           {"x-mac-cyrillic", QT_TRANSLATE_NOOP("muffin::MainWindow", "Cyrillic (Mac)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Central European"),
       {
           {"windows-1250", QT_TRANSLATE_NOOP("muffin::MainWindow", "Central European (Windows-1250)")},
           {"ISO-8859-2", QT_TRANSLATE_NOOP("muffin::MainWindow", "Central European (ISO-8859-2)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Greek"),
       {
           {"windows-1253", QT_TRANSLATE_NOOP("muffin::MainWindow", "Greek (Windows-1253)")},
           {"ISO-8859-7", QT_TRANSLATE_NOOP("muffin::MainWindow", "Greek (ISO-8859-7)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Hebrew"),
       {
           {"windows-1255", QT_TRANSLATE_NOOP("muffin::MainWindow", "Hebrew (Windows-1255)")},
           {"ISO-8859-8", QT_TRANSLATE_NOOP("muffin::MainWindow", "Hebrew (ISO-8859-8)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Chinese Simplified"),
       {
           {"GB2312", QT_TRANSLATE_NOOP("muffin::MainWindow", "Chinese Simplified (GB2312)")},
           {"GB18030", QT_TRANSLATE_NOOP("muffin::MainWindow", "Chinese Simplified (GB18030)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Chinese Traditional"),
       {
           {"Big5", QT_TRANSLATE_NOOP("muffin::MainWindow", "Chinese Traditional (Big5)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Japanese"),
       {
           {"Shift_JIS", QT_TRANSLATE_NOOP("muffin::MainWindow", "Japanese (Shift_JIS)")},
           {"EUC-JP", QT_TRANSLATE_NOOP("muffin::MainWindow", "Japanese (EUC-JP)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Korean"),
       {
           {"EUC-KR", QT_TRANSLATE_NOOP("muffin::MainWindow", "Korean (EUC-KR)")},
       }},
      {QT_TRANSLATE_NOOP("muffin::MainWindow", "Thai"),
       {
           {"TIS-620", QT_TRANSLATE_NOOP("muffin::MainWindow", "Thai (TIS-620)")},
       }},
  };

  for (const auto& group : groups) {
    for (const auto& entry : group.entries) {
      QAction* action = reopenEncodingMenu_->addAction(tr(entry.label));
      const QString encodingName = QString::fromLatin1(entry.name);
      connect(action, &QAction::triggered, this, [this, encodingName] { reopenWithEncoding(encodingName); });
    }
    reopenEncodingMenu_->addSeparator();
  }
}

void muffin::MainWindow::reopenWithEncoding(const QString& encodingName) {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }

  if (tab_->fileController.reopenWithEncoding(tab_->session, this, encodingName)) {
    tab_->editorController.clearHistoryAndSelection();
  }
}

void muffin::MainWindow::moveToFile() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }

  const QString oldPath = tab_->session.filePath();
  const QString newPath =
      QFileDialog::getSaveFileName(this, tr("Move To"), oldPath, tr("Markdown Files (*.md *.markdown);;All Files (*)"));
  if (newPath.isEmpty() || newPath == oldPath)
    return;
  renameOrMoveWithLinks(oldPath, newPath);
}

void muffin::MainWindow::saveAllOpenFiles() {
  int savedCount = 0;
  int failedCount = 0;

  for (QWidget* widget : QApplication::topLevelWidgets()) {
    auto* window = qobject_cast<MainWindow*>(widget);
    if (!window) {
      continue;
    }
    DocumentTab* original = window->tab_;
    const QScopedValueRollback<bool> switchGuard(window->suppressSaveOnTabSwitch_, true);
    for (DocumentTab* document : window->documentTabs()) {
      if (!document->session.document().isModified()) {
        continue;
      }
      window->tabs_->setCurrentWidget(document);
      if (window->saveCurrentDocument()) {
        ++savedCount;
      } else {
        ++failedCount;
      }
    }
    window->tabs_->setCurrentWidget(original);
  }

  if (failedCount > 0) {
    QMessageBox::warning(this, tr("Save All"),
                         tr("Saved %1 file(s). %2 file(s) could not be saved.").arg(savedCount).arg(failedCount));
  }
}

void muffin::MainWindow::showInSidebar() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }
  const QString path = tab_->session.filePath();
  const QString root = sidebar_->folderRoot();
  const QString relative = QDir(root).relativeFilePath(path);
  if (root.isEmpty() || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(relative)) {
    openFolderAtPath(QFileInfo(path).absolutePath());
  }
  setSidebarPanel(SidebarWidget::Panel::Files);
  sidebar_->setCurrentDocument(tab_->session.displayName(), tab_->session.filePath(),
                               tab_->session.document().isModified());
  sidebar_->setCurrentPath(path);
}

void muffin::MainWindow::deleteFile() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }

  const QString filePath = tab_->session.filePath();
  const QString fileName = QFileInfo(filePath).fileName();

  const QMessageBox::StandardButton confirm =
      QMessageBox::warning(this, tr("Delete File"),
                           tr("Are you sure you want to move \"%1\" to the trash?\n\n"
                              "This action cannot be undone.")
                               .arg(fileName),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (confirm != QMessageBox::Yes) {
    return;
  }

  if (tab_->session.document().isModified()) {
    const QMessageBox::StandardButton unsaved =
        QMessageBox::warning(this, tr("Muffin"), tr("The current document has unsaved changes."),
                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (unsaved == QMessageBox::Cancel) {
      return;
    }
    if (unsaved == QMessageBox::Save && !saveCurrentDocument()) {
      return;
    }
  }

  bool trashed = QFile::moveToTrash(filePath);

  if (!trashed) {
    const QMessageBox::StandardButton confirm2 =
        QMessageBox::critical(this, tr("Delete File"),
                              tr("Could not move to trash. Permanently delete \"%1\"?\n\n"
                                 "This action cannot be undone.")
                                  .arg(fileName),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (confirm2 != QMessageBox::Yes) {
      return;
    }

    if (!QFile::remove(filePath)) {
      QMessageBox::critical(this, tr("Delete Failed"), tr("Could not delete file:\n%1").arg(filePath));
      return;
    }
  }

  clearDraftSnapshot(filePath, tab_->draftKey);
  tab_->session.document().setModified(false);
  closeTab(tabs_->indexOf(tab_));

  QStringList recent = recentFiles();
  recent.removeAll(QFileInfo(filePath).absoluteFilePath());
  setRecentFiles(recent);
  rebuildRecentFilesMenu();
}

// ---- Sidebar file-tree context menu operations ------------------------------
//
// These act on arbitrary paths (the file/dir under the right-click), unlike the
// commands above which operate on tab_->session.filePath(). The filesystem work lives
// in FilePathOps; here we only prompt + integrate with the editor/sidebar.

void muffin::MainWindow::openFolderAtPath(QString path) {
  if (!QFileInfo(path).isDir())
    return;
  path = QFileInfo(path).absoluteFilePath();
  const QString openedPath = path; // capture before the move, for recent-folders recording
  sidebarFolderRoot_ = std::move(path);
  if (sidebar_) {
    sidebar_->setFolderRoot(sidebarFolderRoot_);
    setSidebarPanel(SidebarWidget::Panel::Files);
  }
  addRecentFolder(openedPath);
  saveSessionState();
}

void muffin::MainWindow::openFileInNewWindow(QString path) {
  auto* window = new MainWindow();
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->openFile(path); // before show() so the document parses during construction
  window->show();
}

void muffin::MainWindow::openFolderInNewWindow(QString path) {
  auto* window = new MainWindow();
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->openFolderAtPath(path);
  window->show();
}

void muffin::MainWindow::newFileInDirectory(QString dir) {
  if (dir.isEmpty()) {
    return;
  }
  // Create a uniquely-named temp entry on disk so the file tree shows a real row to rename
  // inline (OS-style). The actual name is finalized on commit; cancelling / clearing the
  // editor deletes the temp. The untitled suggestion respects files/defaultExtension.
  QString tempPath = QDir(dir).filePath(defaultUntitledSuggestion());
  if (QFileInfo::exists(tempPath)) {
    tempPath = FilePathOps::uniqueDuplicatePath(tempPath);
  }
  QString error;
  if (!FilePathOps::createFile(tempPath, &error)) {
    QMessageBox::critical(this, tr("New File"),
                          tr("Could not create file:\n%1\n\n%2").arg(QDir::toNativeSeparators(tempPath), error));
    return;
  }
  if (sidebar_) {
    sidebar_->beginInlineCreate(tempPath);
  }
}

void muffin::MainWindow::newFolderInDirectory(QString dir) {
  if (dir.isEmpty()) {
    return;
  }
  QString tempPath = QDir(dir).filePath(tr("New Folder"));
  if (QFileInfo::exists(tempPath)) {
    tempPath = FilePathOps::uniqueDuplicatePath(tempPath);
  }
  QString error;
  if (!FilePathOps::createFolder(tempPath, &error)) {
    QMessageBox::critical(this, tr("New Folder"),
                          tr("Could not create folder:\n%1\n\n%2").arg(QDir::toNativeSeparators(tempPath), error));
    return;
  }
  if (sidebar_) {
    sidebar_->beginInlineCreate(tempPath);
  }
}

void muffin::MainWindow::renamePath(QString path) {
  if (path.isEmpty()) {
    return;
  }
  // Opens the inline rename editor on the row; the actual rename happens in onInlineCommit.
  if (sidebar_) {
    sidebar_->beginInlineRename(path);
  }
}

// Validate an inline name WITHOUT touching the filesystem — the delegate uses this to decide
// whether to keep the editor open (Duplicate / Empty) or let the commit proceed.
void muffin::MainWindow::onInlineValidate(muffin::InlineEditContext ctx, const QString& name,
                                          muffin::InlineValidation* out) {
  if (!out) {
    return;
  }
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    out->kind = muffin::InlineValidation::Empty;
    out->errorText = tr("Name cannot be empty.");
    return;
  }
  // Only append ".md" for a brand-new file (matches the old New File prompt); a rename never
  // forces an extension (the user may be changing it on purpose).
  const QString normalized =
      (ctx.pendingCreate && !ctx.isFolder) ? FilePathOps::normalizeMarkdownFileName(trimmed) : trimmed;
  if (normalized == QFileInfo(ctx.oldPath).fileName()) {
    out->kind = muffin::InlineValidation::NoChange;
    return;
  }
  const QString dir = QFileInfo(ctx.oldPath).absolutePath();
  if (FilePathOps::targetNameCollides(ctx.oldPath, dir, normalized)) {
    out->kind = muffin::InlineValidation::Duplicate;
    out->errorText = tr("An item named \"%1\" already exists.").arg(normalized);
    return;
  }
  out->kind = muffin::InlineValidation::Valid;
}

// Commit the inline edit: perform the rename, then run the preserved post-steps (select the
// new path, open a newly-created file, sync the session when the renamed entry was the open
// document). The name has already been validated.
void muffin::MainWindow::onInlineCommit(muffin::InlineEditContext ctx, const QString& name) {
  const QString trimmed = name.trimmed();
  const QString normalized =
      (ctx.pendingCreate && !ctx.isFolder) ? FilePathOps::normalizeMarkdownFileName(trimmed) : trimmed;
  const QString oldName = QFileInfo(ctx.oldPath).fileName();
  const QString newPath = QDir(QFileInfo(ctx.oldPath).absolutePath()).filePath(normalized);
  if (!ctx.pendingCreate) {
    renameOrMoveWithLinks(ctx.oldPath, newPath);
    return;
  }
  if (normalized != oldName) {
    QString error;
    if (!FilePathOps::renamePath(ctx.oldPath, newPath, &error)) {
      statusBar()->showMessage(tr("Could not rename: %1").arg(error), 5000);
      return; // editor already closed on commit; best-effort surface via the status bar
    }
  }
  if (sidebar_) {
    sidebar_->setCurrentPath(newPath);
  }
  if (!ctx.isFolder) {
    openFile(newPath);
  }
}

// Cancel a pending new-item create: the user pressed Escape or submitted an empty name, so
// delete the temp entry that was provisionally created on disk. Renames have nothing to undo.
void muffin::MainWindow::onInlineCancel(muffin::InlineEditContext ctx) {
  if (!ctx.pendingCreate || ctx.oldPath.isEmpty()) {
    return;
  }
  QString error;
  if (!FilePathOps::moveToTrash(ctx.oldPath, &error)) {
    FilePathOps::removePermanently(ctx.oldPath, nullptr); // fallback so no temp is stranded
  }
}

void muffin::MainWindow::duplicateFile(QString path) {
  if (path.isEmpty() || !QFileInfo(path).isFile()) {
    return;
  }
  const QString dest = FilePathOps::uniqueDuplicatePath(path);
  QString error;
  if (!FilePathOps::copyFile(path, dest, &error)) {
    QMessageBox::critical(this, tr("Duplicate"),
                          tr("Could not create a copy:\n%1\n\n%2").arg(QDir::toNativeSeparators(path), error));
    return;
  }
  if (sidebar_) {
    sidebar_->setCurrentPath(dest);
  }
}

void muffin::MainWindow::deletePath(QString path) {
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return;
  }
  const QFileInfo info(path);
  const QString windowTitle = info.isDir() ? tr("Delete Folder") : tr("Delete File");
  const QString trashPrompt =
      info.isDir()
          ? tr("Are you sure you want to move \"%1\" and all its contents to the trash?\n\nThis action cannot be undone.")
                .arg(info.fileName())
          : tr("Are you sure you want to move \"%1\" to the trash?\n\nThis action cannot be undone.")
                .arg(info.fileName());
  if (QMessageBox::warning(this, windowTitle, trashPrompt, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
      QMessageBox::Yes) {
    return;
  }

  QList<DocumentTab*> affected;
  const QScopedValueRollback<bool> switchGuard(suppressSaveOnTabSwitch_, true);
  DocumentTab* original = tab_;
  for (DocumentTab* document : documentTabs()) {
    if (FilePathOps::isSameOrDescendant(document->session.filePath(), path)) {
      affected.append(document);
      tabs_->setCurrentWidget(document);
      if (!maybeSaveChanges()) {
        tabs_->setCurrentWidget(original);
        return;
      }
    }
  }
  tabs_->setCurrentWidget(original);

  QString error;
  bool removed = FilePathOps::moveToTrash(path, &error);
  if (!removed) {
    // Trash unavailable (e.g. some Linux backends): offer a permanent delete.
    if (QMessageBox::critical(this, windowTitle,
                              tr("Could not move to trash. Permanently delete \"%1\"?\n\nThis action cannot be undone.")
                                  .arg(info.fileName()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
      removed = FilePathOps::removePermanently(path, &error);
    }
  }
  if (!removed) {
    if (!error.isEmpty()) {
      QMessageBox::critical(this, tr("Delete Failed"), tr("Could not delete:\n%1").arg(QDir::toNativeSeparators(path)));
    }
    return;
  }

  for (DocumentTab* document : affected) {
    document->session.document().setModified(false);
    clearDraftSnapshot(document->session.filePath(), document->draftKey);
    closeTab(tabs_->indexOf(document));
  }
  QStringList recent = recentFiles();
  recent.erase(std::remove_if(recent.begin(), recent.end(),
                              [&path](const QString& item) { return FilePathOps::isSameOrDescendant(item, path); }),
               recent.end());
  setRecentFiles(recent);
  rebuildRecentFilesMenu();
}

void muffin::MainWindow::showPathProperties(QString path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return;
  }
  const QString type = info.isDir() ? tr("Folder") : tr("File", "properties: entry type");
  const QString location = QDir::toNativeSeparators(info.absolutePath());
  const QString sizeLine = info.isFile() ? tr("Size: %1 bytes\n").arg(QString::number(info.size())) : QString();
  const QString message = tr("Name: %1\nType: %2\nLocation: %3\n%4Modified: %5")
                              .arg(info.fileName(), type, location, sizeLine,
                                   QLocale().toString(info.lastModified(), QLocale::ShortFormat));
  QMessageBox::information(this, tr("Properties"), message);
}

void muffin::MainWindow::copyPathToClipboard(QString path) const {
  if (path.isEmpty()) {
    return;
  }
  QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void muffin::MainWindow::revealPathInManager(QString path) {
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!FilePathOps::revealPathInManager(path, &error)) {
    QMessageBox::warning(this, tr("Reveal in File Manager"),
                         tr("Could not reveal:\n%1").arg(QDir::toNativeSeparators(path)));
  }
}

QString muffin::MainWindow::defaultUntitledSuggestion() const {
  // Reuse the files/defaultExtension setting so the New File prompt matches the
  // Save As untitled name (.md/.markdown/.txt).
  switch (QSettings().value(QStringLiteral("files/defaultExtension"), 0).toInt()) {
  case 1:
    return QStringLiteral("Untitled.markdown");
  case 2:
    return QStringLiteral("Untitled.txt");
  default:
    return QStringLiteral("Untitled.md");
  }
}

bool muffin::MainWindow::maybeSaveChanges() {
  if (!tab_->session.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice =
      QMessageBox::warning(this, tr("Muffin"), tr("The current document has unsaved changes."),
                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return tab_->fileController.save(tab_->session, this) == SaveOutcome::Saved; // save() emits documentBecameClean.
  }
  // Discard: the user explicitly abandoned the unsaved work — drop its recovery
  // draft so the next launch doesn't offer to restore what was just thrown away.
  clearDraftSnapshot(tab_->session.filePath(), tab_->draftKey);
  return true;
}
