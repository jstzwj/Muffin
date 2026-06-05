#include "app/SidebarWidget.h"

#include <QAbstractItemView>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSize>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace muffin {
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

}  // namespace

SidebarWidget::SidebarWidget(QWidget* parent) : QWidget(parent) {
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
  applyThemeName(QStringLiteral("github"));
  setPanel(Panel::Files);

  connect(filesTabButton_, &QToolButton::clicked, this, [this] { setPanel(Panel::Files); });
  connect(outlineTabButton_, &QToolButton::clicked, this, [this] { setPanel(Panel::Outline); });
}

void SidebarWidget::setupFilesPanel() {
  filesPanel_ = new QWidget(this);
  auto* layout = new QVBoxLayout(filesPanel_);
  layout->setContentsMargins(0, 12, 0, 0);
  layout->setSpacing(0);

  fileModel_ = new QFileSystemModel(this);
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

  stack_->addWidget(filesPanel_);
}

void SidebarWidget::setupOutlinePanel() {
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
  outlineTree_->setIndentation(0);
  outlineTree_->setRootIsDecorated(false);
  outlineTree_->setItemsExpandable(false);
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

void SidebarWidget::setPanel(Panel panel) {
  panel_ = panel;
  stack_->setCurrentWidget(panel == Panel::Files ? filesPanel_ : outlinePanel_);
  updateTabButtons();
}

SidebarWidget::Panel SidebarWidget::panel() const {
  return panel_;
}

void SidebarWidget::setCurrentDocument(QString displayName, QString filePath, bool modified) {
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

void SidebarWidget::setFolderRoot(QString path) {
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

QString SidebarWidget::folderRoot() const {
  return folderRoot_;
}

void SidebarWidget::setOutline(const QVector<OutlineEntry>& entries) {
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

void SidebarWidget::applyThemeName(QString name) {
  applyStyle(name == QStringLiteral("night"));
}

void SidebarWidget::retranslateUi() {
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

void SidebarWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QWidget::changeEvent(event);
}

void SidebarWidget::updateTabButtons() {
  filesTabButton_->setChecked(panel_ == Panel::Files);
  outlineTabButton_->setChecked(panel_ == Panel::Outline);
}

void SidebarWidget::applyStyle(bool night) {
  if (night) {
    setStyleSheet(QStringLiteral(
        "#MuffinSidebar { background:#1f2328; border-right:1px solid #3d444d; }"
        "#MuffinSidebar QToolButton { background:transparent; border:0; color:#9aa4af; padding:5px 4px; }"
        "#MuffinSidebar QToolButton:hover { background:#2b3138; }"
        "#MuffinSidebar QToolButton:checked { color:#e6edf3; border-bottom:3px solid #8b949e; }"
        "#OutlineEmptyLabel { color:#8b949e; }"
        "#FileTree, #OutlineTree { background:#1f2328; color:#e6edf3; border:0; padding:4px 0; outline:0; }"
        "#OutlineTree::branch { image:none; width:0; }"
        "#FileTree::item, #OutlineTree::item { min-height:22px; padding:1px 4px; border:0; }"
        "#FileTree::item:hover, #OutlineTree::item:hover { background:#2b3138; color:#e6edf3; }"
        "#FileTree::item:selected, #OutlineTree::item:selected { background:#30363d; color:#e6edf3; }"
        "#SidebarNewFileButton { min-width:32px; min-height:24px; padding:0; color:#9aa4af; }"));
  } else {
    setStyleSheet(QStringLiteral(
        "#MuffinSidebar { background:#fafafa; border-right:1px solid #eeeeee; }"
        "#MuffinSidebar QToolButton { background:transparent; border:0; color:#666666; padding:5px 4px; }"
        "#MuffinSidebar QToolButton:hover { background:#eeeeee; }"
        "#MuffinSidebar QToolButton:checked { color:#111111; border-bottom:3px solid #333333; }"
        "#OutlineEmptyLabel { color:#777777; }"
        "#FileTree, #OutlineTree { background:#fafafa; color:#222222; border:0; padding:4px 0; outline:0; }"
        "#OutlineTree::branch { image:none; width:0; }"
        "#FileTree::item, #OutlineTree::item { min-height:22px; padding:1px 4px; border:0; }"
        "#FileTree::item:hover, #OutlineTree::item:hover { background:#eeeeee; color:#222222; }"
        "#FileTree::item:selected, #OutlineTree::item:selected { background:#e8e8e8; color:#222222; }"
        "#SidebarNewFileButton { min-width:32px; min-height:24px; padding:0; color:#3574b8; }"));
  }
}

QTreeWidgetItem* SidebarWidget::addOutlineItem(const OutlineEntry& entry, QTreeWidgetItem* parent) {
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
  item->setText(0, QString(qMax(0, entry.level - 1) * 2, QChar(0x2002)) + entry.title);
  return item;
}

void SidebarWidget::emitOutlineItem(QTreeWidgetItem* item) {
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

}  // namespace muffin
