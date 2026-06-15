#pragma once

#include "app/CommandRegistry.h"
#include "app/CommandDeclarations.h"
#include "document/DocumentSession.h"
#include "app/EditorBackend.h"
#include "app/RenderCommandFacade.h"
#include "app/SidebarWidget.h"
#include "editor/EditorController.h"
#include "io/FileController.h"
#include "theme/ThemeManager.h"

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <memory>

class QLabel;
class QMenu;
class QActionGroup;
class QTimer;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace muffin {

class EditorView;
class FindBarWidget;
class SourceEditorWidget;

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
  bool saveCurrentDocument();
  bool isDocumentModified() const;

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

private:
  // Create and register one QAction for a declared command, pulling its label,
  // shortcut, checkable state and action-group from the declaration table. The
  // menu walker (buildMenus) calls this for every Action item in mainMenuSpec().
  QAction* registerAction(QMenu* menu, const QString& id);

  void setupUi();
  void setupMenuBar();
  void setupStatusBar();
  void setupConnections();
  void applyEditorChrome();
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
  // Apply every command's enabled/checked predicate within one category (the
  // per-domain update*Actions above are thin wrappers around this so call sites
  // stay unchanged). updateAllActions() refreshes every category.
  void updateActionsForCategory(CommandCategory category);
  void updateAllActions();

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
  void updateSidebarMode();
  void updateViewMode();
  int zoomPercent() const;
  void setZoomPercent(int percent);
  int fontSizePx() const;
  void setFontSizePx(int px);
  void setStatusBarVisible(bool visible);
  void loadAppearanceSettings();
  void saveAppearanceTheme(const QString& name) const;
  void saveAppearanceStatusBarVisible(bool visible) const;
  void saveAppearanceZoomPercent(int percent) const;
  void saveAppearanceFontSizePx(int px) const;
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
  static int countWords(const QString& text);
  void undoEdit();
  void redoEdit();
  void applyTheme(QString name);
  void rebuildRecentFilesMenu();
  void addRecentFile(QString path);
  QStringList recentFiles() const;
  void setRecentFiles(const QStringList& paths) const;
  void showDocumentProperties();
  void showPreferences();
  void printDocument();
  void revealCurrentFile();
  bool maybeSaveChanges();
  void reopenWithEncoding(const QString& encodingName);
  void buildReopenEncodingMenu();
  void moveToFile();
  void saveAllOpenFiles();
  void showInSidebar();
  void deleteFile();
  void insertTableWithDialog();
  void insertImageWithDialog();
  void insertLocalImageWithDialog();

  void showFindBar();
  void showReplaceBar();
  void hideFindBar();
  void performFind(const QString& text, bool forward);
  void performFindNext();
  void performFindPrevious();
  void performReplace(const QString& findText, const QString& replaceText);
  void performReplaceAll(const QString& findText, const QString& replaceText);

  DocumentSession session_;
  FileController fileController_;
  CommandRegistry commands_;
  ThemeManager themeManager_;
  EditorController editorController_;
  RenderCommandFacade renderCommands_;
  QSplitter* centralSplitter_ = nullptr;
  SidebarWidget* sidebar_ = nullptr;
  QStackedWidget* viewStack_ = nullptr;
  EditorView* renderView_ = nullptr;
  SourceEditorWidget* editor_ = nullptr;
  FindBarWidget* findBar_ = nullptr;
  QToolButton* sidebarButton_ = nullptr;
  QToolButton* sourceModeButton_ = nullptr;
  QLabel* cursorLabel_ = nullptr;
  QLabel* wordsLabel_ = nullptr;
  QTimer* wordCountTimer_ = nullptr;
  QMenu* recentFilesMenu_ = nullptr;
  QMenu* reopenEncodingMenu_ = nullptr;
  // Exclusive radio groups (image resize, image insert action), keyed by the
  // declaration's actionGroup id. Rebuilt on every menu rebuild.
  QHash<QString, QActionGroup*> actionGroups_;
  int cursorLine_ = 1;
  int cursorColumn_ = 1;
  QString renderCursorStatus_;
  QString sidebarFolderRoot_;
  int zoomPercent_ = 100;
  int fontSizePx_ = 16;
  bool sourceEditorDirty_ = false;
  bool wordCountDirty_ = true;
  std::unique_ptr<EditorBackend> backend_;
  bool focusMode_ = false;
  bool typewriterMode_ = false;
  QString lastFindText_;
  qsizetype lastFindOffset_ = -1;
};

}  // namespace muffin
