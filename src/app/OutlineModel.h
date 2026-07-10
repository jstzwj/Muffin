#pragma once

#include "document/OutlineBuilder.h"

#include <QAbstractItemModel>
#include <QVector>

namespace muffin {

// Compact outline model. Entries stay in document order and hierarchy is represented by integer
// links, avoiding one heap-allocated QTreeWidgetItem plus several QVariants per heading.
class OutlineModel final : public QAbstractItemModel {
public:
  explicit OutlineModel(QObject* parent = nullptr);

  QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

  void setEntries(QVector<OutlineEntry> entries);
  void clear();
  bool isEmpty() const;
  void setFoldable(bool foldable);
  bool isFoldable() const;
  const OutlineEntry* entry(const QModelIndex& index) const;

private:
  int entryIndex(const QModelIndex& index) const;
  int childAt(int parentEntry, int row) const;
  void rebuildLinks();

  QVector<OutlineEntry> entries_;
  QVector<int> firstChild_;
  QVector<int> nextSibling_;
  QVector<int> childCount_;
  QVector<int> rowInParent_;
  QVector<int> roots_;
  bool foldable_ = false;
};

}  // namespace muffin
