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
  inputController_.setTableController(&tableController_);
  inputController_.setCodeFenceController(&codeFenceController_);
  inputController_.setHtmlBlockController(&htmlBlockController_);
  inputController_.setMathBlockController(&mathBlockController_);
  inputController_.attach(view_);
  stylizeController_.setDocumentSession(session_);
  stylizeController_.setSelectionController(&selection_);
  stylizeController_.setUndoStack(&undoStack_);
  stylizeController_.setBrushQueue(&brushQueue_);
  codeFenceController_.setDocumentSession(session_);
  codeFenceController_.setSelectionController(&selection_);
  codeFenceController_.setUndoStack(&undoStack_);
  codeFenceController_.setBrushQueue(&brushQueue_);
  htmlBlockController_.setDocumentSession(session_);
  htmlBlockController_.setSelectionController(&selection_);
  htmlBlockController_.setUndoStack(&undoStack_);
  htmlBlockController_.setBrushQueue(&brushQueue_);
  mathBlockController_.setDocumentSession(session_);
  mathBlockController_.setSelectionController(&selection_);
  mathBlockController_.setUndoStack(&undoStack_);
  mathBlockController_.setBrushQueue(&brushQueue_);
  tableController_.setDocumentSession(session_);
  tableController_.setSelectionController(&selection_);
  tableController_.setUndoStack(&undoStack_);
  tableController_.setBrushQueue(&brushQueue_);
  clipboardController_.setDocumentSession(session_);
  clipboardController_.setSelectionController(&selection_);
  clipboardController_.setInputController(&inputController_);

  if (view_) {
    connect(view_, &EditorView::blockClicked, this, &EditorController::activateHit);
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

CodeFenceController& EditorController::codeFenceController() {
  return codeFenceController_;
}

HtmlBlockController& EditorController::htmlBlockController() {
  return htmlBlockController_;
}

MathBlockController& EditorController::mathBlockController() {
  return mathBlockController_;
}

TableController& EditorController::tableController() {
  return tableController_;
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

bool EditorController::insertTableRowBefore() {
  return tableController_.insertRowBefore();
}

bool EditorController::insertTableRowAfter() {
  return tableController_.insertRowAfter();
}

bool EditorController::deleteTableRow() {
  return tableController_.deleteCurrentRow();
}

bool EditorController::moveTableRowUp() {
  return tableController_.moveCurrentRowUp();
}

bool EditorController::moveTableRowDown() {
  return tableController_.moveCurrentRowDown();
}

bool EditorController::insertTableColumnBefore() {
  return tableController_.insertColumnBefore();
}

bool EditorController::insertTableColumnAfter() {
  return tableController_.insertColumnAfter();
}

bool EditorController::deleteTableColumn() {
  return tableController_.deleteCurrentColumn();
}

bool EditorController::moveTableColumnLeft() {
  return tableController_.moveCurrentColumnLeft();
}

bool EditorController::moveTableColumnRight() {
  return tableController_.moveCurrentColumnRight();
}

bool EditorController::setTableColumnAlignment(TableAlignment alignment) {
  return tableController_.setCurrentColumnAlignment(alignment);
}

bool EditorController::insertTable() {
  return tableController_.insertTable();
}

bool EditorController::enterCodeFenceEditMode() {
  return codeFenceController_.enterEditMode();
}

bool EditorController::exitCodeFenceEditMode() {
  return codeFenceController_.exitEditMode();
}

bool EditorController::setCodeFenceLanguage(QString language) {
  return codeFenceController_.setLanguage(std::move(language));
}

bool EditorController::enterHtmlBlockEditMode() {
  return htmlBlockController_.enterEditMode();
}

bool EditorController::exitHtmlBlockEditMode() {
  return htmlBlockController_.exitEditMode();
}

bool EditorController::setHtmlBlockSource(QString html) {
  return htmlBlockController_.setHtml(std::move(html));
}

bool EditorController::enterMathBlockEditMode() {
  return mathBlockController_.enterEditMode();
}

bool EditorController::exitMathBlockEditMode() {
  return mathBlockController_.exitEditMode();
}

bool EditorController::setMathBlockTex(QString tex) {
  return mathBlockController_.setTex(std::move(tex));
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
  codeFenceController_.exitEditMode();
  htmlBlockController_.exitEditMode();
  mathBlockController_.exitEditMode();
}

void EditorController::activateHit(HitTestResult hit) {
  if (!hit.isValid()) {
    selection_.clear();
    codeFenceController_.exitEditMode();
    htmlBlockController_.exitEditMode();
    mathBlockController_.exitEditMode();
    return;
  }

  switch (hit.zone) {
    case HitTestResult::Zone::Code:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (codeFenceController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    case HitTestResult::Zone::Html:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (htmlBlockController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    case HitTestResult::Zone::Math:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      if (mathBlockController_.enterEditMode()) {
        selection_.setHitResult(hit);
      }
      break;
    default:
      codeFenceController_.exitEditMode();
      htmlBlockController_.exitEditMode();
      mathBlockController_.exitEditMode();
      selection_.setHitResult(hit);
      break;
  }
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
