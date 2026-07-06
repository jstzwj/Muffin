#include "editor/SelectionController.h"

#include <utility>

namespace muffin {

SelectionController::SelectionController(QObject* parent) : QObject(parent) {}

bool SelectionController::hasCursor() const {
  return hasCursor_;
}

SelectionRange SelectionController::selection() const {
  return selection_;
}

CursorPosition SelectionController::cursorPosition() const {
  return selection_.focus;
}

HitTestResult SelectionController::currentHit() const {
  return currentHit_;
}

void SelectionController::setHitResult(HitTestResult hit) {
  if (!hit.isValid()) {
    clear();
    return;
  }

  currentHit_ = hit;
  selection_.anchor = hit.cursorPosition();
  selection_.focus = selection_.anchor;
  hasCursor_ = true;
  emit selectionChanged(selection_, currentHit_);
}

void SelectionController::setCursorPosition(CursorPosition position) {
  if (!position.isValid()) {
    clear();
    return;
  }

  currentHit_ = HitTestResult::from(position);
  selection_.anchor = position;
  selection_.focus = position;
  hasCursor_ = true;
  emit selectionChanged(selection_, currentHit_);
}

void SelectionController::setSelection(SelectionRange selection) {
  setSelection(std::move(selection), HitTestResult::from(selection.focus));
}

void SelectionController::setSelection(SelectionRange selection, HitTestResult focusHit) {
  if (!selection.focus.isValid()) {
    clear();
    return;
  }

  selection_ = selection;
  currentHit_ = focusHit.isValid() ? focusHit : HitTestResult::from(selection.focus);
  hasCursor_ = true;
  emit selectionChanged(selection_, currentHit_);
}

void SelectionController::clear() {
  if (!hasCursor_) {
    return;
  }

  selection_ = {};
  currentHit_ = {};
  hasCursor_ = false;
  emit selectionChanged(selection_, currentHit_);
}

}  // namespace muffin
