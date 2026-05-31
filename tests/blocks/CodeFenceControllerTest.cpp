#include "app/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
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

MarkdownNode* firstCodeFence(DocumentSession& session) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::CodeFence) {
      return child.get();
    }
  }
  return nullptr;
}

void setCodeHit(SelectionController& selection, MarkdownNode* code, qsizetype offset = 0) {
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = offset;
  selection.setHitResult(hit);
}

void setCodeSelection(SelectionController& selection, MarkdownNode* code, qsizetype anchor, qsizetype focus) {
  SelectionRange range;
  range.anchor.blockId = code->id();
  range.anchor.text.nodeId = code->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = code->id();
  range.focus.text.nodeId = code->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

void testEnterEditAndTextEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("```cpp\nreturn 0;\n```"), false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "code fence missing");
  setCodeHit(selection, code, 0);

  require(controller.enterEditMode(), "enter code edit should work");
  require(controller.isEditing(), "controller should be in edit mode");
  require(selection.cursorPosition().text.textOffset == code->literal().size(), "enter edit cursor mismatch");

  require(controller.insertText(QStringLiteral("\n// done")), "code insert should work");
  require(session.markdownText().contains(QStringLiteral("// done")), "code insert markdown mismatch");
  require(undoStack.canUndo(), "code insert should push undo");

  require(controller.deleteBackward(), "code backspace should work");
  require(!session.markdownText().contains(QStringLiteral("// done")), "code backspace markdown mismatch");
}

void testSetLanguageAndContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("```\nalpha\n```"), false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "code fence missing for language test");
  setCodeHit(selection, code, 0);

  require(controller.enterEditMode(), "enter code edit should work for language test");
  require(controller.setLanguage(QStringLiteral("powershell")), "set language should work");
  require(session.markdownText().startsWith(QStringLiteral("```powershell")), "set language markdown mismatch");

  require(controller.setContent(QStringLiteral("conan install\ncmake --build")), "set content should work");
  require(session.markdownText().contains(QStringLiteral("conan install\ncmake --build")), "set content markdown mismatch");
}

void testSelectionReplaceAndDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("```cpp\nabcdef\n```"), false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "code fence missing for selection test");
  setCodeHit(selection, code, 0);
  require(controller.enterEditMode(), "enter code edit should work for selection test");

  setCodeSelection(selection, firstCodeFence(session), 1, 4);
  require(controller.insertText(QStringLiteral("X")), "code selection typing should replace selection");
  require(session.markdownText().contains(QStringLiteral("aXef")), "code selection replace mismatch");

  setCodeSelection(selection, firstCodeFence(session), 1, 2);
  require(controller.deleteBackward(), "code selection backspace should delete selection");
  require(session.markdownText().contains(QStringLiteral("aef")), "code selection delete mismatch");
}

}  // namespace

int main() {
  testEnterEditAndTextEditing();
  testSetLanguageAndContent();
  testSelectionReplaceAndDelete();
  return 0;
}
