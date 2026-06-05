#pragma once

#include "document/OutlineBuilder.h"

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
  void setOutline(const QVector<OutlineEntry>& entries);
  void applyThemeName(QString name);
  void retranslateUi();

signals:
  void newFileRequested();
  void newWindowRequested();
  void openFolderRequested();
  void fileOpenRequested(QString path);
  void outlineActivated(NodeId nodeId, SourceRange sourceRange);

private:
  void changeEvent(QEvent* event) override;
  void setupFilesPanel();
  void setupOutlinePanel();
  void updateTabButtons();
  void applyStyle(bool night);
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
};

}  // namespace muffin
