// The one path a user edit takes: applyEdit (editor/TextEditApplier.h) changes the text through
// the session, places the caret or selection, records the undo step and has the touched blocks
// re-laid out.
//
// The style and paragraph commands used to carry a copy of that pipeline. Its caret mapping wrote
// the source offset into the caret as if the block held no Markdown marker, so behind a `**bold**`
// word the caret landed too far right, while undo — which used the right mapping — put it
// somewhere else than the command had. Its undo step also forgot a selection the command was
// applied to.

#include "app/RenderCommandFacade.h"
#include "commands/StylizeController.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"
#include "editor/TextEditApplier.h"

#include "EditorTestUtils.h"

#include <QApplication>

using namespace muffin;

namespace {

struct Editor {
  DocumentSession session;
  EditorController controller;
  EditorView view;

  Editor() {
    controller.attach(&session, &view);
    view.resize(720, 460);
  }

  void open(const QString& markdown) {
    session.setMarkdownText(markdown, false);
    view.setDocument(session.document());
  }

  SelectionController& selection() { return controller.selection(); }
  StylizeController& style() { return controller.stylizeController(); }
  QString text() const { return session.markdownText().toString(); }
};

bool toggleBoldOn(Editor& editor, const QString& word) {
  const qsizetype start = editor.text().indexOf(word);
  require(start >= 0, "precondition: the word is in the document");
  setSelection(editor.selection(), blockAt(editor.session, 0), start, start + word.size());
  return editor.style().toggleBold();
}

} // namespace

void testStyleCaretFollowsTheVisibleText() {
  Editor editor;
  editor.open(QStringLiteral("alpha"));
  setCursor(editor.selection(), blockAt(editor.session, 0), 5);
  require(editor.style().toggleBold(), "bold should wrap the word");
  require(editor.text() == QStringLiteral("**alpha**"), "bold should wrap the word");
  // The caret sits behind "alpha": offset 5 in the text that is shown, 7 in the Markdown.
  const CursorPosition caret = editor.selection().cursorPosition();
  require(caret.text.textOffset == 5, "the caret should be at the end of the visible word");
  require(caret.text.sourceOffset == 7, "the caret's source offset should sit behind the closing marker");
}

void testStyleSelectionStaysInTheVisibleText() {
  Editor editor;
  editor.open(QStringLiteral("**bold** tail"));
  // "tail" is visible at 5..9, and at 9..13 in the Markdown.
  setSelection(editor.selection(), blockAt(editor.session, 0), 5, 9);
  require(editor.style().toggleItalic(), "italic should wrap the selection");
  require(editor.text() == QStringLiteral("**bold** *tail*"), "italic should wrap the selection");
  const SelectionRange range = editor.selection().selection();
  require(range.startOffset() == 5 && range.endOffset() == 9, "the selection should still be the visible word");
  require(range.anchor.text.sourceOffset == 10 && range.focus.text.sourceOffset == 14,
          "the selection's ends should sit on the word in the Markdown");
}

void testStyleUndoRestoresTheSelection() {
  Editor editor;
  editor.open(QStringLiteral("alpha beta"));
  require(toggleBoldOn(editor, QStringLiteral("alpha")), "bold should wrap the selection");
  require(editor.text() == QStringLiteral("**alpha** beta"), "bold should wrap the selection");

  require(editor.controller.canUndo(), "the command should be one undo step");
  editor.controller.undo();
  require(editor.text() == QStringLiteral("alpha beta"), "undo should restore the text");
  const SelectionRange range = editor.selection().selection();
  require(!range.isCollapsed(), "undo should give the selection back");
  require(range.startOffset() == 0 && range.endOffset() == 5, "the selection should be the word again");
}

void testStyleCommandIsRejectedWhileEditingIsLocked() {
  Editor editor;
  editor.open(QStringLiteral("alpha"));
  setCursor(editor.selection(), blockAt(editor.session, 0), 5);
  editor.session.setEditingReadOnly(true);
  require(!editor.style().toggleBold(), "a locked document should refuse the command");
  require(editor.text() == QStringLiteral("alpha"), "a refused command should not change the text");
  require(!editor.controller.undoStack().canUndo(), "a refused command should not reach the undo stack");
}

// Window-level commands (a spelling correction, an image path after a move or upload) replace a
// range of the source through the facade. They used to change the session directly, which put
// them past the undo stack.
void testReplaceSourceRangeIsOneUndoStep() {
  Editor editor;
  editor.open(QStringLiteral("![alt](images/a.png)\n\nhello world"));
  RenderCommandFacade commands(editor.controller, {});
  const qsizetype path = editor.text().indexOf(QStringLiteral("images/a.png"));

  require(commands.replaceSourceRange(QStringLiteral("Copy Image To..."), path, 12, QStringLiteral("b.png")),
          "the path rewrite should apply");
  require(editor.text() == QStringLiteral("![alt](b.png)\n\nhello world"), "the image path should be rewritten");

  require(editor.controller.canUndo(), "the path rewrite should be one undo step");
  editor.controller.undo();
  require(editor.text() == QStringLiteral("![alt](images/a.png)\n\nhello world"), "undo should restore the path");
  require(editor.controller.canRedo(), "the path rewrite should be redoable");
  editor.controller.redo();
  require(editor.text() == QStringLiteral("![alt](b.png)\n\nhello world"), "redo should rewrite the path again");
}

void testSpellingReplacementIsOneUndoStep() {
  Editor editor;
  editor.open(QStringLiteral("hello world"));
  RenderCommandFacade commands(editor.controller, {});

  require(commands.replaceSourceRange(QStringLiteral("Replace"), 6, 5, QStringLiteral("there")),
          "the replacement should apply");
  require(editor.text() == QStringLiteral("hello there"), "the misspelt word should be replaced");
  require(editor.selection().hasCursor(), "the caret should sit behind the replacement");

  editor.controller.undo();
  require(editor.text() == QStringLiteral("hello world"), "undo should bring the misspelt word back");
  editor.controller.redo();
  require(editor.text() == QStringLiteral("hello there"), "redo should replace the word again");
}

// An edit the slice parser cannot take is applied by re-parsing the whole document. Commands
// used to give up on such an edit instead: the user pressed the key and nothing happened.
void testAnEditTheSliceParserRefusesStillApplies() {
  Editor editor;
  editor.open(QStringLiteral("a\n\n```\ncode\n```\n\nnext"));
  EditorContext context{&editor.session, &editor.selection(), &editor.controller.undoStack(),
                        &editor.controller.brushQueue(), &editor.view, {}};
  // The blank line after the code block and the first character of the paragraph behind it are
  // deleted together, as when a selection that spans the gap is removed. The slice to re-parse
  // would have to start inside the code block's trailing blank line, where no block starts, so
  // the parser cannot take the edit as a slice.
  const EditOutcome outcome = applyEdit(context, {.sourceStart = 16, .removedLength = 2});
  require(outcome.applied, "the edit should apply");
  require(editor.text() == QStringLiteral("a\n\n```\ncode\n```\next"), "the blank line and the 'n' should be gone");
  require(outcome.reparsed, "precondition: this edit should need the full re-parse");
  require(editor.controller.canUndo(), "the edit should be one undo step");
  editor.controller.undo();
  require(editor.text() == QStringLiteral("a\n\n```\ncode\n```\n\nnext"), "undo should restore the blank line");
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testStyleCaretFollowsTheVisibleText);
  RUN_TEST(testStyleSelectionStaysInTheVisibleText);
  RUN_TEST(testStyleUndoRestoresTheSelection);
  RUN_TEST(testStyleCommandIsRejectedWhileEditingIsLocked);
  RUN_TEST(testReplaceSourceRangeIsOneUndoStep);
  RUN_TEST(testSpellingReplacementIsOneUndoStep);
  RUN_TEST(testAnEditTheSliceParserRefusesStillApplies);
#undef RUN_TEST
  return 0;
}
