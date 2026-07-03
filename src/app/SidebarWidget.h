#pragma once

#include "document/OutlineBuilder.h"
#include "theme/ThemeDefinition.h"

#include <QPoint>
#include <QWidget>

class QFileSystemModel;
class QLabel;
class QStackedWidget;
class QToolButton;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

namespace muffin {

class SidebarWidget final : public QWidget {
  Q_OBJECT

public:
  enum class Panel { Files, Outline };

  explicit SidebarWidget(QWidget* parent = nullptr);

  void setPanel(Panel panel);
  Panel panel() const;
  void setCurrentDocument(QString displayName, QString filePath, bool modified);
  void setFolderRoot(QString path);
  QString folderRoot() const;
  // Select and scroll to a path after create/rename. Expands ancestors first so a
  // fresh entry in a never-expanded folder resolves to a valid index.
  void setCurrentPath(QString path);
  void setOutline(const QVector<OutlineEntry>& entries);
  void setOutlineFoldable(bool foldable);
  void applyTheme(const ThemeDefinition& theme);
  void retranslateUi();

signals:
  void newFileRequested();
  void newWindowRequested();
  void openFolderRequested();
  void fileOpenRequested(QString path);
  // Right-click on the file tree. `path` is the clicked file/dir, or the folder
  // root when the click landed on empty space. `isDir` says which. `onItem` is
  // false for empty space (menu omits per-item actions). Empty path ⇒ no menu.
  void fileTreeContextMenuRequested(QString path, bool isDir, bool onItem, QPoint globalPos);
  void outlineActivated(NodeId nodeId, SourceRange sourceRange);

private:
  void changeEvent(QEvent* event) override;
  void setupFilesPanel();
  void setupOutlinePanel();
  void updateTabButtons();
  void applyStyle();
  QTreeWidgetItem* addOutlineItem(const OutlineEntry& entry, QTreeWidgetItem* parent);
  void emitOutlineItem(QTreeWidgetItem* item);

  QToolButton* filesTabButton_ = nullptr;
  QToolButton* outlineTabButton_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QWidget* filesPanel_ = nullptr;
  QWidget* outlinePanel_ = nullptr;
  QToolButton* newFileButton_ = nullptr;
  QFileSystemModel* fileModel_ = nullptr;
  QTreeView* fileTree_ = nullptr;
  QTreeWidget* outlineTree_ = nullptr;
  QLabel* outlineEmptyLabel_ = nullptr;
  Panel panel_ = Panel::Files;
  QString currentFilePath_;
  QString folderRoot_;
  bool outlineFoldable_ = false;
  ThemeDefinition currentTheme_;
  QVector<OutlineEntry> lastOutlineEntries_;
};

}  // namespace muffin
