#pragma once

#include "edit/EditTransaction.h"

#include <QObject>
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
  QVector<EditTransaction> undo_;
  QVector<EditTransaction> redo_;
};

}  // namespace muffin
