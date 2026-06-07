#include "edit/UndoStack.h"

namespace muffin {

UndoStack::UndoStack(QObject* parent) : QObject(parent) {
  lastPushTime_.start();
}

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

  if (!tryMergeWithLast(transaction)) {
    undo_.push_back(std::move(transaction));
  }
  redo_.clear();

  while (undo_.size() > kMaxUndoDepth) {
    undo_.removeFirst();
  }

  lastPushTime_.restart();
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

bool UndoStack::tryMergeWithLast(EditTransaction& next) {
  if (undo_.isEmpty()) {
    return false;
  }
  if (lastPushTime_.elapsed() > kMergeIntervalMs) {
    return false;
  }

  EditTransaction& prev = undo_.last();
  if (prev.storage() != EditTransaction::Storage::TextDeltaCommand ||
      next.storage() != EditTransaction::Storage::TextDeltaCommand) {
    return false;
  }
  if (prev.kind() != next.kind()) {
    return false;
  }

  const TextDelta& prevDelta = prev.textDeltaCommand().delta;
  const TextDelta& nextDelta = next.textDeltaCommand().delta;

  bool mergeable = false;
  if (prev.kind() == EditTransaction::Kind::InsertText) {
    mergeable = nextDelta.start == prevDelta.start + prevDelta.insertedText.size();
  } else if (prev.kind() == EditTransaction::Kind::DeleteText) {
    mergeable = nextDelta.start + nextDelta.removedText.size() == prevDelta.start;
  }

  if (!mergeable) {
    return false;
  }

  prev.mergeTextDelta(next.textDeltaCommand());
  return true;
}

}  // namespace muffin
