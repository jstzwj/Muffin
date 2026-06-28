#include "app/LanguageManager.h"
#include "app/MainWindow.h"
#include "app/UpdateChecker.h"
#include "app/SidebarWidget.h"
#include "app/StatusBarWidget.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "spellcheck/SpellChecker.h"

#include <QAction>
#include <QDesktopServices>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QMessageBox>
#include <QStatusBar>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMenu>
#include <QToolButton>

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

}  // namespace

void muffin::MainWindow::connectEditorSignals() {
  auto& window = *this;  // preserves the window.X call sites after the friend→member split
  QObject::connect(window.editor_, &SourceEditorWidget::textEdited, &window.session_, [&window](const QString& text) {
    // In render mode the (hidden, possibly empty) source widget is a shadow, not the source
    // of truth — the user edits through the rendered view. Its textChanged must not write
    // back to the document, because the source widget may be empty/out of sync (it is only
    // populated on entering source mode). Otherwise a spurious textChanged — e.g. from
    // QSyntaxHighlighter::rehighlight when toggling spell check — would call updateFromEditor
    // with an empty string and wipe the document. Only the source-mode backend edits via it.
    if (!window.backend_ || !window.backend_->isSourceMode()) {
      return;
    }
    window.session_.updateFromEditor(text);
  });
  QObject::connect(window.editor_, &SourceEditorWidget::cursorPositionChanged, &window, [&window](int line, int column) {
    window.updateCursorStatus(line, column);
    if (window.typewriterMode_ && window.backend_->isSourceMode()) {
      window.backend_->centerCursor();
    }
  });
  QObject::connect(window.editor_, &SourceEditorWidget::cursorPositionChanged, &window, [&window](int, int) {
    window.updateEditActions();
  });
}

void muffin::MainWindow::connectRenderSignals() {
  auto& window = *this;
  QObject::connect(&window.editorController_, &EditorController::cursorChanged, &window, [&window](const HitTestResult& hit) {
    window.updateRenderCursorStatus(hit);
    if (window.typewriterMode_ && !window.backend_->isSourceMode()) {
      window.renderView_->scrollToCursorCenteredAnimated();
    }
  });
  QObject::connect(window.renderView_, &EditorView::codeLanguageCommitted, &window, [&window](NodeId codeId, const QString& language) {
    if (window.backend_->isSourceMode()) {
      return;
    }
    window.renderCommands_.setCodeLanguageFor(codeId, language);
  });
  QObject::connect(window.renderView_, &EditorView::tableResizeRequested, &window, [&window](int rows, int columns) {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.resizeCurrentTable(rows, columns);
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableColumnAlignmentRequested, &window, [&window](TableAlignment alignment) {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.setCurrentColumnAlignment(alignment);
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableDeleteRequested, &window, [&window] {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.deleteCurrentTable();
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableMoreActionsRequested, &window, [&window](QPoint globalPos) {
    if (window.backend_->isSourceMode()) {
      return;
    }
    window.updateTableActions();
    window.updateParagraphActions();
    QMenu menu(&window);
    const QStringList ids = {
        QStringLiteral("table.insert_table"),
        QStringLiteral("table.insert_row_before"),
        QStringLiteral("table.insert_row_after"),
        QStringLiteral("table.insert_column_before"),
        QStringLiteral("table.insert_column_after"),
        QStringLiteral("table.move_row_up"),
        QStringLiteral("table.move_row_down"),
        QStringLiteral("table.move_column_left"),
        QStringLiteral("table.move_column_right"),
        QStringLiteral("table.delete_row"),
        QStringLiteral("table.delete_column"),
        QStringLiteral("table.copy_table"),
        QStringLiteral("table.format_source"),
        QStringLiteral("table.align_none"),
        QStringLiteral("table.delete_table"),
    };
    for (const QString& id : ids) {
      if (id == QStringLiteral("table.insert_row_before") || id == QStringLiteral("table.insert_column_before") || id == QStringLiteral("table.move_row_up") ||
          id == QStringLiteral("table.delete_row") || id == QStringLiteral("table.copy_table") || id == QStringLiteral("table.align_none") ||
          id == QStringLiteral("table.delete_table")) {
        menu.addSeparator();
      }
      if (QAction* action = window.commands_.action(id)) {
        menu.addAction(action);
      }
    }
    menu.exec(globalPos);
  });
  QObject::connect(window.renderView_, &EditorView::contextMenuRequested, &window,
      [&window](HitTestResult hit, QPoint globalPos) {
        if (window.backend_->isSourceMode()) {
          return;  // source mode keeps the native QPlainTextEdit context menu
        }
        window.buildEditorContextMenu(hit, globalPos);
      });

  // External drag-and-drop of folders / documents is handled by the main window
  // according to the files/drop* preferences (images are still inserted inline).
  QObject::connect(window.renderView_, &EditorView::folderDropped, &window, [&window](const QString& path) {
    // files/dropFolder: 0 = open in Muffin (sidebar root), 1 = open in file manager.
    const int action = QSettings().value(QStringLiteral("files/dropFolder"), 0).toInt();
    if (action == 1) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (window.sidebar_) {
      window.sidebar_->setFolderRoot(path);
      window.setSidebarPanel(SidebarWidget::Panel::Files);
    }
  });
  QObject::connect(window.renderView_, &EditorView::markdownFileDropped, &window, [&window](const QString& path) {
    // Markdown is Muffin's native format, so a dropped .md/.txt always opens.
    // (files/dropMarkdown is reserved for a future "import into current doc" action.)
    window.openFile(path);
  });
  QObject::connect(window.renderView_, &EditorView::importableFileDropped, &window, [&window](const QString& path) {
    // files/dropImportable: 1 = open as text; 0 (default) = import. Import has no
    // backend yet, so it no-ops — which also means a binary/unknown drop (PDF,
    // DOCX, …) is ignored rather than force-opened as text garbage.
    const int action = QSettings().value(QStringLiteral("files/dropImportable"), 0).toInt();
    if (action == 1) {
      window.openFile(path);
    }
  });
}

void muffin::MainWindow::connectSessionSignals() {
  auto& window = *this;
  QObject::connect(&window.session_, &DocumentSession::documentTextChanged, &window, [&window](const QString& text) {
    PerfTimer perf("main.documentTextChanged.consumer");
    if (window.backend_->isSourceMode()) {
      window.editor_->setText(text);
      window.sourceEditorDirty_ = false;
      return;
    }
    window.sourceEditorDirty_ = true;
  });
  QObject::connect(&window.session_, &DocumentSession::documentLocallyEdited, &window, [&window](qsizetype, qsizetype, const QString&) {
    PerfTimer perf("main.documentLocallyEdited.consumer");
    window.sourceEditorDirty_ = true;
  });
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, &MainWindow::updateTitle);
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, [&window] {
    window.updateFileActions();
  });
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, &MainWindow::refreshSidebarDocumentInfo);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::updateTitle);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::updateStatus);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::refreshSidebarDocumentInfo);
  // Drive auto-save: arm the debounce timer when a pathed document becomes dirty
  // (and files/autoSave is on); stop it once clean. The timer does the gated write.
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, [&window](bool modified) {
    if (modified && QSettings().value(QStringLiteral("files/autoSave"), false).toBool()
        && !window.session_.filePath().isEmpty()) {
      window.autoSaveTimer_->start();
    } else {
      window.autoSaveTimer_->stop();
    }
  });
  // Arm the crash-recovery snapshot timer whenever the document becomes dirty;
  // snapshotDraft() re-arms it (the heartbeat) while it stays dirty, and stops
  // here once clean. Resetting the content tracker lets the next dirty period
  // snapshot immediately instead of deduping against stale text.
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, [&window](bool modified) {
    if (modified) {
      window.draftTimer_->start(window.draftSnapshotIntervalMs());
    } else {
      window.draftTimer_->stop();
      window.lastDraftSnapshotRevision_ = 0;  // next dirty period snapshots immediately
    }
  });
  // When unsaved work is resolved (saved or explicitly discarded), clear that
  // document's recovery draft — see FileController::documentBecameClean.
  QObject::connect(&window.fileController_, &FileController::documentBecameClean, &window,
                   [&window](const QString& filePath) { window.drafts_.markClean(filePath); });
  QObject::connect(&window.session_, &DocumentSession::parseBusy, &window, [&window](bool busy) {
    window.renderView_->setLoading(busy);
  });
  QObject::connect(&window.session_, &DocumentSession::parsed, &window, [&window] {
    PerfTimer perf("main.parsed.consumer");
    if (window.session_.lastParseWasLocalEdit()) {
      // Local edit (typing): debounce the outline rebuild so a full-tree heading walk does not
      // run on every keystroke. Word count is already debounced; status is O(1).
      window.outlineTimer_->start();
    } else {
      // Full parse (open/import/options change): the view and outline may have changed wholesale.
      window.outlineTimer_->stop();
      window.renderView_->setDocument(window.session_.document(), window.session_.filePath());
      window.refreshSidebarOutline();
    }
    window.scheduleWordCountUpdate();
    window.updateStatus();
  });
}

void muffin::MainWindow::connectApplicationSignals() {
  auto& window = *this;
  QObject::connect(&window.editorController_, &EditorController::stateChanged, &window, [&window] {
    window.updateStatus();
    window.updateContextActions();
  });
  QObject::connect(&window.themeManager_, &ThemeManager::themeChanged, &window, [&window](const QString& name) {
    window.applyTheme(name);
  });
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
    if (!window.renderView_) {
      return;
    }
    // Only the visible (promoted) blocks are re-decorated now; offscreen blocks pick up the new
    // spell state when they scroll into view. Avoids rebuilding an entire large document on toggle.
    window.renderView_->refreshVisibleBlocks(window.session_.document());
  };
  QObject::connect(&SpellChecker::instance(), &SpellChecker::enabledChanged, &window,
      [&window, refreshSpellOverlay] {
        window.updateContextActions();
        refreshSpellOverlay();
      });
  QObject::connect(&SpellChecker::instance(), &SpellChecker::languageChanged, &window, refreshSpellOverlay);

  auto& updateChecker = muffin::UpdateChecker::instance();
  QObject::connect(&updateChecker, &muffin::UpdateChecker::updateAvailable, &window, [&window](const QString& version, const QString& url) {
    if (muffin::UpdateChecker::instance().isUserInitiated()) {
      const int result = QMessageBox::information(&window,
          muffin::MainWindow::tr("Update Available"),
          muffin::MainWindow::tr("A new version of Muffin (%1) is available.\n\nWould you like to open the download page?").arg(version),
          QMessageBox::Yes | QMessageBox::No);
      if (result == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(url));
      }
    } else if (window.statusBar() && window.statusBar()->isVisible()) {
      window.statusBar()->showMessage(
          muffin::MainWindow::tr("Muffin %1 is available. Use Help > Check for Updates to download.").arg(version),
          15000);
    }
  });
  QObject::connect(&updateChecker, &muffin::UpdateChecker::upToDate, &window, [&window] {
    if (muffin::UpdateChecker::instance().isUserInitiated()) {
      QMessageBox::information(&window,
          muffin::MainWindow::tr("Up to Date"),
          muffin::MainWindow::tr("You are running the latest version of Muffin."));
    }
  });
  QObject::connect(&updateChecker, &muffin::UpdateChecker::checkFailed, &window, [&window](const QString& errorMessage) {
    if (muffin::UpdateChecker::instance().isUserInitiated()) {
      QMessageBox::warning(&window,
          muffin::MainWindow::tr("Update Check Failed"),
          muffin::MainWindow::tr("Could not check for updates:\n%1").arg(errorMessage));
    }
  });
}

void muffin::MainWindow::connectFindBarSignals() {
  auto& window = *this;
  QObject::connect(window.findBar_, &FindBarWidget::findRequested, &window, &MainWindow::performFind);
  QObject::connect(window.findBar_, &FindBarWidget::closed, &window, &MainWindow::hideFindBar);
  QObject::connect(window.findBar_, &FindBarWidget::replaceRequested, &window, &MainWindow::performReplace);
  QObject::connect(window.findBar_, &FindBarWidget::replaceAllRequested, &window, &MainWindow::performReplaceAll);
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
    const PieceTable& md = window.session_.markdownText();
    muffin::StatusBarStats stats;
    stats.words = muffin::MainWindow::countWords(md.toString());
    stats.characters = md.size();
    stats.lines = window.session_.document().lineOffsets().lineCount();
    stats.readingMinutes = qMax(1, stats.words / 200);
    // Selection: source mode has a clean extraction; render mode slices the source range.
    QString selected;
    if (window.backend_ && window.backend_->hasSelection()) {
      selected = window.backend_->selectedText();
    } else if (window.backend_ && !window.backend_->isSourceMode()) {
      const auto range = window.editorController_.selection().selection();
      if (!range.isCollapsed()) {
        selected = md.mid(range.startOffset(), range.endOffset() - range.startOffset());
      }
    }
    if (!selected.isEmpty()) {
      stats.selectedWords = muffin::MainWindow::countWords(selected);
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
  QObject::connect(window.sidebar_, &SidebarWidget::newFileRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.new"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::newWindowRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.new_window"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::openFolderRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.open_folder"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::fileOpenRequested, &window, [&window](const QString& path) {
    window.openFile(path);
  });
  QObject::connect(window.sidebar_, &SidebarWidget::outlineActivated, &window, &MainWindow::activateOutlineNode);
}
