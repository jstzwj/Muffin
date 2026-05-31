#pragma once

#include "app/CommandRegistry.h"
#include "app/DocumentSession.h"
#include "io/FileController.h"

#include <QMainWindow>
#include <QString>

class QLabel;
class QMenu;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace muffin {

class MarkdownRenderWidget;
class SourceEditorWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool openFile(QString path);

protected:
  void closeEvent(QCloseEvent* event) override;

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

  void setupFileMenu();
  void setupEditMenu();
  void setupParagraphMenu();
  void setupFormatMenu();
  void setupViewMenu();
  void setupThemeMenu();
  void setupHelpMenu();

  void updateTitle();
  void updateStatus();
  void updateCursorStatus(int line, int column);
  void updateSidebarMode();
  void updateViewMode();
  void updateFileActions();
  void rebuildRecentFilesMenu();
  void addRecentFile(QString path);
  QStringList recentFiles() const;
  void setRecentFiles(const QStringList& paths) const;
  void showDocumentProperties();
  void revealCurrentFile();
  bool maybeSaveChanges();

  DocumentSession session_;
  FileController fileController_;
  CommandRegistry commands_;
  QSplitter* centralSplitter_ = nullptr;
  QWidget* sidebar_ = nullptr;
  QStackedWidget* viewStack_ = nullptr;
  MarkdownRenderWidget* renderView_ = nullptr;
  SourceEditorWidget* editor_ = nullptr;
  QToolButton* sidebarButton_ = nullptr;
  QToolButton* sourceModeButton_ = nullptr;
  QLabel* parseLabel_ = nullptr;
  QLabel* cursorLabel_ = nullptr;
  QLabel* wordsLabel_ = nullptr;
  QMenu* recentFilesMenu_ = nullptr;
  int cursorLine_ = 1;
  int cursorColumn_ = 1;
  int zoomPercent_ = 100;
};

}  // namespace muffin
