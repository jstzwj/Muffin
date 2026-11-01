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
  return canUndo() ? undo_.last().transaction.label() : QString();
}

QString UndoStack::redoText() const {
  return canRedo() ? redo_.last().transaction.label() : QString();
}

void UndoStack::push(EditTransaction transaction) {
  if (!transaction.isValid()) {
    return;
  }

  if (!tryMergeWithLast(transaction)) {
    undo_.push_back({std::move(transaction), currentState_, nextState_++});
  } else {
    undo_.last().after = nextState_++;
  }
  currentState_ = undo_.last().after;
  redo_.clear();

  while (undo_.size() > kMaxUndoDepth || (undo_.size() > 1 && retainedTextBytes() > kTextBudget)) {
    undo_.removeFirst();
  }

  lastPushTime_.restart();
  emit stateChanged();
}

qint64 UndoStack::retainedTextBytes() const {
  qint64 bytes = 0;
  for (const auto& entry : undo_)
    bytes += entry.transaction.retainedTextBytes();
  for (const auto& entry : redo_)
    bytes += entry.transaction.retainedTextBytes();
  return bytes;
}

EditTransaction UndoStack::takeUndo() {
  if (!canUndo()) {
    return {};
  }

  Entry entry = undo_.takeLast();
  currentState_ = entry.before;
  redo_.push_back(entry);
  emit stateChanged();
  return entry.transaction;
}

EditTransaction UndoStack::takeRedo() {
  if (!canRedo()) {
    return {};
  }

  Entry entry = redo_.takeLast();
  currentState_ = entry.after;
  undo_.push_back(entry);
  emit stateChanged();
  return entry.transaction;
}

void UndoStack::restoreUndo(const EditTransaction& transaction) {
  Q_UNUSED(transaction);
  if (!redo_.isEmpty()) {
    Entry entry = redo_.takeLast();
    currentState_ = entry.after;
    undo_.push_back(std::move(entry));
  }
  emit stateChanged();
}

void UndoStack::restoreRedo(const EditTransaction& transaction) {
  Q_UNUSED(transaction);
  if (!undo_.isEmpty()) {
    Entry entry = undo_.takeLast();
    currentState_ = entry.before;
    redo_.push_back(std::move(entry));
  }
  emit stateChanged();
}

void UndoStack::clear() {
  if (undo_.isEmpty() && redo_.isEmpty()) {
    return;
  }

  undo_.clear();
  redo_.clear();
  const bool clean = isClean();
  currentState_ = nextState_++;
  cleanState_ = clean ? currentState_ : 0;
  emit stateChanged();
}

void UndoStack::markClean() {
  cleanState_ = currentState_;
}
void UndoStack::invalidateClean() {
  cleanState_ = 0;
}
bool UndoStack::isClean() const {
  return currentState_ == cleanState_;
}

void UndoStack::updateSourceSelection(qsizetype anchor, qsizetype cursor) {
  if (!undo_.isEmpty())
    undo_.last().transaction.updateSourceSelection(anchor, cursor);
}

bool UndoStack::tryMergeWithLast(EditTransaction& next) {
  if (undo_.isEmpty() || isClean()) {
    return false;
  }
  if (lastPushTime_.elapsed() > kMergeIntervalMs) {
    return false;
  }

  EditTransaction& prev = undo_.last().transaction;
  if (prev.storage() != EditTransaction::Storage::TextDeltaCommand ||
      next.storage() != EditTransaction::Storage::TextDeltaCommand) {
    return false;
  }
  if (prev.kind() != next.kind()) {
    return false;
  }
  // Replacing a selection is one explicit operation, never a continuation of
  // neighbouring typing/backspaces, even within the normal merge interval.
  if (prev.beforeSelection().anchor.isValid() || next.beforeSelection().anchor.isValid())
    return false;

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

  // Don't coalesce across a caret relocation: type, click elsewhere, then type again within the
  // merge window would otherwise fuse into one undo step. Require the next edit to begin exactly
  // where the previous one left the caret (same block + offset + source offset + after-block flag).
  const CursorPosition& prevEnd = prev.textDeltaCommand().afterCursor;
  const CursorPosition& nextStart = next.textDeltaCommand().beforeCursor;
  if (prevEnd.blockId != nextStart.blockId || prevEnd.text.textOffset != nextStart.text.textOffset ||
      prevEnd.text.sourceOffset != nextStart.text.sourceOffset || prevEnd.afterBlock != nextStart.afterBlock) {
    return false;
  }

  prev.mergeTextDelta(next.textDeltaCommand());
  return true;
}

} // namespace muffin
