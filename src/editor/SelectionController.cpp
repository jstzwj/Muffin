#include "editor/SelectionController.h"

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

  currentHit_ = {};
  currentHit_.blockId = position.blockId;
  currentHit_.textNodeId = position.text.nodeId;
  currentHit_.textOffset = position.text.textOffset;
  currentHit_.sourceOffset = position.text.sourceOffset;
  selection_.anchor = position;
  selection_.focus = position;
  hasCursor_ = true;
  emit selectionChanged(selection_, currentHit_);
}

void SelectionController::setSelection(SelectionRange selection) {
  if (!selection.focus.isValid()) {
    clear();
    return;
  }

  selection_ = selection;
  currentHit_ = {};
  currentHit_.blockId = selection.focus.blockId;
  currentHit_.textNodeId = selection.focus.text.nodeId;
  currentHit_.textOffset = selection.focus.text.textOffset;
  currentHit_.sourceOffset = selection.focus.text.sourceOffset;
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
