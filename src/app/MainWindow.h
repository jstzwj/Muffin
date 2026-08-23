#pragma once

#include "app/CommandRegistry.h"
#include "app/CommandDeclarations.h"
#include "document/DocumentSession.h"
#include "app/EditorBackend.h"
#include "app/RenderCommandFacade.h"
#include "app/SidebarWidget.h"
#include "editor/EditorController.h"
#include "editor/EmojiProvider.h"
#include "io/FileController.h"
#include "app/DraftRecovery.h"
#include "export/ExportFormat.h"
#include "theme/ThemeManager.h"

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <memory>

class QLabel;
class QMenu;
class QPrinter;
class QActionGroup;
class QTimer;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QWidget;
class QVariantAnimation;
class QPropertyAnimation;

namespace muffin {

class EditorView;
class FindBarWidget;
class SourceEditorWidget;
class StatusBarWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

  // The command declaration table (CommandDeclarations.cpp) defines every
  // command's handler and enable/checked predicates against MainWindow state, so
  // it needs private access — a single narrow free-function friend rather than
  // the friend-classes removed in the binder→member split.
  friend const std::vector<CommandDeclaration>& commandDeclarations();

public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool openFile(QString path);
  // Open <path> as the sidebar folder root (switches to the Files panel).
  // Shared body of the File → Open Folder command and the new-window path,
  // and the entry point for the command-line --folder argument and the
  // Explorer "Open with Muffin" directory verb.
  void openFolderAtPath(QString path);
  // Honor the "files/startupBehavior" preference when the app is launched
  // without a file argument: reopen the most-recently-used file, or leave the
  // default empty document. Called from main() so a command-line file still wins.
  void restoreStartupFile();
  // Offer to restore unsaved drafts left by a previous (crashed/closed-unsaved)
  // session. Returns true if at least one draft was restored. Called from main()
  // before restoreStartupFile() so a restored draft takes precedence.
  bool offerDraftRecovery();
  bool saveCurrentDocument();
  bool isDocumentModified() const;
  // Quit the application (File → Quit / macOS Cmd+Q). Closes every top-level
  // window so each runs its closeEvent (unsaved-changes prompt); the event loop
  // ends when the last window closes.
  void quitApplication();

  // Composite editor-state queries backing the command declaration predicates
  // (CommandDeclarations.cpp). Exposed so the predicate table — which lives
  // outside the class — can evaluate enable/checked state without private access
  // or a scatter of friend helpers.
  bool commandHasCursor() const;
  bool commandHasSelection() const;
  bool commandOnEditableParagraph() const;
  int commandHeadingLevel() const;
  bool commandInlineFormatEnabled() const;
  bool commandInTableCell() const;
  bool commandOnImage() const;
  bool commandOnLocalImage() const;

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  bool startNewDocument();
  // Create and register one QAction for a declared command, pulling its label,
  // shortcut, checkable state and action-group from the declaration table. The
  // menu walker (buildMenus) calls this for every Action item in mainMenuSpec().
  QAction* registerAction(QMenu* menu, const QString& id);

  void setupUi();
  void setupMenuBar();
  // Update menu action labels and menu/submenu titles in place for a locale
  // change — unlike setupMenuBar(), this deletes/recreates nothing (which is what
  // made the language switch crash: deleting menu actions while Qt still has
  // LanguageChange events pending for them corrupted the heap).
  void retranslateMenuTexts();
  void setupStatusBar();
  void setupConnections();
  void retranslateUi();

  // Command binding and per-domain action enable/checked refresh. These were
  // once free functions in a friend MainWindowActionBinder; they are members now
  // so MainWindow carries no friend coupling and the binding lives with the
  // window like the other MainWindow*.cpp partials.
  void bindCommands();
  void restorePersistentActionStates();
  void updateFileActions();
  void updateEditActions();
  void updateTableActions();
  void updateParagraphActions();
  void updateCodeActions();
  void updateHtmlActions();
  void updateMathActions();
  void updateImageActions();
  void updateFormatActions();
  void updateContextActions();
  void updateThemeActions();
  // Sync the radio-style checkmarks in the Theme menu to the active theme.
  // Called from applyTheme — the single choke point every theme change flows
  // through (incl. the startup restore) — so the checks can never desync from
  // the visuals. Lighter than rebuildThemesMenu: no QActions are created/removed.
  void updateThemeChecks();
  // Apply every command's enabled/checked predicate within one category (the
  // per-domain update*Actions above are thin wrappers around this so call sites
  // stay unchanged). updateAllActions() refreshes every category.
  void updateActionsForCategory(CommandCategory category);
  void updateAllActions();
  void scheduleEditorStateRefresh();

  // Qt signal wiring, split out for readability (formerly MainWindowSignalBinder).
  void connectEditorSignals();
  void connectRenderSignals();
  void connectSessionSignals();
  void connectApplicationSignals();
  void connectFindBarSignals();
  void connectChromeSignals();
  void connectSidebarSignals();

  // Menu bar built from mainMenuSpec() (CommandDeclarations.cpp). setupMenuBar()
  // clears and calls buildMenus(); retranslateUi() calls setupMenuBar() to
  // rebuild with retranslated labels.
  void buildMenus();
  void buildMenuItems(QMenu* parent, const std::vector<MenuItem>& items);

  void updateTitle();
  void updateStatus();
  void updateCursorStatus(int line, int column);
  void updateRenderCursorStatus(const HitTestResult& hit);
  // Assemble and exec the rendered-mode right-click menu for `hit`. Reuses the
  // registered command actions (labels/shortcuts/handlers/enabled-state for
  // free), keyed off the click's zone/link/image/table context. The caret has
  // already been moved to the click by EditorView, so caret-based commands land
  // on the right target. Mirrors the tableMoreActionsRequested pattern.
  void buildEditorContextMenu(const HitTestResult& hit, QPoint globalPos);
  void exportMermaidDiagram(NodeId blockId);
  // editor/showBlockSource: render-mode status-bar preview of the raw markdown of the block under
  // the caret. No-op (clears the label) when the setting is off, in source mode, or without a hit.
  void updateBlockSourceLabel(const HitTestResult& hit);
  void updateSidebarMode();
  void updateViewMode();
  // Sidebar width transition + mode-switch crossfade. Skipped when reducedMotion() is set or the
  // window isn't shown yet (animating before showEvent leaves chrome mid-transition at startup).
  bool reducedMotion() const;
  void animateSidebarWidth(int from, int to);
  void showViewSwitchOverlay(const QPixmap& snapshot);
  int zoomPercent() const;
  void setZoomPercent(int percent);
  int fontSizePx() const;
  void setFontSizePx(int px);
  int contentWidthPx() const;
  void setContentWidthPx(int px);
  void setStatusBarVisible(bool visible);
  void loadAppearanceSettings();
  void saveAppearanceTheme(const QString& name) const;
  void saveAppearanceStatusBarVisible(bool visible) const;
  void saveAppearanceZoomPercent(int percent) const;
  void saveAppearanceFontSizePx(int px) const;
  void saveAppearanceContentWidthPx(int px) const;
  void setFocusMode(bool enabled);
  void setTypewriterMode(bool enabled);
  void saveAppearanceFocusMode(bool enabled) const;
  void saveAppearanceTypewriterMode(bool enabled) const;
  void setSidebarPanel(SidebarWidget::Panel panel);
  void refreshSidebarDocumentInfo();
  void refreshSidebarOutline();
  void openFolder();
  void openNewWindow();
  void activateOutlineNode(NodeId nodeId, SourceRange sourceRange);
  void syncSourceEditorIfNeeded();
  void scheduleWordCountUpdate();
  void updateWordCountNow();
  void undoEdit();
  void redoEdit();
  void applyTheme(QString name);
  // Toggle the OS-drawn title bar (Windows caption + system buttons) to match
  // the theme brightness. Style sheets can't reach it; on Windows this calls
  // DwmSetWindowAttribute, elsewhere it's a no-op.
  void applyNativeTitleBarDarkMode(bool dark);
  // Theme menu is enumerated from ThemeManager::definitions() (built-ins +
  // imported custom themes), so it is rebuilt rather than a fixed command list.
  void rebuildThemesMenu();
  // Apply a theme by id (menu + Preferences dropdown path): persists the choice
  // and refreshes the menu checks. The visual apply happens via themeChanged.
  void setThemeByName(const QString& name);
  // File → pick a *.json, copy into the user themes dir, reload the registry.
  void importTheme();
  // Reveal the user themes directory in the OS file manager.
  void openThemesFolder();
  void rebuildRecentFilesMenu();
  void addRecentFile(QString path);
  void addRecentFolder(QString path);
  QStringList recentFiles() const;
  void setRecentFiles(const QStringList& paths) const;
  QStringList recentFolders() const;
  void setRecentFolders(const QStringList& paths) const;
  // Directory offered by Save As for an untitled document: the sidebar's open
  // folder if one is set, else empty (falls back to the working directory).
  QString defaultSaveDirectory() const;
  // Suggested name for the sidebar New File prompt, honoring files/defaultExtension.
  QString defaultUntitledSuggestion() const;
  void showDocumentProperties();
  void showPreferences();
  void printDocument();
  // File → Quick Open (Ctrl+P): pick from recent files plus the current file's
  // sibling Markdown files.
  void quickOpen();
  QStringList quickOpenCandidates() const;
  // File → Import: convert a foreign format to markdown via Pandoc and load it
  // into a new untitled document.
  void importFile();
  // File → Export: write the document to a file in the given format (native PDF
  // / HTML, or Pandoc-driven for the rest).
  void exportAs(ExportFormat format);
  // Paints the laid-out document onto a printer (shared by Print and PDF
  // export). Extracted from printDocument so PDF export can reuse it.
  // Returns false when painting could not start (null printer or inactive
  // QPainter) — callers must not report a successful export then.
  bool paintDocumentToPrinter(QPrinter* printer);
  void revealCurrentFile();
  bool maybeSaveChanges();
  void reopenWithEncoding(const QString& encodingName);
  void buildReopenEncodingMenu();
  void moveToFile();
  void saveAllOpenFiles();
  void showInSidebar();
  void deleteFile();
  // ---- Sidebar file-tree context menu operations ----
  // Build and exec the right-click menu for the file tree; the variant (file /
  // directory / empty space) is decided from the click target.
  void buildSidebarContextMenu(QString path, bool isDir, bool onItem, QPoint globalPos);
  void newFileInDirectory(QString dir);
  void newFolderInDirectory(QString dir);
  void renamePath(QString path);
  void duplicateFile(QString path);
  void deletePath(QString path);
  void showPathProperties(QString path);
  // Inline file-tree edit handlers (validate / commit / cancel), connected to SidebarWidget
  // signals. The FS op + preserved post-steps (open doc sync, open-on-create) live here.
  void onInlineValidate(muffin::InlineEditContext ctx, const QString& name, muffin::InlineValidation* out);
  void onInlineCommit(muffin::InlineEditContext ctx, const QString& name);
  void onInlineCancel(muffin::InlineEditContext ctx);
  void copyPathToClipboard(QString path) const;
  // Reveal in the OS file manager, selecting the item where the platform allows.
  // revealCurrentFile() delegates here so the menu command shares the behavior.
  void revealPathInManager(QString path);
  void openFileInNewWindow(QString path);
  void openFolderInNewWindow(QString path);
  void insertTableWithDialog();
  void insertImageWithDialog();
  void insertLocalImageWithDialog();

  struct CommandContextSnapshot {
    bool hasCursor = false;
    bool hasSelection = false;
    bool onEditableParagraph = false;
    int headingLevel = -1;
    bool inlineFormatEnabled = false;
    bool inTableCell = false;
    bool onImage = false;
    bool onLocalImage = false;
  };
  CommandContextSnapshot captureCommandContext() const;

  void showFindBar();
  void showReplaceBar();
  void hideFindBar();
  void performFind(const QString& text, bool forward, bool regularExpression, bool caseSensitive);
  void performFindNext();
  void performFindPrevious();
  void performReplace(const QString& findText, const QString& replaceText,
                      bool regularExpression, bool caseSensitive);
  void performReplaceAll(const QString& findText, const QString& replaceText,
                         bool regularExpression, bool caseSensitive);
  void performAutoSave();
  void snapshotDraft();
  // Crash-recovery heartbeat interval, scaled by document length: floor 3s, then +1s per ~2k
  // characters. The length is QString::size() — UTF-16 CODE UNITS, not on-disk bytes — so a
  // CJK-heavy doc (1 code unit per char but 3 UTF-8 bytes) scales by its character count, not byte
  // size (a "50MB on disk" CJK doc is ~25M code units → ~12s, not ~25s). Bounds typing-at-risk on a
  // crash while keeping the O(doc) encode + sync write off the typing path on large documents.
  int draftSnapshotIntervalMs() const;
  bool restoreDraft(const DraftRecovery::PendingDraft& draft);

  DocumentSession session_;
  FileController fileController_;
  DraftRecovery drafts_;
  QString draftKey_ = DraftRecovery::createDraftKey();
  CommandRegistry commands_;
  ThemeManager themeManager_;
  EditorController editorController_;
  BundledEmojiProvider emojiProvider_;
  RenderCommandFacade renderCommands_;
  QSplitter* centralSplitter_ = nullptr;
  SidebarWidget* sidebar_ = nullptr;
  QStackedWidget* viewStack_ = nullptr;
  EditorView* renderView_ = nullptr;
  SourceEditorWidget* editor_ = nullptr;
  FindBarWidget* findBar_ = nullptr;
  QToolButton* sidebarButton_ = nullptr;
  QToolButton* sourceModeButton_ = nullptr;
  StatusBarWidget* statusBar_ = nullptr;
  // Sidebar width transition (QVariantAnimation drives min+max in lockstep so the splitter tracks
  // the value exactly) and the mode-switch snapshot overlay (a grab()'d pixmap QLabel faded out by
  // overlayOpacityAnimation_ — never a live QGraphicsOpacityEffect on the heavy editor widgets).
  QVariantAnimation* sidebarAnimation_ = nullptr;
  QPropertyAnimation* overlayOpacityAnimation_ = nullptr;
  QLabel* viewSwitchOverlay_ = nullptr;
  int sidebarTargetWidth_ = 260;
  QTimer* wordCountTimer_ = nullptr;
  QTimer* outlineTimer_ = nullptr;
  QTimer* autoSaveTimer_ = nullptr;
  QTimer* draftTimer_ = nullptr;
  // Document revision at the last draft snapshot this dirty period. snapshotDraft() skips the
  // O(doc) UTF-8 encode + sync disk write when the revision is unchanged (the old code compared a
  // full copy of the text — an O(doc) copy + compare on every heartbeat). Reset to 0 on clean so
  // the next dirty period snapshots immediately.
  quint64 lastDraftSnapshotRevision_ = 0;
  QMenu* recentFilesMenu_ = nullptr;
  QMenu* reopenEncodingMenu_ = nullptr;
  QMenu* themesMenu_ = nullptr;
  // Exclusive radio groups (image resize, image insert action), keyed by the
  // declaration's actionGroup id. Rebuilt on every menu rebuild.
  QHash<QString, QActionGroup*> actionGroups_;
  int cursorLine_ = 1;
  int cursorColumn_ = 1;
  QString renderCursorStatus_;
  bool lastFindRegularExpression_ = false;
  bool lastFindCaseSensitive_ = false;
  QString sidebarFolderRoot_;
  int zoomPercent_ = 100;
  int fontSizePx_ = 16;
  int contentWidthPx_ = 0;
  bool renderViewDirty_ = false;
  bool wordCountDirty_ = true;
  bool outlineDirty_ = true;
  bool showBlockSourceEnabled_ = false;
  bool editorStateRefreshScheduled_ = false;
  mutable bool commandContextSnapshotActive_ = false;
  mutable CommandContextSnapshot commandContextSnapshot_;
  quint64 sidebarOutlineRevision_ = 0;
  std::unique_ptr<EditorBackend> backend_;
  bool focusMode_ = false;
  bool typewriterMode_ = false;
  QString lastFindText_;
  qsizetype lastFindOffset_ = -1;
};

}  // namespace muffin
