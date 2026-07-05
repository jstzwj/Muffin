#pragma once

#include "document/OutlineBuilder.h"
#include "theme/ThemeDefinition.h"

#include <QMetaObject>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QFileSystemModel;
class QLabel;
class QStackedWidget;
class QToolButton;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

namespace muffin {

// Context for an in-progress inline edit of a file-tree row. `pendingCreate` marks a temp
// entry the MainWindow created on disk so the tree shows a row to rename; on cancel/empty
// the temp is deleted. `isFolder` distinguishes folder vs file (folders don't get ".md").
struct InlineEditContext {
  QString oldPath;
  bool pendingCreate = false;
  bool isFolder = false;
};

// Result of validating an inline name. `errorText` carries a translated, user-facing message
// for the inline tooltip shown when the editor is kept open (Duplicate / Empty).
struct InlineValidation {
  enum Kind { Valid, NoChange, Empty, Duplicate } kind = Valid;
  QString errorText;
};

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

  // Open an inline rename editor on an existing file/folder row. The filename field becomes
  // editable with the basename selected (Windows-Explorer style); Enter / focus-loss commits,
  // Escape cancels, and a duplicate name keeps the editor open with an inline error tooltip.
  void beginInlineRename(QString path);
  // Open an inline editor on a freshly-created temp entry (a row the caller already made on
  // disk). Whether it is a folder is read from the model (isDir), so the commit handler knows
  // not to normalize ".md" onto a folder. The model refreshes async, so resolution waits for
  // directoryLoaded (with a short timer fallback).
  void beginInlineCreate(QString tempPath);

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
  // Inline-edit lifecycle, driven by the file-tree delegate. Validate is synchronous with an
  // out-param so the delegate can decide commit-vs-keep-open on the same call.
  void inlineValidateRequested(muffin::InlineEditContext ctx, QString name, muffin::InlineValidation* out);
  void inlineCommitRequested(muffin::InlineEditContext ctx, QString name);
  void inlineCancelRequested(muffin::InlineEditContext ctx);

private:
  void changeEvent(QEvent* event) override;
  void setupFilesPanel();
  void setupOutlinePanel();
  void updateTabButtons();
  void applyStyle();
  QTreeWidgetItem* addOutlineItem(const OutlineEntry& entry, QTreeWidgetItem* parent);
  void emitOutlineItem(QTreeWidgetItem* item);
  // Resolve a just-created entry's index (waiting on the QFileSystemModel async refresh if
  // needed) and open the inline editor on it. Idempotent: skips if an editor is already open.
  void resolveAndEdit(QString path);
  // Build the InlineEditContext for an editing index from the model + pendingCreate set.
  InlineEditContext contextForIndex(const QModelIndex& index) const;
  // Clear stale pending-create entries (resolved/cancelled) so ctx lookup stays accurate.
  void forgetPendingCreate(const QString& path);
  // Open the inline filename editor (a QLineEdit child of the tree viewport) over `idx`,
  // deferred to the next event-loop iteration so it is created after any active context-menu
  // exec() loop has torn down. Bypasses QAbstractItemView::edit entirely, so there is no
  // dependence on ItemIsEditable / editTriggers / the commitData→closeEditor chain — focus
  // events from the menu's teardown land before the editor exists, and focusOutEvent
  // discriminates the reason so only a genuine mouse/tab navigation commits.
  void showInlineEditor(QModelIndex idx);

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
  // Inline-edit state: the row being edited (only one editor is open at a time) and the set
  // of temp paths the MainWindow created for a New File / New Folder gesture (so the delegate
  // can tell a "pending create" commit from a plain rename, and cancel can delete the temp).
  QPersistentModelIndex editingIndex_;
  QPointer<QWidget> inlineEditor_;
  QSet<QString> pendingCreatePaths_;
  QMetaObject::Connection directoryLoadedConn_;
};

}  // namespace muffin
