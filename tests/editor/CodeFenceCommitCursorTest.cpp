// Regression tests for the caret placement when a pending block opener ("```", "$$", "\\[") is
// committed with Enter. Previously the post-edit cursor was resolved through
// nodeAtContentSourceOffset, which only matches inline-editable text blocks — a freshly created
// code/math block is a *literal* block, so the cursor was unresolvable and got cleared, making
// the caret vanish. These tests pin the fixed behavior: the caret lands inside the new block,
// the block's inline editor activates, typing routes into it, and undo followed by typing still
// works (a stale editor must not silently swallow keystrokes).
#include "../TestUtils.h"
#include "EditorTestUtils.h"

#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

namespace {

struct Harness {
  DocumentSession session;
  EditorView view;
  EditorController controller;

  Harness() {
    controller.attach(&session, &view);
    view.resize(900, 500);
    session.setMarkdownText(QString(), false);
    view.setDocument(session.document());
    setCursor(controller.selection(), blockAt(session, 0), 0);
  }

  // Type character-by-character to reach the same pending-marker state real editing produces.
  void type(QStringView text) {
    for (QChar ch : text) {
      require(controller.inputController().insertText(QString(ch)),
              QStringLiteral("typing '%1' should succeed").arg(ch));
    }
  }
};

}  // namespace

// "```" + Enter commits to a code block; the caret must land inside it and the code editor must
// be active so the next keystroke types into the fence rather than being rejected.
void testBacktickFenceCommitCaretInsideBlock() {
  Harness h;
  h.type(QStringLiteral("```"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "precondition: '```' is a pending paragraph");
  require(h.controller.inputController().insertParagraphBreak(), "Enter on '```' should commit");

  MarkdownNode* fence = firstBlockOfType(h.session, BlockType::CodeFence);
  require(fence != nullptr, "'```' + Enter should produce a code fence");
  const CursorPosition cursor = h.controller.selection().cursorPosition();
  require(cursor.blockId == fence->id(), "caret should be inside the code fence, not lost");
  require(cursor.text.textOffset == 0, "caret should be at the start of the empty code content");
  require(h.controller.codeFenceController().isEditing(), "code fence should be in edit mode after commit");

  // Typing routes into the fence (proves the caret is genuinely inside, not just visually).
  require(h.controller.inputController().insertText(QStringLiteral("x")), "typing into the committed fence should succeed");
  require(h.session.markdownText() == QStringLiteral("```\nx\n```"), "typed text should land inside the code fence");
  require(firstBlockOfType(h.session, BlockType::CodeFence)->literal() == QStringLiteral("x"),
          "code fence literal should hold the typed text");
}

// "$$" + Enter commits to a math block; caret inside, typing routes into it.
void testDollarMathCommitCaretInsideBlock() {
  Harness h;
  h.type(QStringLiteral("$$"));
  require(h.controller.inputController().insertParagraphBreak(), "Enter on '$$' should commit");

  MarkdownNode* math = firstBlockOfType(h.session, BlockType::MathBlock);
  require(math != nullptr, "'$$' + Enter should produce a math block");
  const CursorPosition cursor = h.controller.selection().cursorPosition();
  require(cursor.blockId == math->id(), "caret should be inside the math block, not lost");
  require(cursor.text.textOffset == 0, "caret should be at the start of the empty math content");

  require(h.controller.inputController().insertText(QStringLiteral("x")), "typing into the committed math block should succeed");
  require(firstBlockOfType(h.session, BlockType::MathBlock)->literal() == QStringLiteral("x"),
          "math block literal should hold the typed text");
}

// "\\[" + Enter commits to a bracket math block; caret inside, typing routes into it.
void testBracketMathCommitCaretInsideBlock() {
  Harness h;
  h.type(QStringLiteral("\\["));
  require(h.controller.inputController().insertParagraphBreak(), "Enter on '\\[' should commit");

  MarkdownNode* math = firstBlockOfType(h.session, BlockType::MathBlock);
  require(math != nullptr, "'\\[' + Enter should produce a math block");
  const CursorPosition cursor = h.controller.selection().cursorPosition();
  require(cursor.blockId == math->id(), "caret should be inside the bracket math block, not lost");
  require(cursor.text.textOffset == 0, "caret should be at the start of the empty math content");

  require(h.controller.inputController().insertText(QStringLiteral("x")), "typing into the committed math block should succeed");
  require(firstBlockOfType(h.session, BlockType::MathBlock)->literal() == QStringLiteral("x"),
          "bracket math literal should hold the typed text");
}

// After committing a code block and undoing, the code editor is left stale while the caret
// returns to the paragraph. The next keystroke must still type into the paragraph instead of
// being silently swallowed by the stale editor.
void testTypingAfterCommitUndoStillWorks() {
  Harness h;
  h.type(QStringLiteral("```"));
  require(h.controller.inputController().insertParagraphBreak(), "Enter on '```' should commit");
  require(h.controller.codeFenceController().isEditing(), "code fence should be editing right after commit");

  h.controller.undo();
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "undo should restore the pending paragraph");
  require(h.session.markdownText() == QStringLiteral("```"), "undo should restore the '```' text");

  setCursor(h.controller.selection(), blockAt(h.session, 0), h.session.markdownText().size());
  require(h.controller.inputController().insertText(QStringLiteral("x")),
          "typing after undo must succeed (stale editor must not swallow it)");
  require(h.session.markdownText() == QStringLiteral("```x"), "typing should edit the restored paragraph");
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "typing should not recommit to a code fence");
  require(!h.controller.codeFenceController().isEditing(), "stale code editor should be exited before typing");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testBacktickFenceCommitCaretInsideBlock);
  RUN_TEST(testDollarMathCommitCaretInsideBlock);
  RUN_TEST(testBracketMathCommitCaretInsideBlock);
  RUN_TEST(testTypingAfterCommitUndoStillWorks);
#undef RUN_TEST
  return 0;
}
