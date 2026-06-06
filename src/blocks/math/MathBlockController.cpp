#include "blocks/math/MathBlockController.h"

namespace muffin {

namespace {

LiteralBlockSpec mathSpec() {
  return LiteralBlockSpec{
      BlockType::MathBlock,
      HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")};
}

}  // namespace

MathBlockController::MathBlockController(QObject* parent) : QObject(parent), literal_(mathSpec()) {
  literal_.setRejectedHandler([this](QString reason) { emit mathCommandRejected(std::move(reason)); });
}

void MathBlockController::setDocumentSession(DocumentSession* session) {
  literal_.setDocumentSession(session);
}

void MathBlockController::setSelectionController(SelectionController* selection) {
  literal_.setSelectionController(selection);
}

void MathBlockController::setUndoStack(UndoStack* undoStack) {
  literal_.setUndoStack(undoStack);
}

void MathBlockController::setBrushQueue(BrushQueue* brushQueue) {
  literal_.setBrushQueue(brushQueue);
}

NodeId MathBlockController::currentMathBlockId() const {
  return literal_.currentBlockId();
}

bool MathBlockController::isEditing() const {
  return literal_.isEditing();
}

bool MathBlockController::enterEditMode() {
  return literal_.enterEditMode();
}

bool MathBlockController::exitEditMode() {
  return literal_.exitEditMode();
}

bool MathBlockController::insertText(QString text) {
  return literal_.insertText(std::move(text));
}

bool MathBlockController::deleteBackward() {
  return literal_.deleteBackward();
}

bool MathBlockController::deleteForward() {
  return literal_.deleteForward();
}

bool MathBlockController::deleteSelection() {
  return literal_.deleteSelection();
}

bool MathBlockController::setTex(QString tex) {
  return literal_.setContent(std::move(tex));
}

QString MathBlockController::tabText() const {
  return literal_.tabText();
}

}  // namespace muffin
