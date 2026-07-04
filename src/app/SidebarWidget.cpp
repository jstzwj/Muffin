#include "app/SidebarWidget.h"

#include "theme/ChromeStyleSheet.h"
#include "theme/ThemeDefinition.h"

#include <QAbstractItemView>
#include <QDir>
#include <QSettings>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QSize>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "io/MuffinMime.h"

#include <utility>

namespace {

enum OutlineRole {
  NodeIdRole = Qt::UserRole + 1,
  SourceStartRole,
  SourceEndRole,
  LineStartRole,
  LineEndRole,
  ColumnStartRole,
  ColumnEndRole
};

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
  for (int column = 1; column < fileModel_->columnCount(); ++column) {
    fileTree_->hideColumn(column);
  }
  layout->addWidget(fileTree_, 1);

  auto* footerLayout = new QHBoxLayout();
  footerLayout->setContentsMargins(0, 0, 0, 0);
  footerLayout->setSpacing(0);
  newFileButton_ = createFlatButton(QStringLiteral("+"), filesPanel_);
  newFileButton_->setObjectName(QStringLiteral("SidebarNewFileButton"));
  footerLayout->addWidget(newFileButton_);
  footerLayout->addStretch(1);
  layout->addLayout(footerLayout);

  connect(newFileButton_, &QToolButton::clicked, this, &SidebarWidget::newFileRequested);
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

  outlineTree_ = new QTreeWidget(outlinePanel_);
  outlineTree_->setObjectName(QStringLiteral("OutlineTree"));
  outlineTree_->setHeaderHidden(true);
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

  connect(outlineTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
    const QList<QTreeWidgetItem*> allItems = outlineTree_->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
    for (QTreeWidgetItem* current : allItems) {
      QFont font = current->font(0);
      font.setBold(current == item);
      current->setFont(0, font);
    }
    emitOutlineItem(item);
  });

  stack_->addWidget(outlinePanel_);
}

void muffin::SidebarWidget::setPanel(Panel panel) {
  panel_ = panel;
  stack_->setCurrentWidget(panel == Panel::Files ? filesPanel_ : outlinePanel_);
  updateTabButtons();
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

void muffin::SidebarWidget::setOutline(const QVector<OutlineEntry>& entries) {
  lastOutlineEntries_ = entries;
  outlineTree_->clear();
  QVector<QTreeWidgetItem*> items;
  items.reserve(entries.size());

  for (const OutlineEntry& entry : entries) {
    QTreeWidgetItem* parent = entry.parentIndex >= 0 && entry.parentIndex < items.size() ? items[entry.parentIndex] : nullptr;
    items.push_back(addOutlineItem(entry, parent));
  }

  outlineTree_->expandAll();
  const bool empty = entries.isEmpty();
  outlineEmptyLabel_->setVisible(empty);
  outlineTree_->setVisible(!empty);
}

void muffin::SidebarWidget::setOutlineFoldable(bool foldable) {
  if (outlineFoldable_ == foldable) {
    return;
  }
  outlineFoldable_ = foldable;
  outlineTree_->setIndentation(foldable ? 14 : 0);
  outlineTree_->setRootIsDecorated(foldable);
  outlineTree_->setItemsExpandable(foldable);
  applyStyle();                     // toggle the expand-arrow rule
  setOutline(lastOutlineEntries_);  // rebuild items (prefix logic depends on foldable)
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
  if (newFileButton_) {
    newFileButton_->setToolTip(tr("New File"));
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

QTreeWidgetItem* muffin::SidebarWidget::addOutlineItem(const OutlineEntry& entry, QTreeWidgetItem* parent) {
  auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(outlineTree_);
  item->setText(0, entry.title);
  item->setData(0, NodeIdRole, entry.nodeId.toString());
  item->setData(0, SourceStartRole, static_cast<qlonglong>(entry.sourceRange.byteStart));
  item->setData(0, SourceEndRole, static_cast<qlonglong>(entry.sourceRange.byteEnd));
  item->setData(0, LineStartRole, entry.sourceRange.lineStart);
  item->setData(0, LineEndRole, entry.sourceRange.lineEnd);
  item->setData(0, ColumnStartRole, entry.sourceRange.columnStart);
  item->setData(0, ColumnEndRole, entry.sourceRange.columnEnd);
  item->setTextAlignment(0, Qt::AlignVCenter | Qt::AlignLeft);
  item->setSizeHint(0, QSize(0, 22));
  // Flat mode fakes hierarchy with leading spaces; foldable mode relies on the
  // tree's own indentation, so no prefix.
  const QString title = outlineFoldable_
      ? entry.title
      : QString(qMax(0, entry.level - 1) * 2, QChar(0x2002)) + entry.title;
  item->setText(0, title);
  return item;
}

void muffin::SidebarWidget::emitOutlineItem(QTreeWidgetItem* item) {
  if (!item) {
    return;
  }
  SourceRange range;
  range.byteStart = item->data(0, SourceStartRole).toLongLong();
  range.byteEnd = item->data(0, SourceEndRole).toLongLong();
  range.lineStart = item->data(0, LineStartRole).toInt();
  range.lineEnd = item->data(0, LineEndRole).toInt();
  range.columnStart = item->data(0, ColumnStartRole).toInt();
  range.columnEnd = item->data(0, ColumnEndRole).toInt();
  emit outlineActivated(NodeId::fromString(item->data(0, NodeIdRole).toString()), range);
}
