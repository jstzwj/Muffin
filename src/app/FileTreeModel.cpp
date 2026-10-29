#include "app/FileTreeModel.h"

#include "io/MuffinMime.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QMimeData>

namespace {
class FileSourceModel final : public QFileSystemModel {
public:
  using QFileSystemModel::QFileSystemModel;

protected:
  QMimeData* mimeData(const QModelIndexList& indexes) const override {
    auto* data = QFileSystemModel::mimeData(indexes);
    if (data)
      data->setData(muffin::kMuffinFileTreeDragMime, {});
    return data;
  }
};

template <typename T> int compare(const T& a, const T& b) {
  return a < b ? -1 : b < a ? 1 : 0;
}
} // namespace

muffin::FileTreeModel::FileTreeModel(QObject* parent) : QSortFilterProxyModel(parent) {
  // The C-locale collator ignores numericMode, even with the ICU backend.
  if (collator_.locale().language() == QLocale::C)
    collator_.setLocale(QLocale(QLocale::English));
  collator_.setNumericMode(true);
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(true);
  replaceSource();
  sort(0, Qt::AscendingOrder);
}

void muffin::FileTreeModel::replaceSource() {
  auto* previous = files_;
  loadedDirectories_.clear();
  files_ = new FileSourceModel(this);
  files_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
  connect(files_, &QFileSystemModel::directoryLoaded, this, [this](const QString& path) {
    loadedDirectories_.insert(QPersistentModelIndex(files_->index(path)));
    emit directoryLoaded(path);
  });
  setSourceModel(files_);
  if (previous) {
    disconnect(previous, nullptr, this, nullptr);
    previous->deleteLater();
  }
}

QModelIndex muffin::FileTreeModel::index(const QString& path) const {
  return mapFromSource(files_->index(path));
}

QModelIndex muffin::FileTreeModel::setRootPath(const QString& path) {
  return mapFromSource(files_->setRootPath(path));
}

QString muffin::FileTreeModel::filePath(const QModelIndex& index) const {
  return files_->filePath(mapToSource(index));
}

QString muffin::FileTreeModel::fileName(const QModelIndex& index) const {
  return files_->fileName(mapToSource(index));
}

bool muffin::FileTreeModel::isDir(const QModelIndex& index) const {
  return files_->isDir(mapToSource(index));
}

bool muffin::FileTreeModel::isDirectoryLoaded(const QModelIndex& index) const {
  return index.isValid() && loadedDirectories_.contains(QPersistentModelIndex(mapToSource(index)));
}

void muffin::FileTreeModel::setSortBy(SortBy field, Qt::SortOrder order) {
  if (sortBy_ != field) {
    sortBy_ = field;
    invalidate();
  }
  sort(0, order);
}

QModelIndex muffin::FileTreeModel::refresh(const QString& root) {
  replaceSource();
  return setRootPath(root);
}

bool muffin::FileTreeModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  const auto a = files_->fileInfo(left), b = files_->fileInfo(right);
  // QSortFilterProxyModel reverses the comparison in descending order. Keep
  // directories ahead of files in either direction, on every platform.
  if (a.isDir() != b.isDir())
    return sortOrder() == Qt::AscendingOrder ? a.isDir() : b.isDir();
  int result = 0;
  switch (sortBy_) {
  case SortBy::Name:
    break;
  case SortBy::Modified:
    result = compare(a.lastModified(), b.lastModified());
    break;
  case SortBy::Created: {
    const auto createdA = a.birthTime(), createdB = b.birthTime();
    result =
        compare(createdA.isValid() ? createdA : a.lastModified(), createdB.isValid() ? createdB : b.lastModified());
    break;
  }
  case SortBy::Type:
    result = collator_.compare(a.suffix(), b.suffix());
    break;
  case SortBy::Size:
    result = compare(a.size(), b.size());
    break;
  }
  if (result == 0)
    result = collator_.compare(a.fileName(), b.fileName());
  if (result == 0)
    result = QString::compare(a.fileName(), b.fileName());
  return result < 0;
}
