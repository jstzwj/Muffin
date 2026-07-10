#include "app/OutlineModel.h"

#include <QSize>

#include <utility>

namespace muffin {

OutlineModel::OutlineModel(QObject* parent) : QAbstractItemModel(parent) {}

QModelIndex OutlineModel::index(int row, int column, const QModelIndex& parentIndex) const {
  if (column != 0 || row < 0 ||
      (parentIndex.isValid() && (parentIndex.model() != this || parentIndex.column() != 0))) {
    return {};
  }
  const int parentEntry = foldable_ && parentIndex.isValid() ? entryIndex(parentIndex) : -1;
  const int child = childAt(parentEntry, row);
  return child >= 0 ? createIndex(row, column, quintptr(child + 1)) : QModelIndex();
}

QModelIndex OutlineModel::parent(const QModelIndex& childIndex) const {
  if (!foldable_) {
    return {};
  }
  const int child = entryIndex(childIndex);
  if (child < 0) {
    return {};
  }
  const int parentEntry = entries_.at(child).parentIndex;
  if (parentEntry < 0 || parentEntry >= entries_.size()) {
    return {};
  }
  return createIndex(rowInParent_.at(parentEntry), 0, quintptr(parentEntry + 1));
}

int OutlineModel::rowCount(const QModelIndex& parentIndex) const {
  if ((parentIndex.isValid() && parentIndex.model() != this) || parentIndex.column() > 0) {
    return 0;
  }
  if (!foldable_) {
    return parentIndex.isValid() ? 0 : entries_.size();
  }
  if (!parentIndex.isValid()) {
    return roots_.size();
  }
  const int parentEntry = entryIndex(parentIndex);
  return parentEntry >= 0 ? childCount_.at(parentEntry) : 0;
}

int OutlineModel::columnCount(const QModelIndex&) const {
  return 1;
}

QVariant OutlineModel::data(const QModelIndex& modelIndex, int role) const {
  const OutlineEntry* outlineEntry = entry(modelIndex);
  if (!outlineEntry) {
    return {};
  }
  if (role == Qt::DisplayRole) {
    return foldable_
        ? outlineEntry->title
        : QString(qMax(0, outlineEntry->level - 1) * 2, QChar(0x2002)) + outlineEntry->title;
  }
  if (role == Qt::TextAlignmentRole) {
    return int(Qt::AlignVCenter | Qt::AlignLeft);
  }
  if (role == Qt::SizeHintRole) {
    return QSize(0, 22);
  }
  return {};
}

Qt::ItemFlags OutlineModel::flags(const QModelIndex& modelIndex) const {
  return modelIndex.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

void OutlineModel::setEntries(QVector<OutlineEntry> entries) {
  beginResetModel();
  entries_ = std::move(entries);
  rebuildLinks();
  endResetModel();
}

void OutlineModel::clear() {
  if (entries_.isEmpty()) {
    return;
  }
  setEntries({});
}

bool OutlineModel::isEmpty() const {
  return entries_.isEmpty();
}

void OutlineModel::setFoldable(bool foldable) {
  if (foldable_ == foldable) {
    return;
  }
  beginResetModel();
  foldable_ = foldable;
  endResetModel();
}

bool OutlineModel::isFoldable() const {
  return foldable_;
}

const OutlineEntry* OutlineModel::entry(const QModelIndex& modelIndex) const {
  const int i = entryIndex(modelIndex);
  return i >= 0 ? &entries_.at(i) : nullptr;
}

int OutlineModel::entryIndex(const QModelIndex& modelIndex) const {
  if (!modelIndex.isValid() || modelIndex.model() != this) {
    return -1;
  }
  const quintptr stored = modelIndex.internalId();
  if (stored == 0 || stored - 1 >= quintptr(entries_.size())) {
    return -1;
  }
  return int(stored - 1);
}

int OutlineModel::childAt(int parentEntry, int row) const {
  if (row < 0) {
    return -1;
  }
  if (!foldable_) {
    return parentEntry < 0 && row < entries_.size() ? row : -1;
  }
  if (parentEntry < 0) {
    return row < roots_.size() ? roots_.at(row) : -1;
  }
  if (parentEntry >= entries_.size() || row >= childCount_.at(parentEntry)) {
    return -1;
  }
  int child = firstChild_.at(parentEntry);
  for (int i = 0; i < row && child >= 0; ++i) {
    child = nextSibling_.at(child);
  }
  return child;
}

void OutlineModel::rebuildLinks() {
  firstChild_.fill(-1, entries_.size());
  nextSibling_.fill(-1, entries_.size());
  childCount_.fill(0, entries_.size());
  rowInParent_.fill(0, entries_.size());
  roots_.clear();
  roots_.reserve(entries_.size());
  QVector<int> lastChild(entries_.size(), -1);

  for (int i = 0; i < entries_.size(); ++i) {
    int parentEntry = entries_.at(i).parentIndex;
    if (parentEntry < 0 || parentEntry >= i) {
      roots_.push_back(i);
      rowInParent_[i] = roots_.size() - 1;
      continue;
    }
    rowInParent_[i] = childCount_[parentEntry]++;
    if (firstChild_[parentEntry] < 0) {
      firstChild_[parentEntry] = i;
    } else {
      nextSibling_[lastChild[parentEntry]] = i;
    }
    lastChild[parentEntry] = i;
  }
}

}  // namespace muffin
