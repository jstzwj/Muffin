#include "document/DocumentSession.h"
#include "blocks/literal/LiteralBlockController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include <cstdlib>
#include <iostream>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

MarkdownNode* firstMathBlock(DocumentSession& session) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::MathBlock) {
      return child.get();
    }
  }
  return nullptr;
}

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

void setMathHit(SelectionController& selection, MarkdownNode* math, qsizetype offset = 0) {
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Math;
  hit.blockId = math->id();
  hit.textNodeId = math->id();
  hit.textOffset = offset;
  selection.setHitResult(hit);
}

void testEnterEditAndTextEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  LiteralBlockController controller(mathSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("$$\na=b\n$$"), false);
  MarkdownNode* math = firstMathBlock(session);
  require(math != nullptr, "math block missing");
  setMathHit(selection, math, 0);

  require(controller.enterEditMode(), "enter math edit should work");
  require(controller.isEditing(), "controller should be in math edit mode");
  require(selection.cursorPosition().text.textOffset == math->literal().size(), "enter math edit cursor mismatch");

  require(controller.insertText(QStringLiteral("\n+c")), "math insert should work");
  require(session.markdownText().contains(QStringLiteral("a=b\n+c")), "math insert markdown mismatch");
  require(undoStack.canUndo(), "math insert should push undo");
  EditTransaction mathInsertUndo = undoStack.takeUndo();
  require(mathInsertUndo.isReplaceNodeCommand(), "math insert should use ReplaceNodeCommand");
  require(mathInsertUndo.replaceNodeCommand().nodeType == BlockType::MathBlock, "math insert command type mismatch");

  require(controller.deleteBackward(), "math backspace should work");
  require(session.markdownText().contains(QStringLiteral("a=b\n+")), "math backspace markdown mismatch");
}

void testSetTexAndRoundtripFence() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  LiteralBlockController controller(mathSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("before\n\n$$\nx^2\n$$\n\nafter"), false);
  MarkdownNode* math = firstMathBlock(session);
  require(math != nullptr, "math block missing for set tex test");
  setMathHit(selection, math, 0);

  require(controller.enterEditMode(), "enter math edit should work for set tex test");
  require(controller.setContent(QStringLiteral("E = mc^2\n\\\\int_0^1 x dx")), "set tex should work");
  require(session.markdownText().contains(QStringLiteral("$$\nE = mc^2\n\\\\int_0^1 x dx\n$$")), "math fence roundtrip mismatch");
  require(undoStack.canUndo(), "set tex should push undo");
  EditTransaction setTexUndo = undoStack.takeUndo();
  require(setTexUndo.isReplaceNodeCommand(), "set tex should use ReplaceNodeCommand");
}

void testBracketMathLiteralEditingPreservesBracketDelimiters() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  LiteralBlockController controller(mathSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("before\n\n\\[\nx^2\n\\]\n\nafter"), false);
  MarkdownNode* math = firstMathBlock(session);
  require(math != nullptr, "bracket math block missing");
  require(math->mathDelimiter() == MathDelimiter::Bracket, "bracket math delimiter should be preserved");
  require(math->literal() == QStringLiteral("x^2"), "bracket math literal should not include closing delimiter");
  setMathHit(selection, math, 0);

  require(controller.enterEditMode(), "enter bracket math edit should work");
  require(selection.cursorPosition().text.textOffset == math->literal().size(), "bracket math edit cursor mismatch");
  require(controller.setContent(QStringLiteral("E = mc^2")), "set bracket math tex should work");
  require(session.markdownText() == QStringLiteral("before\n\n\\[\nE = mc^2\n\\]\n\nafter"),
          "bracket math setContent should preserve bracket delimiters exactly");

  math = firstMathBlock(session);
  require(math != nullptr, "bracket math block missing after setContent");
  require(math->mathDelimiter() == MathDelimiter::Bracket, "bracket math delimiter should survive setContent");
  require(math->literal() == QStringLiteral("E = mc^2"), "bracket math literal mismatch after setContent");

  setMathHit(selection, math, math->literal().size());
  require(controller.enterEditMode(), "re-enter bracket math edit should work");
  require(controller.insertText(QStringLiteral("\n+ c")), "bracket math insert should work");
  require(session.markdownText() == QStringLiteral("before\n\n\\[\nE = mc^2\n+ c\n\\]\n\nafter"),
          "bracket math insert should keep closing bracket delimiter outside literal");
}

}  // namespace

int main() {
  testEnterEditAndTextEditing();
  testSetTexAndRoundtripFence();
  testBracketMathLiteralEditingPreservesBracketDelimiters();
  return 0;
}
