#pragma once

#include "Export.h"

#include <QCollator>
#include <QFileSystemModel>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSortFilterProxyModel>

namespace muffin {

// Path-based facade over the asynchronous filesystem model. View indexes always
// belong to this proxy, including drag, rename and saved expansion state.
class MUFFIN_UI_EXPORT FileTreeModel final : public QSortFilterProxyModel {
  Q_OBJECT
public:
  enum class SortBy { Name, Modified, Created, Type, Size };
  Q_ENUM(SortBy)

  explicit FileTreeModel(QObject* parent = nullptr);
  using QSortFilterProxyModel::index;
  QModelIndex index(const QString& path) const;
  QModelIndex setRootPath(const QString& path);
  QString filePath(const QModelIndex& index) const;
  QString fileName(const QModelIndex& index) const;
  bool isDir(const QModelIndex& index) const;
  bool isDirectoryLoaded(const QModelIndex& index) const;
  void setSortBy(SortBy field, Qt::SortOrder order);
  SortBy sortBy() const { return sortBy_; }
  // Replace stale filesystem caches; consumers restore only saved branches.
  QModelIndex refresh(const QString& root);

signals:
  void directoryLoaded(QString path);

protected:
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  void replaceSource();
  QFileSystemModel* files_ = nullptr;
  QSet<QPersistentModelIndex> loadedDirectories_;
  QCollator collator_;
  SortBy sortBy_ = SortBy::Name;
};

} // namespace muffin
