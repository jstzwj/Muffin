#include "edit/UndoStack.h"

namespace muffin {

UndoStack::UndoStack(QObject* parent) : QObject(parent) {}

bool UndoStack::canUndo() const {
  return !undo_.isEmpty();
}

bool UndoStack::canRedo() const {
  return !redo_.isEmpty();
}

QString UndoStack::undoText() const {
  return canUndo() ? undo_.last().label() : QString();
}

QString UndoStack::redoText() const {
  return canRedo() ? redo_.last().label() : QString();
}

void UndoStack::push(EditTransaction transaction) {
  if (!transaction.isValid()) {
    return;
  }

  undo_.push_back(std::move(transaction));
  redo_.clear();
  emit stateChanged();
}

EditTransaction UndoStack::takeUndo() {
  if (!canUndo()) {
    return {};
  }

  EditTransaction transaction = undo_.takeLast();
  redo_.push_back(transaction);
  emit stateChanged();
  return transaction;
}

EditTransaction UndoStack::takeRedo() {
  if (!canRedo()) {
    return {};
  }

  EditTransaction transaction = redo_.takeLast();
  undo_.push_back(transaction);
  emit stateChanged();
  return transaction;
}

void UndoStack::clear() {
  if (undo_.isEmpty() && redo_.isEmpty()) {
    return;
  }

  undo_.clear();
  redo_.clear();
  emit stateChanged();
}

}  // namespace muffin
