// Regression tests for backspace at the start of a paragraph that immediately follows a fenced
// literal block (code fence or math). Previously such a backspace was a silent no-op: the generic
// paragraph merge looks up the previous block via previousEditableTextBlock, which deliberately
// skips literal blocks (they edit through dedicated controllers, not inline text), so it found no
// previous editable block and returned handled=false, swallowing the keystroke. The fix adds a
// dedicated merge that folds the paragraph's text into the block's last content line (relocating
// the closing fence below it) and lands the caret inside the block. These tests pin that behavior
// for code fences and for both math delimiter styles ($$ and \[), plus the empty-paragraph case,
// the following-block-preserved case, undo, and the builder-level command it produces.
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/TextBlockCommandBuilder.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

namespace {

struct Harness {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  Harness() { wireInput(input, session, selection, undoStack, brushQueue); }
};

MarkdownNode* literalBlock(DocumentSession& session, BlockType type) {
  return firstBlockOfType(session, type);
}

}  // namespace

// Code fence + empty paragraph: backspace folds the empty paragraph into the code block. The caret
// must land inside the block at the end of its content (the user's exact scenario).
void testCodeFenceBackspaceEmptyParagraphMergesIntoBlock() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("```\n1231\n```\n\n"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "code-fence empty-paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("```\n1231\n```"), "code-fence empty merge text mismatch");
  MarkdownNode* code = literalBlock(h.session, BlockType::CodeFence);
  require(code != nullptr, "code fence should survive the merge");
  require(code->literal() == QStringLiteral("1231"), "code fence content should be unchanged by empty merge");
  require(h.selection.cursorPosition().blockId == code->id(), "code-fence empty merge cursor should be inside the block");
  require(h.selection.cursorPosition().text.textOffset == 4, "code-fence empty merge cursor should sit at content end");
}

// Code fence + non-empty paragraph: the paragraph text is appended to the code block's last line,
// and the closing fence is relocated below it.
void testCodeFenceBackspaceNonEmptyParagraphAppendsToLastLine() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("```\n1231\n```\n\nhello"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "code-fence paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("```\n1231hello\n```"), "code-fence append merge text mismatch");
  MarkdownNode* code = literalBlock(h.session, BlockType::CodeFence);
  require(code != nullptr, "code fence should survive the append merge");
  require(code->literal() == QStringLiteral("1231hello"), "code fence content should include the paragraph text");
  require(h.selection.cursorPosition().blockId == code->id(), "code-fence append merge cursor should be inside the block");
  require(h.selection.cursorPosition().text.textOffset == 9, "code-fence append merge cursor should sit after the grafted text");

  // A following block survives intact: only the paragraph directly after the fence is consumed.
  h.session.setMarkdownText(QStringLiteral("```\n1231\n```\n\nhello\n\nworld"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);
  require(h.input.deleteBackward(), "code-fence paragraph backspace with trailing block should be handled");
  require(h.session.markdownText() == QStringLiteral("```\n1231hello\n```\n\nworld"), "code-fence merge should preserve the following block");
  require(literalBlock(h.session, BlockType::CodeFence)->literal() == QStringLiteral("1231hello"),
          "code fence content should include grafted text with trailing block present");
}

// A paragraph carrying inline formatting is grafted verbatim (code blocks are literal), not as
// rendered/visible text with markers stripped.
void testCodeFenceBackspaceGraftsRawParagraphSource() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("```\n1231\n```\n\n**bold**"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "code-fence formatted-paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("```\n1231**bold**\n```"), "code-fence raw graft text mismatch");
  require(literalBlock(h.session, BlockType::CodeFence)->literal() == QStringLiteral("1231**bold**"),
          "code fence content should graft raw paragraph source");
}

// Math $$ block + paragraph directly after (no blank line): the paragraph text is appended to the
// math content, the closing $$ is relocated, and the caret lands inside the math block.
void testMathDollarBackspaceAppendsToLastLine() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("$$\na=b\n$$\nhello"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "math $$ paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("$$\na=bhello\n$$"), "math $$ append merge text mismatch");
  MarkdownNode* math = literalBlock(h.session, BlockType::MathBlock);
  require(math != nullptr, "math block should survive the merge");
  require(math->literal() == QStringLiteral("a=bhello"), "math content should include the paragraph text");
  require(h.selection.cursorPosition().blockId == math->id(), "math $$ merge cursor should be inside the block");
  require(h.selection.cursorPosition().text.textOffset == 8, "math $$ merge cursor should sit after the grafted text");
}

// Math \[ ... \] block + paragraph directly after: same merge behavior as $$.
void testMathBracketBackspaceAppendsToLastLine() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("\\[\na=b\n\\]\nhello"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "math \\[ paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("\\[\na=bhello\n\\]"), "math \\[ append merge text mismatch");
  MarkdownNode* math = literalBlock(h.session, BlockType::MathBlock);
  require(math != nullptr, "math bracket block should survive the merge");
  require(math->literal() == QStringLiteral("a=bhello"), "math bracket content should include the paragraph text");
  require(h.selection.cursorPosition().blockId == math->id(), "math \\[ merge cursor should be inside the block");
  require(h.selection.cursorPosition().text.textOffset == 8, "math \\[ merge cursor should sit after the grafted text");
}

// Empty paragraph sitting directly after a literal block (a blank line between the block and the
// next real paragraph) merges into the block; the real paragraph below survives. Math blocks emit
// such a virtual empty paragraph for the blank line, so they exercise this path.
void testLiteralBackspaceConsumesEmptyParagraphBeforeFollowingBlock() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("$$\na=b\n$$\n\nhello"), false);
  // child[1] is the empty paragraph right after the math block; "hello" is child[2].
  setCursor(h.selection, blockAt(h.session, 1), 0);
  require(h.input.deleteBackward(), "math empty-paragraph-before-block backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("$$\na=b\n$$\nhello"),
          "empty-paragraph merge should drop the blank line and keep the following paragraph");
  MarkdownNode* math = literalBlock(h.session, BlockType::MathBlock);
  require(math != nullptr, "math block should survive the empty merge");
  require(math->literal() == QStringLiteral("a=b"), "empty-paragraph merge should not change the math content");
  require(h.selection.cursorPosition().blockId == math->id(), "math empty merge cursor should be inside the block");
  require(h.selection.cursorPosition().text.textOffset == 3, "math empty merge cursor should sit at content end");
}

// The merge must be undoable as a clean snapshot (it restructures across two blocks).
void testLiteralBackspaceMergeProducesSnapshotUndo() {
  Harness h;
  const QString before = QStringLiteral("```\n1231\n```\n\nhello");
  h.session.setMarkdownText(before, false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  require(h.input.deleteBackward(), "code-fence merge should be handled");
  require(h.undoStack.canUndo(), "code-fence merge should be undoable");
  EditTransaction undo = h.undoStack.takeUndo();
  require(undo.isSnapshot(), "code-fence merge should use a snapshot undo command");
  require(undo.before().markdownText == before, "code-fence merge undo before text mismatch");
  require(undo.after().markdownText == h.session.markdownText(), "code-fence merge undo after text mismatch");
}

// Regression guard: a paragraph whose previous sibling is a normal paragraph still uses the generic
// merge (direct concatenation), not the literal-block path.
void testNormalParagraphBackspaceStillMergesNormally() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);
  require(h.input.deleteBackward(), "normal paragraph backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("alphabeta"), "normal paragraph merge text mismatch");
  require(h.selection.cursorPosition().text.textOffset == 5, "normal paragraph merge cursor mismatch");
}

// Builder-level pin on the command the literal-block merge produces, so a refactor that changes the
// delta shape is caught directly.
void testBuilderProducesLiteralMergeCommand() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("```\n1231\n```\n\nhello"), false);
  setCursor(h.selection, blockAt(h.session, 1), 0);

  BlockEditContextResolver resolver(&h.session, &h.selection);
  BlockEditContext context;
  require(resolver.current(context), "builder context should resolve the paragraph");

  const TextBlockCommandBuilder builder(&h.session, &resolver);
  TextBlockCommandBuilder::Command command =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Backspace);
  require(command.valid && command.handled, "builder should produce a literal-merge backspace command");
  require(command.hasLocalEdit(), "literal-merge command should carry a local edit");
  require(command.sourceStart == 8, "literal-merge source start should sit after the code content");
  require(command.removedLength == 11, "literal-merge removed length should span fence, separator and paragraph");
  require(command.insertedText == QStringLiteral("hello\n```"), "literal-merge inserted text should graft text then relocate the fence");
  require(command.fallbackSourceOffset == 13, "literal-merge fallback cursor should sit after the grafted text");
  require(command.structureEdit, "literal-merge should be flagged as a structure edit");
  require(command.label == QStringLiteral("Merge Into Block"), "literal-merge label mismatch");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCodeFenceBackspaceEmptyParagraphMergesIntoBlock);
  RUN_TEST(testCodeFenceBackspaceNonEmptyParagraphAppendsToLastLine);
  RUN_TEST(testCodeFenceBackspaceGraftsRawParagraphSource);
  RUN_TEST(testMathDollarBackspaceAppendsToLastLine);
  RUN_TEST(testMathBracketBackspaceAppendsToLastLine);
  RUN_TEST(testLiteralBackspaceConsumesEmptyParagraphBeforeFollowingBlock);
  RUN_TEST(testLiteralBackspaceMergeProducesSnapshotUndo);
  RUN_TEST(testNormalParagraphBackspaceStillMergesNormally);
  RUN_TEST(testBuilderProducesLiteralMergeCommand);
#undef RUN_TEST
  return 0;
}
