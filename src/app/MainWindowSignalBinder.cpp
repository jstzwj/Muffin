#include "app/LanguageManager.h"
#include "app/MainWindow.h"
#include "app/SearchPanelWidget.h"
#include "app/FileOpenConfirmation.h"
#include "app/UpdateChecker.h"
#include "app/WindowsUpdater.h"
#include "app/SidebarWidget.h"
#include "app/StatusBarWidget.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "document/TextStats.h"
#include "spellcheck/SpellChecker.h"

#include <QAction>
#include <QDesktopServices>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QMessageBox>
#include <QStatusBar>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMenu>
#include <QToolButton>
#include <QPushButton>
#include <QTabWidget>

namespace {

Q_LOGGING_CATEGORY(mainWindowPerf, "muffin.perf", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(mainWindowPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(mainWindowPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

} // namespace

void muffin::MainWindow::connectEditorSignals(DocumentTab* tab) {
  auto& window = *this; // preserves the window.X call sites after the friend→member split
  QObject::connect(tab->editor, &SourceEditorWidget::cursorPositionChanged, tab->windowConnections,
                   [&window, tab](int line, int column) {
                     tab->cursorLine = line;
                     tab->cursorColumn = column;
                     if (window.tab_ != tab) {
                       return;
                     }
                     window.updateCursorStatus(line, column);
                     if (!window.restoringViewState_ && window.typewriterMode_ && tab->backend->isSourceMode()) {
                       tab->backend->centerCursor();
                     }
                   });
}

void muffin::MainWindow::connectRenderSignals(DocumentTab* tab) {
  auto& window = *this;
  QObject::connect(&tab->editorController, &EditorController::cursorChanged, tab->windowConnections,
                   [&window, tab](const HitTestResult& hit) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     window.updateRenderCursorStatus(hit);
                     if (!window.restoringViewState_ && window.typewriterMode_ && !tab->backend->isSourceMode()) {
                       tab->renderView->scrollToCursorCenteredAnimated();
                     }
                   });
  QObject::connect(tab->renderView, &EditorView::codeLanguageCommitted, tab->windowConnections,
                   [&window, tab](NodeId codeId, const QString& language) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     if (tab->backend->isSourceMode()) {
                       return;
                     }
                     tab->renderCommands.setCodeLanguageFor(codeId, language);
                   });
  QObject::connect(tab->renderView, &EditorView::tableResizeRequested, tab->windowConnections,
                   [&window, tab](int rows, int columns) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     if (!tab->backend->isSourceMode()) {
                       tab->renderCommands.resizeCurrentTable(rows, columns);
                     }
                   });
  QObject::connect(tab->renderView, &EditorView::tableColumnAlignmentRequested, tab->windowConnections,
                   [&window, tab](TableAlignment alignment) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     if (!tab->backend->isSourceMode()) {
                       tab->renderCommands.setCurrentColumnAlignment(alignment);
                     }
                   });
  QObject::connect(tab->renderView, &EditorView::tableDeleteRequested, tab->windowConnections, [&window, tab] {
    if (window.tab_ != tab) {
      return;
    }
    if (!tab->backend->isSourceMode()) {
      tab->renderCommands.deleteCurrentTable();
    }
  });
  QObject::connect(tab->renderView, &EditorView::tableMoreActionsRequested, tab->windowConnections,
                   [&window, tab](QPoint globalPos) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     if (tab->backend->isSourceMode()) {
                       return;
                     }
                     window.updateTableActions();
                     window.updateParagraphActions();
                     QMenu menu(&window);
                     const QStringList ids = {
                         QStringLiteral("table.insert_table"),        QStringLiteral("table.insert_row_before"),
                         QStringLiteral("table.insert_row_after"),    QStringLiteral("table.insert_column_before"),
                         QStringLiteral("table.insert_column_after"), QStringLiteral("table.move_row_up"),
                         QStringLiteral("table.move_row_down"),       QStringLiteral("table.move_column_left"),
                         QStringLiteral("table.move_column_right"),   QStringLiteral("table.delete_row"),
                         QStringLiteral("table.delete_column"),       QStringLiteral("table.copy_table"),
                         QStringLiteral("table.format_source"),       QStringLiteral("table.align_none"),
                         QStringLiteral("table.delete_table"),
                     };
                     for (const QString& id : ids) {
                       if (id == QStringLiteral("table.insert_row_before") ||
                           id == QStringLiteral("table.insert_column_before") ||
                           id == QStringLiteral("table.move_row_up") || id == QStringLiteral("table.delete_row") ||
                           id == QStringLiteral("table.copy_table") || id == QStringLiteral("table.align_none") ||
                           id == QStringLiteral("table.delete_table")) {
                         menu.addSeparator();
                       }
                       if (QAction* action = window.commands_.action(id)) {
                         menu.addAction(action);
                       }
                     }
                     menu.exec(globalPos);
                   });
  QObject::connect(tab->renderView, &EditorView::contextMenuRequested, tab->windowConnections,
                   [&window, tab](HitTestResult hit, QPoint globalPos) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     if (tab->backend->isSourceMode()) {
                       return; // the virtual source view owns its source-offset context menu
                     }
                     window.buildEditorContextMenu(hit, globalPos);
                   });

  connect(tab->renderView, &EditorView::linkActivated, tab->windowConnections, [&window, tab](const QString& href) {
    if (window.tab_ == tab)
      window.navigateToLink(href);
  });
  connect(tab->renderView, &EditorView::historyNavigationRequested, tab->windowConnections,
          [&window](bool forward) { window.navigateHistory(forward); });

  // External drag-and-drop of folders / documents is handled by the main window
  // according to the files/drop* preferences (images are still inserted inline).
  QObject::connect(tab->renderView, &EditorView::folderDropped, tab->windowConnections,
                   [&window, tab](const QString& path) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     // files/dropFolder: 0 = open in Muffin (sidebar root), 1 = open in file manager.
                     const int action = QSettings().value(QStringLiteral("files/dropFolder"), 0).toInt();
                     if (action == 1) {
                       QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                     } else if (window.sidebar_) {
                       // Route through openFolderAtPath so the drop also updates the persistent
                       // sidebar root and records the folder in Open Recent.
                       window.openFolderAtPath(path);
                     }
                   });
  QObject::connect(tab->renderView, &EditorView::markdownFileDropped, tab->windowConnections,
                   [&window, tab](const QString& path) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     // Markdown is Muffin's native format, so a dropped .md/.txt always opens.
                     // (files/dropMarkdown is reserved for a future "import into current doc" action.)
                     window.openFile(path);
                   });
  QObject::connect(tab->renderView, &EditorView::importableFileDropped, tab->windowConnections,
                   [&window, tab](const QString& path) {
                     if (window.tab_ != tab) {
                       return;
                     }
                     // files/dropImportable: 1 = open as text; 0 (default) = import. Import has no
                     // backend yet, so it no-ops — which also means a binary/unknown drop (PDF,
                     // DOCX, …) is ignored rather than force-opened as text garbage.
                     const int action = QSettings().value(QStringLiteral("files/dropImportable"), 0).toInt();
                     if (action == 1) {
                       window.openFile(path);
                     }
                   });
}

void muffin::MainWindow::connectSessionSignals(DocumentTab* tab) {
  connect(tab->openConfirmation, &FileOpenConfirmation::confirmed, tab->windowConnections,
          [this, tab] { confirmPendingFileOpen(tab); });
  connect(tab->openConfirmation, &FileOpenConfirmation::canceled, tab->windowConnections,
          [this, tab] { cancelPendingFileOpen(tab); });
  tab->fileController.setPathAvailableCheck([this, tab](const QString& path) {
    if (!isPathOpen(path, &tab->session)) {
      return true;
    }
    QMessageBox::warning(this, tr("File Already Open"),
                         tr("This file is already open in another tab. Switch to that tab to edit or save it."));
    return false;
  });
  connect(&tab->session, &DocumentSession::documentTextChanged, tab->windowConnections,
          [tab](const QString&) { tab->editor->syncFromSession(); });
  connect(&tab->session, &DocumentSession::documentLocallyEdited, tab->windowConnections,
          [tab](qsizetype, qsizetype, const QString&) {
            tab->editor->notifyDocumentChanged();
            if (tab->backend->isSourceMode()) {
              tab->renderViewDirty = true;
            }
          });
  connect(&tab->session, &DocumentSession::filePathChanged, tab->windowConnections, [this, tab](const QString& path) {
    tab->editor->setDocumentPath(path);
    updateTabLabel(tab);
    if (tab_ == tab) {
      updateTitle();
      updateFileActions();
      refreshSidebarDocumentInfo();
    }
    scheduleSessionSave();
  });
  tab->editor->setDocumentPath(tab->session.filePath());
  connect(
      &tab->session, &DocumentSession::filePathChanged, tab->windowConnections,
      [this, tab] {
        if (tab_ != tab || tab->session.isOpenInProgress() || tab->backend->isSourceMode()) {
          tab->renderViewDirty = true;
          return;
        }
        tab->renderView->setDocument(tab->session.document(), tab->session.filePath());
        tab->renderViewDirty = false;
      },
      Qt::QueuedConnection);
  connect(&tab->session, &DocumentSession::modifiedChanged, tab->windowConnections, [this, tab](bool modified) {
    updateTabLabel(tab);
    if (tab_ == tab) {
      updateTitle();
      scheduleEditorStateRefresh();
      refreshSidebarDocumentInfo();
    }
    if (modified && QSettings().value(QStringLiteral("files/autoSave"), false).toBool() &&
        !tab->session.filePath().isEmpty()) {
      tab->autoSaveTimer->start();
    } else {
      tab->autoSaveTimer->stop();
    }
    if (modified) {
      tab->draftTimer->start(draftSnapshotIntervalMs(tab));
    } else {
      tab->draftTimer->stop();
      tab->lastDraftSnapshotRevision = 0;
      clearDraftSnapshot(tab->session.filePath(), tab->draftKey);
    }
  });
  connect(tab->autoSaveTimer, &QTimer::timeout, tab->windowConnections, [this, tab] { performAutoSave(tab); });
  connect(tab->draftTimer, &QTimer::timeout, tab->windowConnections, [this, tab] { snapshotDraft(tab); });
  connect(&tab->session, &DocumentSession::externalFileChanged, tab->windowConnections,
          [this, tab] { queueExternalFileChange(tab); });
  connect(&tab->fileController, &FileController::documentBecameClean, tab->windowConnections,
          [this, tab](const QString& path) { clearDraftSnapshot(path, tab->draftKey); });
  connect(&tab->session, &DocumentSession::parseBusy, tab->windowConnections, [tab](bool busy) {
    tab->renderView->setLoading(busy && tab->pendingOpenPath.isEmpty());
    tab->editor->setReadOnly(busy);
  });
  connect(&tab->session, &DocumentSession::parsed, tab->windowConnections, [this, tab] {
    QTimer::singleShot(0, tab->windowConnections, [this, tab] { applySearchNavigation(tab); });
    QTimer::singleShot(0, tab->windowConnections, [this, tab] { restoreTabViewState(tab); });
    QTimer::singleShot(0, tab->windowConnections, [this, tab] { applyLinkNavigation(tab); });
    const bool outlineChanged = tab->sidebarOutlineRevision != tab->session.document().outlineRevision();
    tab->outlineDirty = tab->outlineDirty || outlineChanged;
    tab->wordCountDirty = true;
    // A background parse updates only its own views; the sidebar and status describe the active tab.
    if (!tab->session.lastParseWasLocalEdit()) {
      if (tab_ != tab || tab->backend->isSourceMode()) {
        tab->renderViewDirty = true;
      } else {
        tab->renderView->setDocument(tab->session.document(), tab->session.filePath());
        tab->renderViewDirty = false;
      }
    }
    if (tab_ != tab) {
      return;
    }
    const QAction* action = commands_.action(QStringLiteral("view.sidebar"));
    const bool outlineVisible = action && action->isChecked() && sidebar_->panel() == SidebarWidget::Panel::Outline;
    if (tab->session.lastParseWasLocalEdit()) {
      if (outlineVisible && outlineChanged) {
        outlineTimer_->start();
      }
    } else {
      outlineTimer_->stop();
      if (outlineVisible) {
        refreshSidebarOutline();
      } else {
        sidebar_->clearOutline();
      }
    }
    scheduleWordCountUpdate();
    scheduleEditorStateRefresh();
  });
  connect(&tab->editorController, &EditorController::stateChanged, tab->windowConnections, [this, tab] {
    if (tab_ == tab) {
      scheduleEditorStateRefresh();
    }
  });
}

void muffin::MainWindow::queueExternalFileChange(DocumentTab* tab) {
  const auto path = tab->session.filePath();
  if (path.isEmpty()) {
    return;
  }
  tab->pendingExternalFileChangePath = path;
  // The pending decision owns this conflict; a background auto-save must not
  // open a second overwrite question while the user is deciding about reload.
  tab->autoSaveTimer->stop();
  if (tab == tab_)
    QTimer::singleShot(0, this, &MainWindow::showNextExternalFileChange);
}

void muffin::MainWindow::showNextExternalFileChange() {
  if (updateClosePrepared_ || externalFileChangePromptActive_ || suppressSaveOnTabSwitch_ || !tab_ ||
      tab_->pendingExternalFileChangePath.isEmpty())
    return;
  auto* tab = tab_;
  const auto path = tab->pendingExternalFileChangePath;
  if (tab->session.filePath() != path || !tab->session.hasFileBaseline()) {
    tab->pendingExternalFileChangePath.clear();
    return;
  }
  // A background tab may already have been saved/reloaded before activation.
  const QFileInfo info(path);
  if (info.exists() && info.lastModified() == tab->session.fileBaselineMtime() &&
      info.size() == tab->session.fileBaselineSize()) {
    tab->pendingExternalFileChangePath.clear();
    return;
  }
  externalFileChangePromptActive_ = true;
  auto* dialog = new QMessageBox(QMessageBox::Question, tr("File Changed on Disk"),
                                 tr("The file \"%1\" has changed outside Muffin. Reload it?\n"
                                    "Unsaved changes in the editor will be lost.")
                                     .arg(path),
                                 QMessageBox::Yes | QMessageBox::No, this);
  dialog->setObjectName(QStringLiteral("ExternalFileChangeDialog"));
  dialog->setDefaultButton(QMessageBox::Yes);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(
      dialog, &QDialog::finished, this,
      [this, document = QPointer<DocumentTab>(tab), path](int result) {
        if (document && document->pendingExternalFileChangePath == path) {
          // Rejecting a retired/deactivated question is not the user's No.
          // Keep it with the tab so activation in either window can ask again.
          const bool deferred =
              result != QMessageBox::Yes && result != QMessageBox::No && document->session.filePath() == path;
          if (!deferred)
            document->pendingExternalFileChangePath.clear();
          if (result == QMessageBox::Yes && document == tab_ && documentTabs().contains(document) &&
              document->session.filePath() == path) {
            // Read at confirmation time, not when the first watcher event arrived.
            document->fileController.reload(document->session, this);
          }
        }
        externalFileChangePromptActive_ = false;
        QTimer::singleShot(0, this, &MainWindow::showNextExternalFileChange);
      },
      Qt::SingleShotConnection);
  // Closing/moving a tab retires its window bindings. A question must neither
  // outlive those bindings nor reload a tab that now represents another path.
  connect(tab->windowConnections, &QObject::destroyed, dialog, &QDialog::reject);
  connect(&tab->session, &DocumentSession::filePathChanged, dialog, [dialog, path](const QString& current) {
    if (current != path)
      dialog->reject();
  });
  connect(tabs_, &QTabWidget::currentChanged, dialog, [this, tab, dialog](int) {
    if (tabs_->currentWidget() != tab)
      dialog->reject();
  });
  // open() returns to the application event loop; exec()/question() would
  // nest event loops and let subsequent watcher events stack more dialogs.
  dialog->open();
}

void muffin::MainWindow::connectApplicationSignals() {
  auto& window = *this;
  QObject::connect(&window.themeManager_, &ThemeManager::themeChanged, &window,
                   [&window](const QString& name) { window.applyTheme(name); });
  QObject::connect(&LanguageManager::instance(), &LanguageManager::languageChanged, &window, [&window] {
    window.retranslateUi();
    if (window.sidebar_) {
      window.sidebar_->retranslateUi();
    }
  });

  // Spell checker: re-decorate the rendered view's squiggles incrementally, NOT via a full
  // setDocument. setDocument clears and rebuilds the whole layout and repaints the entire
  // viewport; during the modal PreferencesDialog that full-viewport repaint races with the
  // dialog's window-blocking and can leave a stale/blank backing store until the next
  // resize/interaction (observed as the editor going blank when toggling the checkbox).
  // refreshBlocks rebuilds each top-level block — re-running the per-block spell predicate
  // baked into InlineLayout — while preserving scroll/cursor/selection and only repainting
  // dirty rects. The source-mode highlighter rehighlights itself on these signals.
  auto refreshSpellOverlay = [&window] {
    for (DocumentTab* document : window.documentTabs()) {
      document->renderView->refreshVisibleBlocks(document->session.document());
    }
  };
  QObject::connect(&SpellChecker::instance(), &SpellChecker::enabledChanged, &window, [&window, refreshSpellOverlay] {
    window.updateContextActions();
    refreshSpellOverlay();
  });
  QObject::connect(&SpellChecker::instance(), &SpellChecker::languageChanged, &window, refreshSpellOverlay);

  auto& updateChecker = muffin::UpdateChecker::instance();
  QObject::connect(
      &updateChecker, &muffin::UpdateChecker::updateAvailable, &window,
      [&window](const QString& version, const QString& url, QObject* requester) {
        if (requester == &window) {
          const auto& package = UpdateChecker::instance().latestPackage();
          if (WindowsUpdater::installedCopy() && package) {
            WindowsUpdater::instance().showUpdate(*package, &window);
            return;
          }
          const int result = QMessageBox::information(
              &window, muffin::MainWindow::tr("Update Available"),
              muffin::MainWindow::tr(
                  "A new version of Muffin (%1) is available.\n\nWould you like to open the download page?")
                  .arg(version),
              QMessageBox::Yes | QMessageBox::No);
          if (result == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl(url));
          }
        } else if (requester == nullptr && window.statusBar() && window.statusBar()->isVisible()) {
          const auto& package = UpdateChecker::instance().latestPackage();
          if (WindowsUpdater::installedCopy() && package) {
            if (!window.findChild<QPushButton*>(QStringLiteral("availableUpdateButton"))) {
              auto* button = new QPushButton(muffin::MainWindow::tr("Update Available"), &window);
              button->setObjectName(QStringLiteral("availableUpdateButton"));
              window.statusBar()->addPermanentWidget(button);
              QObject::connect(button, &QPushButton::clicked, &window, [&window] {
                const auto& latest = UpdateChecker::instance().latestPackage();
                if (latest)
                  WindowsUpdater::instance().showUpdate(*latest, &window);
              });
            }
          }
          window.statusBar()->showMessage(
              muffin::MainWindow::tr("Muffin %1 is available. Use Help > Check for Updates to download.").arg(version),
              15000);
        }
      });
  QObject::connect(&updateChecker, &muffin::UpdateChecker::upToDate, &window, [&window](QObject* requester) {
    if (requester == &window) {
      QMessageBox::information(&window, muffin::MainWindow::tr("Up to Date"),
                               muffin::MainWindow::tr("You are running the latest version of Muffin."));
    }
  });
  QObject::connect(&updateChecker, &muffin::UpdateChecker::checkFailed, &window,
                   [&window](const QString& errorMessage, QObject* requester) {
                     if (requester == &window) {
                       QMessageBox::warning(
                           &window, muffin::MainWindow::tr("Update Check Failed"),
                           muffin::MainWindow::tr("Could not check for updates:\n%1").arg(errorMessage));
                     }
                   });
}

void muffin::MainWindow::connectFindBarSignals(DocumentTab* tab) {
  connect(tab->findBar, &FindBarWidget::findRequested, tab->windowConnections,
          [this, tab](const QString& text, bool forward, bool regex, bool sensitive) {
            if (tab_ == tab) {
              performFind(text, forward, regex, sensitive);
            }
          });
  connect(tab->findBar, &FindBarWidget::closed, tab->windowConnections, [this, tab] {
    if (tab_ == tab) {
      hideFindBar();
    }
  });
  connect(tab->findBar, &FindBarWidget::replaceRequested, tab->windowConnections,
          [this, tab](const QString& find, const QString& replacement, bool regex, bool sensitive) {
            if (tab_ == tab) {
              performReplace(find, replacement, regex, sensitive);
            }
          });
  connect(tab->findBar, &FindBarWidget::replaceAllRequested, tab->windowConnections,
          [this, tab](const QString& find, const QString& replacement, bool regex, bool sensitive) {
            if (tab_ == tab) {
              performReplaceAll(find, replacement, regex, sensitive);
            }
          });
}

void muffin::MainWindow::connectChromeSignals() {
  auto& window = *this;
  QObject::connect(window.sidebarButton_, &QToolButton::clicked, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("view.sidebar"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sourceModeButton_, &QToolButton::clicked, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("view.source_mode"))) {
      action->trigger();
    }
  });

  // Status bar: clicking the stats trigger pops a detail panel with the computed counts.
  QObject::connect(window.statusBar_, &muffin::StatusBarWidget::statsClicked, &window, [&window] {
    if (!window.statusBar_) {
      return;
    }
    const PieceTable& md = window.tab_->session.markdownText();
    muffin::StatusBarStats stats;
    stats.words = muffin::text_stats::countWords(md);
    stats.characters = md.size();
    stats.lines = window.tab_->session.document().lineOffsets().lineCount();
    stats.readingMinutes = qMax(1, stats.words / 200);
    // Selection: source mode has a clean extraction; render mode slices the source range.
    QString selected;
    if (window.tab_->backend && window.tab_->backend->hasSelection()) {
      selected = window.tab_->backend->selectedText();
    } else if (window.tab_->backend && !window.tab_->backend->isSourceMode()) {
      const auto range = window.tab_->editorController.selection().selection();
      if (!range.isCollapsed()) {
        selected = md.mid(range.startOffset(), range.endOffset() - range.startOffset());
      }
    }
    if (!selected.isEmpty()) {
      stats.selectedWords = muffin::text_stats::countWords(QStringView(selected));
      stats.selectedCharacters = selected.size();
    }
    window.statusBar_->showStatsPopup(stats);
  });

  // Keep the spell-language button in sync when the checker changes.
  QObject::connect(&muffin::SpellChecker::instance(), &muffin::SpellChecker::languageChanged, &window,
                   [&window](const QString& code) {
                     if (window.statusBar_) {
                       window.statusBar_->setSpellLanguage(code, muffin::SpellChecker::instance().isEnabled());
                     }
                   });
  QObject::connect(&muffin::SpellChecker::instance(), &muffin::SpellChecker::enabledChanged, &window,
                   [&window](bool enabled) {
                     if (window.statusBar_) {
                       window.statusBar_->setSpellLanguage(muffin::SpellChecker::instance().language(), enabled);
                     }
                   });
}

void muffin::MainWindow::connectSidebarSignals() {
  auto& window = *this;
  auto* search = sidebar_->searchPanel();
  connect(search, &SearchPanelWidget::searchRequested, this,
          [this, search] { search->startSearch(openSearchBuffers()); });
  connect(search, &SearchPanelWidget::resultActivated, this, &MainWindow::navigateSearchResult);
  connect(search, &SearchPanelWidget::replaceRequested, this, &MainWindow::previewWorkspaceReplace);
  connect(sidebar_, &SidebarWidget::folderStateChanged, this, [this] {
    sidebarFolderRoot_ = sidebar_->folderRoot();
    updateFileActions();
    scheduleSessionSave();
  });
  connect(sidebar_, &SidebarWidget::newFileRequested, this, &MainWindow::newFileInDirectory);
  connect(sidebar_, &SidebarWidget::newFolderRequested, this, &MainWindow::newFolderInDirectory);
  QObject::connect(window.sidebar_, &SidebarWidget::panelChanged, &window, [&window](SidebarWidget::Panel panel) {
    if (panel == SidebarWidget::Panel::Outline) {
      window.refreshSidebarOutline();
    } else {
      window.outlineTimer_->stop();
      window.sidebar_->clearOutline();
      window.tab_->outlineDirty = true;
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::newWindowRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.new_window"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::fileOpenRequested, &window,
                   [&window](const QString& path) { window.openFile(path); });
  QObject::connect(window.sidebar_, &SidebarWidget::fileTreeContextMenuRequested, &window,
                   [&window](const QString& path, bool isDir, bool onItem, const QPoint& globalPos) {
                     window.buildSidebarContextMenu(path, isDir, onItem, globalPos);
                   });
  // Inline file-tree edit lifecycle: validate is synchronous (out-param) so the delegate can
  // decide commit-vs-keep-open on the same call; commit does the FS op + post-steps; cancel
  // deletes a pending-create temp.
  QObject::connect(window.sidebar_, &SidebarWidget::inlineValidateRequested, &window,
                   [&window](const muffin::InlineEditContext& ctx, const QString& name, muffin::InlineValidation* out) {
                     window.onInlineValidate(ctx, name, out);
                   });
  QObject::connect(
      window.sidebar_, &SidebarWidget::inlineCommitRequested, &window,
      [&window](const muffin::InlineEditContext& ctx, const QString& name) { window.onInlineCommit(ctx, name); });
  QObject::connect(window.sidebar_, &SidebarWidget::inlineCancelRequested, &window,
                   [&window](const muffin::InlineEditContext& ctx) { window.onInlineCancel(ctx); });
  QObject::connect(window.sidebar_, &SidebarWidget::outlineActivated, &window, &MainWindow::activateOutlineNode);
}
