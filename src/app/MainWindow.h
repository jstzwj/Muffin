#pragma once

#include "app/CommandRegistry.h"
#include "document/DocumentSession.h"
#include "app/EditorBackend.h"
#include "app/RenderCommandFacade.h"
#include "app/SidebarWidget.h"
#include "editor/EditorController.h"
#include "io/FileController.h"
#include "theme/ThemeManager.h"

#include <QMainWindow>
#include <QString>
#include <memory>

class QLabel;
class QMenu;
class QTimer;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace muffin {

class EditorView;
class FindBarWidget;
class MainWindowActionBinder;
class MainWindowSignalBinder;
class SourceEditorWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT
  friend class MainWindowActionBinder;
  friend class MainWindowSignalBinder;

public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool openFile(QString path);
  bool saveCurrentDocument();
  bool isDocumentModified() const;

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  QAction* addAction(
      QMenu* menu,
      const QString& id,
      const QString& text,
      const QKeySequence& shortcut = {},
      bool enabled = true);
  QAction* addCheckAction(
      QMenu* menu,
      const QString& id,
      const QString& text,
      const QKeySequence& shortcut = {},
      bool checked = false,
      bool enabled = true);

  void setupUi();
  void setupMenuBar();
  void setupStatusBar();
  void setupConnections();
  void applyTyporaLikeChrome();
  void retranslateUi();

  void setupFileMenu();
  void setupEditMenu();
  void setupParagraphMenu();
  void setupTableMenu();
  void populateTableMenu(QMenu* menu);
  void setupCodeMenu();
  void setupHtmlMenu();
  void setupMathMenu();
  void setupFormatMenu();
  void setupViewMenu();
  void setupThemeMenu();
  void setupHelpMenu();

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
  QLabel* parseLabel_ = nullptr;
  QLabel* cursorLabel_ = nullptr;
  QLabel* wordsLabel_ = nullptr;
  QTimer* wordCountTimer_ = nullptr;
  QMenu* recentFilesMenu_ = nullptr;
  QMenu* reopenEncodingMenu_ = nullptr;
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
