#pragma once

#include "edit/EditTransaction.h"
#include "Export.h"

#include <QObject>
#include <QElapsedTimer>
#include <QVector>

namespace muffin {

class MUFFIN_UI_EXPORT UndoStack final : public QObject {
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
  // Puts back a transaction whose application failed after takeUndo/takeRedo already
  // moved it to the opposite stack: drop it from there and push it back onto its
  // original stack, so a failed undo/redo leaves the history exactly as it was
  // instead of silently consuming the step. Call immediately after the failed take
  // (synchronously, before anything else can push) — the opposite stack's tail is
  // assumed to be this transaction.
  void restoreUndo(const EditTransaction& transaction);
  void restoreRedo(const EditTransaction& transaction);
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
