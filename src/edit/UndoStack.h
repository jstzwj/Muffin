#pragma once

#include "edit/EditTransaction.h"

#include <QObject>
#include <QElapsedTimer>
#include <QVector>

namespace muffin {

class UndoStack final : public QObject {
  Q_OBJECT

public:
  explicit UndoStack(QObject* parent = nullptr);

  bool canUndo() const;
  bool canRedo() const;
  QString undoText() const;
  QString redoText() const;

  void push(EditTransaction transaction);
  EditTransaction takeUndo();
  EditTransaction takeRedo();
  void clear();

signals:
  void stateChanged();

private:
  bool tryMergeWithLast(EditTransaction& next);

  static constexpr int kMaxUndoDepth = 100;
  static constexpr qint64 kMergeIntervalMs = 500;

  QVector<EditTransaction> undo_;
  QVector<EditTransaction> redo_;
  QElapsedTimer lastPushTime_;
};

}  // namespace muffin
