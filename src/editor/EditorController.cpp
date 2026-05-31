#include "editor/EditorController.h"

#include "editor/EditorView.h"

namespace muffin {

EditorController::EditorController(QObject* parent) : QObject(parent) {}

void EditorController::attach(DocumentSession* session, EditorView* view) {
  if (session_ == session && view_ == view) {
    return;
  }

  detach();
  session_ = session;
  view_ = view;

  inputController_.setDocumentSession(session_);
  inputController_.setSelectionController(&selection_);
  inputController_.setUndoStack(&undoStack_);
  inputController_.setBrushQueue(&brushQueue_);
  inputController_.attach(view_);
  stylizeController_.setDocumentSession(session_);
  stylizeController_.setSelectionController(&selection_);
  stylizeController_.setUndoStack(&undoStack_);
  stylizeController_.setBrushQueue(&brushQueue_);
  clipboardController_.setInputController(&inputController_);

  if (view_) {
    connect(view_, &EditorView::blockClicked, &selection_, &SelectionController::setHitResult);
    connect(view_, &EditorView::selectionChanged, &selection_, &SelectionController::setSelection);
    connect(view_, &EditorView::textCommitted, &inputController_, &InputController::insertText);
  }
  connect(&selection_, &SelectionController::selectionChanged, this, [this](SelectionRange selection, HitTestResult hit) {
    if (view_) {
      if (selection.focus.isValid() && !selection.isCollapsed()) {
        view_->setSelectionRange(selection);
      } else if (selection.focus.isValid()) {
        view_->setCursorPosition(selection.focus);
      } else {
        view_->clearCursor();
      }
    }
    emit cursorChanged(hit);
    emit stateChanged();
  });
  connect(&undoStack_, &UndoStack::stateChanged, this, &EditorController::stateChanged);
  connect(&brushQueue_, &BrushQueue::blockRefreshRequested, this, [this](NodeId) {
    if (session_ && view_) {
      view_->setDocument(session_->document());
    }
  });
  connect(&brushQueue_, &BrushQueue::fullRefreshRequested, this, [this] {
    if (session_ && view_) {
      view_->setDocument(session_->document());
    }
  });
}

void EditorController::detach() {
  if (view_) {
    view_->disconnect(this);
  }
  selection_.disconnect(this);
  undoStack_.disconnect(this);
  brushQueue_.disconnect(this);
  inputController_.attach(nullptr);
  session_ = nullptr;
  view_ = nullptr;
}

SelectionController& EditorController::selection() {
  return selection_;
}

const SelectionController& EditorController::selection() const {
  return selection_;
}

UndoStack& EditorController::undoStack() {
  return undoStack_;
}

const UndoStack& EditorController::undoStack() const {
  return undoStack_;
}

InputController& EditorController::inputController() {
  return inputController_;
}

StylizeController& EditorController::stylizeController() {
  return stylizeController_;
}

ClipboardController& EditorController::clipboardController() {
  return clipboardController_;
}

BrushQueue& EditorController::brushQueue() {
  return brushQueue_;
}

bool EditorController::canUndo() const {
  return undoStack_.canUndo();
}

bool EditorController::canRedo() const {
  return undoStack_.canRedo();
}

void EditorController::undo() {
  if (!canUndo()) {
    return;
  }
  applySnapshot(undoStack_.takeUndo().before());
}

void EditorController::redo() {
  if (!canRedo()) {
    return;
  }
  applySnapshot(undoStack_.takeRedo().after());
}

bool EditorController::toggleBold() {
  return stylizeController_.toggleBold();
}

bool EditorController::toggleItalic() {
  return stylizeController_.toggleItalic();
}

bool EditorController::toggleCode() {
  return stylizeController_.toggleCode();
}

bool EditorController::insertLink() {
  return stylizeController_.insertLink();
}

bool EditorController::copy() {
  return clipboardController_.copy();
}

bool EditorController::cut() {
  return clipboardController_.cut();
}

bool EditorController::paste() {
  return clipboardController_.paste();
}

void EditorController::clearHistoryAndSelection() {
  undoStack_.clear();
  selection_.clear();
}

void EditorController::applySnapshot(const DocumentSnapshot& snapshot) {
  if (!session_) {
    return;
  }

  session_->applyMarkdownText(snapshot.markdownText, true);
  if (snapshot.cursor.isValid()) {
    selection_.setCursorPosition(snapshot.cursor);
  } else {
    selection_.clear();
  }
  brushQueue_.requestFullRefresh();
}

}  // namespace muffin
