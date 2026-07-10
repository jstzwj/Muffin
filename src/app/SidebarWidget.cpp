#include "app/SidebarWidget.h"
#include "app/OutlineModel.h"

#include "theme/ChromeStyleSheet.h"
#include "theme/ThemeDefinition.h"

#include <QAbstractItemView>
#include <QDir>
#include <QScrollBar>
#include <QSettings>
#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSize>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include "io/MuffinMime.h"

#include <functional>
#include <utility>

namespace {

QToolButton* createFlatButton(const QString& text, QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setText(text);
  button->setAutoRaise(true);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

// QFileSystemModel whose drag mime data carries the kMuffinFileTreeDragMime marker in
// addition to the standard text/uri-list URLs. Drop targets in the editor check for that
// marker to route an in-app file-tree drop as "insert a markdown link" instead of the
// external-drop behaviour (open as document / open as folder). No Q_OBJECT: the model only
// overrides a virtual and adds no signals/slots of its own.
class FileTreeModel final : public QFileSystemModel {
 public:
  using QFileSystemModel::QFileSystemModel;

 protected:
  QMimeData* mimeData(const QModelIndexList& indexes) const override {
    QMimeData* data = QFileSystemModel::mimeData(indexes);
    if (data) {
      data->setData(muffin::kMuffinFileTreeDragMime, QByteArray());
    }
    return data;
  }
};

// Inline filename editor: a QLineEdit shown as a child of the file-tree viewport, positioned
// over the row being renamed / created. It bypasses QAbstractItemView's edit machinery entirely
// (no dependence on ItemIsEditable / editTriggers / the commitData→closeEditor chain), so opening
// it from a context-menu action can't be silently no-op'd by the model's flags and can't be
// auto-closed by the menu's teardown focus events. focusOutEvent is the gate the QAbstractItemView
// chain lacks: it discriminates the focus reason — only a genuine mouse/tab navigation commits;
// popup (menu/tooltip) and window-activation focus losses re-grab focus so the editor stays open.
// Owns no FS logic; SidebarWidget wires validateFn / commitFn / cancelFn, forwarding to MainWindow
// (the file-ops owner). No Q_OBJECT: only virtual overrides + std::function members.
class FileNameEdit final : public QLineEdit {
 public:
  explicit FileNameEdit(QWidget* parent) : QLineEdit(parent) {}

  std::function<muffin::InlineValidation(muffin::InlineEditContext, QString)> validateFn;
  std::function<void(muffin::InlineEditContext, QString)> commitFn;
  std::function<void(muffin::InlineEditContext)> cancelFn;

  void setContext(muffin::InlineEditContext ctx) { ctx_ = std::move(ctx); }

  // Windows-Explorer style: select the basename, leave the extension (if any) unselected so a
  // retype keeps it. Directories and leading-dot / extension-less names select all.
  void selectBasename() {
    const QString name = text();
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
      setSelection(0, dot);
    } else {
      selectAll();
    }
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    const int key = event->key();
    if (key == Qt::Key_Enter || key == Qt::Key_Return) {
      // tryCommit commits+closes on Valid/NoChange (or cancels+closes on Empty-of-a-pending-create);
      // on Duplicate / Empty-rename it shows the inline error and we keep the editor open.
      if (tryCommit()) {
        closeSelf();
      }
      return;  // swallow Enter either way — never let it bubble to a parent handler
    }
    if (key == Qt::Key_Escape) {
      if (cancelFn) {
        cancelFn(ctx_);
      }
      closeSelf();
      return;
    }
    QLineEdit::keyPressEvent(event);
  }

  void focusOutEvent(QFocusEvent* event) override {
    QLineEdit::focusOutEvent(event);
    const Qt::FocusReason reason = event->reason();
    // Popup (context-menu teardown, tooltip) and window-activation focus changes are NOT user
    // commits — re-grab focus so the editor survives the menu closing. (This is the case the
    // QAbstractItemView edit chain cannot handle: its FocusOut unconditionally commits+ closes.)
    const bool transient = reason == Qt::PopupFocusReason || reason == Qt::ActiveWindowFocusReason;
    if (transient || !tryCommit()) {
      QTimer::singleShot(0, this, [this] {
        if (isVisible()) {
          setFocus(Qt::OtherFocusReason);
          selectBasename();
        }
      });
      return;
    }
    closeSelf();
  }

 private:
  // Validate + decide. Returns true if the editor should close (a valid commit, a no-change, or
  // an empty name on a pending create which cancels). Returns false (after showing the inline
  // error) for Duplicate or Empty-on-rename, keeping the editor open.
  bool tryCommit() {
    const QString name = text().trimmed();
    const muffin::InlineValidation v = validateFn ? validateFn(ctx_, name) : muffin::InlineValidation{};
    if (v.kind == muffin::InlineValidation::Valid || v.kind == muffin::InlineValidation::NoChange) {
      if (commitFn) {
        commitFn(ctx_, name);
      }
      return true;
    }
    if (v.kind == muffin::InlineValidation::Empty && ctx_.pendingCreate) {
      if (cancelFn) {
        cancelFn(ctx_);
      }
      return true;
    }
    showInlineError(v);
    return false;
  }

  void showInlineError(const muffin::InlineValidation& v) const {
    if (v.errorText.isEmpty()) {
      return;
    }
    const QPoint pos = mapToGlobal(QPoint(width() / 2, height()));
    QToolTip::showText(pos, v.errorText, const_cast<FileNameEdit*>(this));
  }

  void closeSelf() {
    QToolTip::hideText();
    deleteLater();  // owner's QPointer<QWidget> auto-nulls on destruction
  }

  muffin::InlineEditContext ctx_;
};

}  // namespace

muffin::SidebarWidget::SidebarWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("MuffinSidebar"));
  setMinimumWidth(220);
  setMaximumWidth(360);

  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto* tabLayout = new QHBoxLayout();
  tabLayout->setContentsMargins(18, 12, 18, 0);
  tabLayout->setSpacing(24);
  filesTabButton_ = createFlatButton(QString(), this);
  outlineTabButton_ = createFlatButton(QString(), this);
  filesTabButton_->setCheckable(true);
  outlineTabButton_->setCheckable(true);
  tabLayout->addStretch(1);
  tabLayout->addWidget(filesTabButton_);
  tabLayout->addWidget(outlineTabButton_);
  tabLayout->addStretch(1);
  rootLayout->addLayout(tabLayout);

  stack_ = new QStackedWidget(this);
  rootLayout->addWidget(stack_, 1);

  setupFilesPanel();
  setupOutlinePanel();
  retranslateUi();
  applyTheme(ThemeDefinition::builtIn(QStringLiteral("github")).value());
  setPanel(Panel::Files);

  connect(filesTabButton_, &QToolButton::clicked, this, [this] { setPanel(Panel::Files); });
  connect(outlineTabButton_, &QToolButton::clicked, this, [this] { setPanel(Panel::Outline); });
}

void muffin::SidebarWidget::setupFilesPanel() {
  filesPanel_ = new QWidget(this);
  auto* layout = new QVBoxLayout(filesPanel_);
  layout->setContentsMargins(0, 12, 0, 0);
  layout->setSpacing(0);

  fileModel_ = new FileTreeModel(this);
  fileModel_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

  fileTree_ = new QTreeView(filesPanel_);
  fileTree_->setObjectName(QStringLiteral("FileTree"));
  fileTree_->setModel(fileModel_);
  fileTree_->setHeaderHidden(true);
  fileTree_->setAnimated(false);
  fileTree_->setRootIsDecorated(true);
  fileTree_->setIndentation(14);
  fileTree_->setMouseTracking(true);
  fileTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fileTree_->setSelectionMode(QAbstractItemView::SingleSelection);
  fileTree_->setContextMenuPolicy(Qt::CustomContextMenu);
  // Allow dragging file/folder rows out of the tree onto the editor (DragOnly: the tree is a
  // drag source, never a drop target). FileTreeModel tags the drag with kMuffinFileTreeDragMime
  // so the editor inserts a markdown link instead of treating it as an external file:// drop.
  fileTree_->setDragEnabled(true);
  fileTree_->setDragDropMode(QAbstractItemView::DragOnly);
  fileTree_->setDefaultDropAction(Qt::CopyAction);
  // Inline rename / new-file / new-folder editing is done with a FileNameEdit overlay (a child
  // QLineEdit of this viewport), NOT via QAbstractItemView::edit — see showInlineEditor. The
  // tree itself stays non-editable; the overlay is opened manually on the target row.
  // Safety net: if the model resets mid-edit (e.g. the user opens another folder while an inline
  // editor is open), the editing row vanishes and a pending-create temp would be stranded on
  // disk — close the editor and cancel any pending create so MainWindow deletes it.
  connect(fileModel_, &QFileSystemModel::modelReset, this, [this]() {
    if (inlineEditor_) {
      inlineEditor_->deleteLater();
    }
    editingIndex_ = QPersistentModelIndex();
    if (pendingCreatePaths_.isEmpty()) {
      return;
    }
    for (const QString& path : std::as_const(pendingCreatePaths_)) {
      muffin::InlineEditContext ctx;
      ctx.oldPath = path;
      ctx.pendingCreate = true;
      emit inlineCancelRequested(ctx);
    }
    pendingCreatePaths_.clear();
  });
  for (int column = 1; column < fileModel_->columnCount(); ++column) {
    fileTree_->hideColumn(column);
  }
  layout->addWidget(fileTree_, 1);

  connect(fileTree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
    const QString path = fileModel_->filePath(index);
    if (QFileInfo(path).isDir()) {
      fileTree_->setExpanded(index, !fileTree_->isExpanded(index));
      return;
    }
    if (QFileInfo(path).isFile()) {
      emit fileOpenRequested(path);
    }
  });
  // Right-click: move the selection to the clicked row (standard file-manager
  // behavior) then hand the context off to MainWindow, which builds the menu.
  // A click on empty space targets the folder root instead.
  connect(fileTree_, &QTreeView::customContextMenuRequested, this, [this](const QPoint& pos) {
    const QModelIndex index = fileTree_->indexAt(pos);
    QString path;
    bool isDir = true;
    bool onItem = false;
    if (index.isValid()) {
      path = fileModel_->filePath(index);
      isDir = QFileInfo(path).isDir();
      onItem = true;
      fileTree_->setCurrentIndex(index);
    } else {
      path = folderRoot_;
      onItem = false;
    }
    if (path.isEmpty()) {
      return;  // no folder open and no item: nothing to act on
    }
    emit fileTreeContextMenuRequested(path, isDir, onItem, fileTree_->viewport()->mapToGlobal(pos));
  });

  stack_->addWidget(filesPanel_);
}

void muffin::SidebarWidget::setupOutlinePanel() {
  // files/outlineFoldable: when on, the outline is a real collapsible tree
  // (indentation + expand arrows); when off it is a flat list with text-only
  // indentation (the original behavior). Read once at construction; toggling the
  // preference later routes through setOutlineFoldable().
  outlineFoldable_ = QSettings().value(QStringLiteral("files/outlineFoldable"), false).toBool();

  outlinePanel_ = new QWidget(this);
  auto* layout = new QVBoxLayout(outlinePanel_);
  layout->setContentsMargins(0, 12, 0, 0);
  layout->setSpacing(0);

  outlineEmptyLabel_ = new QLabel(outlinePanel_);
  outlineEmptyLabel_->setObjectName(QStringLiteral("OutlineEmptyLabel"));
  outlineEmptyLabel_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

  outlineModel_ = new muffin::OutlineModel(this);
  outlineModel_->setFoldable(outlineFoldable_);
  outlineTree_ = new QTreeView(outlinePanel_);
  outlineTree_->setObjectName(QStringLiteral("OutlineTree"));
  outlineTree_->setModel(outlineModel_);
  outlineTree_->setHeaderHidden(true);
  outlineTree_->setUniformRowHeights(true);
  outlineTree_->setIndentation(outlineFoldable_ ? 14 : 0);
  outlineTree_->setRootIsDecorated(outlineFoldable_);
  outlineTree_->setItemsExpandable(outlineFoldable_);
  // Double-click navigates (emitOutlineItem); folding uses the expand arrow.
  outlineTree_->setExpandsOnDoubleClick(false);
  outlineTree_->setAnimated(false);
  outlineTree_->setMouseTracking(true);
  outlineTree_->viewport()->setCursor(Qt::PointingHandCursor);
  outlineTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  outlineTree_->setSelectionMode(QAbstractItemView::SingleSelection);

  layout->addWidget(outlineEmptyLabel_, 1);
  layout->addWidget(outlineTree_, 1);
  outlineTree_->hide();

  connect(outlineTree_, &QTreeView::clicked, this, &muffin::SidebarWidget::emitOutlineItem);

  stack_->addWidget(outlinePanel_);
}

void muffin::SidebarWidget::setPanel(Panel panel) {
  const bool changed = panel_ != panel;
  panel_ = panel;
  stack_->setCurrentWidget(panel == Panel::Files ? filesPanel_ : outlinePanel_);
  updateTabButtons();
  if (changed) {
    emit panelChanged(panel);
  }
}

muffin::SidebarWidget::Panel muffin::SidebarWidget::panel() const {
  return panel_;
}

void muffin::SidebarWidget::setCurrentDocument(QString displayName, QString filePath, bool modified) {
  Q_UNUSED(displayName);
  Q_UNUSED(modified);
  currentFilePath_ = std::move(filePath);
  if (folderRoot_.isEmpty() && !currentFilePath_.isEmpty()) {
    setFolderRoot(QFileInfo(currentFilePath_).absolutePath());
  }
  if (!currentFilePath_.isEmpty()) {
    const QModelIndex index = fileModel_->index(currentFilePath_);
    if (index.isValid()) {
      fileTree_->setCurrentIndex(index);
      fileTree_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
  }
}

void muffin::SidebarWidget::setFolderRoot(QString path) {
  folderRoot_ = std::move(path);
  if (folderRoot_.isEmpty()) {
    fileTree_->setRootIndex({});
    return;
  }
  const QModelIndex rootIndex = fileModel_->setRootPath(folderRoot_);
  fileTree_->setRootIndex(rootIndex);
  if (!currentFilePath_.isEmpty()) {
    const QModelIndex currentIndex = fileModel_->index(currentFilePath_);
    if (currentIndex.isValid()) {
      fileTree_->setCurrentIndex(currentIndex);
    }
  }
}

QString muffin::SidebarWidget::folderRoot() const {
  return folderRoot_;
}

void muffin::SidebarWidget::setCurrentPath(QString path) {
  if (path.isEmpty()) {
    return;
  }
  // A freshly-created entry in a folder the model never listed won't have an
  // index until that parent's children are fetched, so expand/fetch every
  // ancestor first (the QFileSystemModel populates lazily per directory).
  QModelIndex index = fileModel_->index(path);
  if (!index.isValid()) {
    QModelIndex ancestor = fileModel_->index(QFileInfo(path).absolutePath());
    while (ancestor.isValid()) {
      fileTree_->expand(ancestor);
      fileModel_->fetchMore(ancestor);
      ancestor = ancestor.parent();
    }
    index = fileModel_->index(path);
  }
  if (index.isValid()) {
    fileTree_->setCurrentIndex(index);
    fileTree_->scrollTo(index, QAbstractItemView::PositionAtCenter);
  }
}

void muffin::SidebarWidget::beginInlineRename(QString path) {
  if (path.isEmpty() || !fileTree_ || !fileModel_) {
    return;
  }
  setCurrentPath(path);  // expands ancestors + selects + scrolls to the row
  const QModelIndex idx = fileModel_->index(path);
  if (!idx.isValid() || !QFileInfo(path).isWritable()) {
    return;  // unresolvable or read-only: no inline editor (silent no-op)
  }
  showInlineEditor(idx);
}

void muffin::SidebarWidget::beginInlineCreate(QString tempPath) {
  if (tempPath.isEmpty() || !fileTree_ || !fileModel_) {
    return;
  }
  pendingCreatePaths_.insert(tempPath);
  const QString parentDir = QFileInfo(tempPath).absolutePath();
  const QModelIndex parentIdx = fileModel_->index(parentDir);
  if (parentIdx.isValid()) {
    fileTree_->expand(parentIdx);
  }
  // The temp entry was just created on disk; QFileSystemModel refreshes async via its worker,
  // so index(tempPath) is usually invalid until directoryLoaded fires for the parent. Resolve
  // immediately if possible; otherwise wait for directoryLoaded (one-shot) with a short timer
  // fallback. Both paths route through the idempotent resolveAndEdit.
  if (fileModel_->index(tempPath).isValid()) {
    resolveAndEdit(tempPath);
    return;
  }
  if (directoryLoadedConn_) {
    QObject::disconnect(directoryLoadedConn_);
  }
  directoryLoadedConn_ = QObject::connect(
      fileModel_, &QFileSystemModel::directoryLoaded, this,
      [this, tempPath, parentDir](const QString& loadedDir) {
        if (loadedDir != parentDir) {
          return;
        }
        if (directoryLoadedConn_) {
          QObject::disconnect(directoryLoadedConn_);
          directoryLoadedConn_ = QMetaObject::Connection{};
        }
        resolveAndEdit(tempPath);
      });
  QTimer::singleShot(150, this, [this, tempPath] { resolveAndEdit(tempPath); });
}

void muffin::SidebarWidget::resolveAndEdit(QString path) {
  if (!fileTree_ || !fileModel_) {
    return;
  }
  // Idempotent: directoryLoaded and the fallback timer can both fire.
  if (inlineEditor_) {
    return;  // an editor is already open
  }
  const QModelIndex idx = fileModel_->index(path);
  if (!idx.isValid()) {
    return;  // entry gone (cancelled / deleted) before it could be resolved
  }
  showInlineEditor(idx);
}

void muffin::SidebarWidget::showInlineEditor(QModelIndex idx) {
  if (!fileTree_ || !fileModel_ || !idx.isValid()) {
    return;
  }
  // One editor at a time: discard any still-open editor from a prior gesture.
  if (inlineEditor_) {
    inlineEditor_->deleteLater();
  }
  editingIndex_ = idx;
  fileTree_->setCurrentIndex(idx);
  fileTree_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
  // Defer creation to the next event-loop iteration so it happens AFTER any active context-menu
  // exec() loop has fully torn down — the menu's focus events then land before the editor exists
  // (FileNameEdit::focusOutEvent also ignores PopupFocusReason as belt-and-braces). The
  // QPersistentModelIndex survives the model refresh the file watcher may emit in the meantime.
  const QPersistentModelIndex persistent(idx);
  QTimer::singleShot(0, fileTree_, [this, persistent]() {
    if (!fileTree_ || !fileModel_) {
      return;
    }
    const QModelIndex i = persistent;
    if (!i.isValid()) {
      return;
    }
    const QRect rowRect = fileTree_->visualRect(i);
    if (rowRect.isNull()) {
      return;  // row not laid out / scrolled out of view
    }
    auto* editor = new FileNameEdit(fileTree_->viewport());
    inlineEditor_ = editor;
    editor->setContext(contextForIndex(i));
    editor->setText(fileModel_->fileName(i));
    editor->setGeometry(rowRect);
    editor->selectBasename();
    editor->validateFn = [this](muffin::InlineEditContext ctx, const QString& name) {
      muffin::InlineValidation out;
      emit inlineValidateRequested(ctx, name, &out);
      return out;
    };
    editor->commitFn = [this](muffin::InlineEditContext ctx, const QString& name) {
      forgetPendingCreate(ctx.oldPath);
      emit inlineCommitRequested(ctx, name);
    };
    editor->cancelFn = [this](muffin::InlineEditContext ctx) {
      forgetPendingCreate(ctx.oldPath);
      emit inlineCancelRequested(ctx);
    };
    connect(editor, &QObject::destroyed, this, [this] { editingIndex_ = QPersistentModelIndex(); });
    // Keep the editor glued to its row while the tree scrolls (otherwise the viewport scrolls
    // under the static child widget). Auto-disconnects when `editor` is destroyed.
    if (auto* bar = fileTree_->verticalScrollBar()) {
      connect(bar, &QAbstractSlider::valueChanged, editor, [this, editor](int) {
        if (!inlineEditor_ || !editingIndex_.isValid()) {
          return;
        }
        const QRect rect = fileTree_->visualRect(QModelIndex(editingIndex_));
        if (!rect.isNull()) {
          editor->setGeometry(rect);
        }
      });
    }
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
  });
}

muffin::InlineEditContext muffin::SidebarWidget::contextForIndex(const QModelIndex& index) const {
  muffin::InlineEditContext ctx;
  if (!fileModel_ || !index.isValid()) {
    return ctx;
  }
  ctx.oldPath = fileModel_->filePath(index);
  ctx.isFolder = fileModel_->isDir(index);
  ctx.pendingCreate = pendingCreatePaths_.contains(ctx.oldPath);
  return ctx;
}

void muffin::SidebarWidget::forgetPendingCreate(const QString& path) {
  pendingCreatePaths_.remove(path);
}

void muffin::SidebarWidget::setOutline(QVector<OutlineEntry> entries) {
  const bool empty = entries.isEmpty();
  outlineModel_->setEntries(std::move(entries));
  if (outlineFoldable_) {
    outlineTree_->expandAll();
  }
  outlineEmptyLabel_->setVisible(empty);
  outlineTree_->setVisible(!empty);
}

void muffin::SidebarWidget::clearOutline() {
  outlineModel_->clear();
  outlineTree_->hide();
  outlineEmptyLabel_->show();
}

void muffin::SidebarWidget::setOutlineFoldable(bool foldable) {
  if (outlineFoldable_ == foldable) {
    return;
  }
  outlineFoldable_ = foldable;
  outlineTree_->setIndentation(foldable ? 14 : 0);
  outlineTree_->setRootIsDecorated(foldable);
  outlineTree_->setItemsExpandable(foldable);
  outlineModel_->setFoldable(foldable);
  applyStyle();                     // toggle the expand-arrow rule
  if (foldable) {
    outlineTree_->expandAll();
  }
}

void muffin::SidebarWidget::applyTheme(const ThemeDefinition& theme) {
  currentTheme_ = theme;
  applyStyle();
}

void muffin::SidebarWidget::retranslateUi() {
  if (filesTabButton_) {
    filesTabButton_->setText(tr("Files"));
  }
  if (outlineTabButton_) {
    outlineTabButton_->setText(tr("Outline"));
  }
  if (outlineEmptyLabel_) {
    outlineEmptyLabel_->setText(tr("No Headings"));
  }
}

void muffin::SidebarWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QWidget::changeEvent(event);
}

void muffin::SidebarWidget::updateTabButtons() {
  filesTabButton_->setChecked(panel_ == Panel::Files);
  outlineTabButton_->setChecked(panel_ == Panel::Outline);
}

void muffin::SidebarWidget::applyStyle() {
  setStyleSheet(sidebarStyleSheet(currentTheme_, outlineFoldable_));
}

void muffin::SidebarWidget::emitOutlineItem(const QModelIndex& index) {
  const OutlineEntry* entry = outlineModel_->entry(index);
  if (!entry) {
    return;
  }
  emit outlineActivated(entry->nodeId, entry->sourceRange);
}
