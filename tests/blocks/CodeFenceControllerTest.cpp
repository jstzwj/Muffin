#include "document/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

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

void testIndentedCodeEditingStaysIndented() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("    conan install\n    cmake --build"), false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "indented code block missing");
  require(code->isIndentedCode(), "indented code block should be flagged as indented");
  require(code->literal() == QStringLiteral("conan install\ncmake --build"), "indented code literal should be stripped of indent");

  setCodeHit(selection, code, 0);
  require(controller.enterEditMode(), "enter edit on indented code should work");

  // Type at the end of the block — must stay indented, NOT be rewritten as a fenced block.
  require(controller.insertText(QStringLiteral("2")), "insert into indented code should work");
  const QString md = session.markdownText();
  require(!md.contains(QLatin1String("```")), "indented code must not be rewritten as fenced on edit");
  require(md.contains(QLatin1String("    cmake --build2")), "indented code edit should preserve 4-space indent");
  MarkdownNode* after = firstCodeFence(session);
  require(after != nullptr && after->isIndentedCode(), "indented code should remain flagged as indented after edit");

  // setContent must also preserve the indented form.
  require(controller.setContent(QStringLiteral("alpha\nbeta")), "setContent on indented code should work");
  const QString md2 = session.markdownText();
  require(!md2.contains(QLatin1String("```")), "setContent must not rewrite indented code as fenced");
  require(md2.contains(QLatin1String("    alpha")), "setContent should re-indent indented code");

  // deleteBackward must also preserve the indented form.
  require(controller.deleteBackward(), "deleteBackward on indented code should work");
  require(!session.markdownText().contains(QLatin1String("```")), "deleteBackward must not rewrite indented code as fenced");
}

// Regression for the user-reported scenario: an indented code block that follows a
// paragraph (separated by a blank line). Typing, pressing Enter, and repeated edits must all
// preserve the indented form — the block must never be rewritten as a fenced (```) block.
void testIndentedCodeAfterParagraphStaysIndented() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  const QString source = QStringLiteral("Indented code:\n\n    conan install\n    cmake --build");
  session.setMarkdownText(source, false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "indented code block after paragraph missing");
  require(code->isIndentedCode(), "indented code (after paragraph) should be flagged as indented");

  setCodeHit(selection, code, 0);
  require(controller.enterEditMode(), "enter edit on indented code (after paragraph) should work");

  // Typing a character must keep the block indented.
  require(controller.insertText(QStringLiteral("2")), "type into indented code (after paragraph) should work");
  require(!session.markdownText().contains(QLatin1String("```")), "indented code must not become fenced after typing");
  require(session.markdownText().contains(QLatin1String("    cmake --build2")), "typed text should land on the indented line");
  require(firstCodeFence(session)->isIndentedCode(), "indented flag must survive typing");

  // Pressing Enter (new line) must keep the block indented.
  require(controller.insertText(QStringLiteral("\n")), "Enter in indented code (after paragraph) should work");
  require(!session.markdownText().contains(QLatin1String("```")), "indented code must not become fenced after Enter");
  require(firstCodeFence(session)->isIndentedCode(), "indented flag must survive Enter");

  // A second typed character after the Enter must still be indented.
  require(controller.insertText(QStringLiteral("x")), "type after Enter should work");
  require(!session.markdownText().contains(QLatin1String("```")), "indented code must not become fenced on second edit");
  require(firstCodeFence(session)->isIndentedCode(), "indented flag must survive repeated edits");
}

// Enter at the very end of an indented code block cannot persist a trailing empty line (cmark
// strips it). Instead the editor shows a phantom blank line held only in the node, and commits
// it to the document once a character lands on that line.
void testEnterAtEndOfIndentedCodeShowsPhantomLine() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController controller;
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  const QString source = QStringLiteral("    line1\n    line2");
  session.setMarkdownText(source, false);
  MarkdownNode* code = firstCodeFence(session);
  require(code != nullptr, "indented code block missing");
  require(code->isIndentedCode(), "should be flagged indented");

  setCodeHit(selection, code, 0);
  require(controller.enterEditMode(), "enter edit should work");

  // Enter at the end must NOT mutate the source (cmark would strip a real trailing empty line);
  // instead it extends the node literal with a phantom trailing newline (the phantom IS that '\n').
  const QString beforeEnter = session.markdownText();
  require(controller.insertText(QStringLiteral("\n")), "Enter at end should be handled");
  require(session.markdownText() == beforeEnter, "Enter at end must not mutate the source");
  MarkdownNode* afterEnter = firstCodeFence(session);
  require(afterEnter != nullptr, "code block still present after phantom Enter");
  require(controller.hasPendingTrailingNewline(), "phantom line should be present after Enter");
  require(afterEnter->literal() == QStringLiteral("line1\nline2\n"), "phantom newline should extend the node literal");

  // Typing a character commits the phantom line as real indented code.
  require(controller.insertText(QStringLiteral("x")), "commit character should be accepted");
  MarkdownNode* afterCommit = firstCodeFence(session);
  require(afterCommit != nullptr && !controller.hasPendingTrailingNewline(), "phantom must clear after commit");
  require(session.markdownText().contains(QStringLiteral("    line2\n    x")), "committed line should be indented code");
  require(!session.markdownText().contains(QLatin1String("```")), "must stay indented, not become fenced");

  // Clearing the phantom undoes it without touching the source.
  session.setMarkdownText(source, false);
  setCodeHit(selection, firstCodeFence(session), 0);
  controller.enterEditMode();
  require(controller.insertText(QStringLiteral("\n")), "phantom Enter before clear");
  require(controller.hasPendingTrailingNewline(), "phantom should be set before clear");
  controller.clearPendingTrailingNewline();
  require(!controller.hasPendingTrailingNewline(), "phantom should clear after clear");
  require(firstCodeFence(session)->literal() == QStringLiteral("line1\nline2"), "literal should be restored");
  require(session.markdownText() == source, "source should be unchanged after clear");
}

}  // namespace

int main() {
  testEnterEditAndTextEditing();
  testSetLanguageAndContent();
  testSetLanguageForSpecificFenceKeepsCursor();
  testSelectionReplaceAndDelete();
  testIndentedCodeEditingStaysIndented();
  testIndentedCodeAfterParagraphStaysIndented();
  testEnterAtEndOfIndentedCodeShowsPhantomLine();
  return 0;
}
