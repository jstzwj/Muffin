#include "app/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"

#include <cstdlib>
#include <iostream>
#include <variant>

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

MarkdownNode* codeFenceAt(DocumentSession& session, int index) {
  int current = 0;
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::CodeFence) {
      if (current == index) {
        return child.get();
      }
      ++current;
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
  EditTransaction codeInsertUndo = undoStack.takeUndo();
  require(codeInsertUndo.isReplaceNodeCommand(), "code insert should use ReplaceNodeCommand");
  require(codeInsertUndo.replaceNodeCommand().nodeType == BlockType::CodeFence, "code insert command type mismatch");

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
  EditTransaction languageUndo = undoStack.takeUndo();
  require(languageUndo.isSetNodeAttrCommand(), "set language should use SetNodeAttrCommand");
  require(languageUndo.setNodeAttrCommand().attribute == NodeAttribute::CodeLanguage, "set language attribute mismatch");
  require(std::get<QString>(languageUndo.setNodeAttrCommand().beforeValue).isEmpty(), "set language before value mismatch");
  require(std::get<QString>(languageUndo.setNodeAttrCommand().afterValue) == QStringLiteral("powershell"), "set language after value mismatch");

  require(controller.setContent(QStringLiteral("conan install\ncmake --build")), "set content should work");
  require(session.markdownText().contains(QStringLiteral("conan install\ncmake --build")), "set content markdown mismatch");
  EditTransaction contentUndo = undoStack.takeUndo();
  require(contentUndo.isReplaceNodeCommand(), "set content should use ReplaceNodeCommand");
}

void testSetLanguageForSpecificFenceKeepsCursor() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("```cpp\nfirst\n```\n\n```\nsecond\n```"), false);
  MarkdownNode* first = codeFenceAt(session, 0);
  MarkdownNode* second = codeFenceAt(session, 1);
  require(first != nullptr && second != nullptr, "two code fences missing");
  setCodeHit(selection, first, 2);

  require(controller.setLanguageFor(second->id(), QStringLiteral("python")), "set language for target fence should work");
  require(session.markdownText().contains(QStringLiteral("```cpp\nfirst\n```")), "first code language should remain unchanged");
  require(session.markdownText().contains(QStringLiteral("```python\nsecond\n```")), "second code language mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "target language edit should keep cursor offset");
  require(undoStack.canUndo(), "target language edit should push undo");
  EditTransaction targetLanguageUndo = undoStack.takeUndo();
  require(targetLanguageUndo.isSetNodeAttrCommand(), "target language edit should use SetNodeAttrCommand");
  require(targetLanguageUndo.setNodeAttrCommand().attribute == NodeAttribute::CodeLanguage, "target language attribute mismatch");
  require(std::get<QString>(targetLanguageUndo.setNodeAttrCommand().afterValue) == QStringLiteral("python"), "target language after value mismatch");
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
  testSetLanguageForSpecificFenceKeepsCursor();
  testSelectionReplaceAndDelete();
  return 0;
}
