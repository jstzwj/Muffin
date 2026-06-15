#include "../TestUtils.h"
#include "EditorTestUtils.h"

#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>

using namespace muffin;

namespace {

struct Harness {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;

  Harness() {
    wireInput(input, session, selection, undoStack, brushQueue);
    session.setMarkdownText(QString(), false);
    setCursor(selection, blockAt(session, 0), 0);
  }
};

void typeString(InputController& input, QStringView text) {
  for (QChar ch : text) {
    require(input.insertText(QString(ch)), QStringLiteral("inserting '%1' should succeed").arg(ch));
  }
}

void requireActiveInlineText(const Harness& h, const QString& displayText, const QString& visibleText, const char* label) {
  DocumentLayout layout;
  layout.rebuild(h.session.document(), RenderTheme::github(), 900.0, h.selection.selection());
  const BlockLayout* block = layout.block(blockAt(h.session, 0)->id());
  require(block != nullptr && block->inlineLayout() != nullptr, QStringLiteral("%1 inline layout missing").arg(QString::fromUtf8(label)));
  require(block->inlineLayout()->displayText() == displayText,
          QStringLiteral("%1 display text mismatch: %2").arg(QString::fromUtf8(label), block->inlineLayout()->displayText()));
  require(block->inlineLayout()->visibleText() == visibleText,
          QStringLiteral("%1 visible text mismatch: %2").arg(QString::fromUtf8(label), block->inlineLayout()->visibleText()));
}

}  // namespace

void testLoneHeadingMarkersStayParagraph() {
  for (int level = 1; level <= 6; ++level) {
    Harness h;
    const QString marker = QStringLiteral("#").repeated(level);
    typeString(h.input, marker);
    require(blockAt(h.session, 0)->type() == BlockType::Paragraph,
            QStringLiteral("lone '%1' should stay paragraph").arg(marker));
    require(h.session.markdownText() == marker, QStringLiteral("text should remain '%1'").arg(marker));
    requireActiveInlineText(h, marker, marker, "lone heading marker");
  }
}

void testHeadingWithoutSpaceStaysParagraph() {
  Harness h;
  typeString(h.input, QStringLiteral("###123"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "'###123' should stay paragraph");
  require(h.session.markdownText() == QStringLiteral("###123"), "text should be '###123'");
  requireActiveInlineText(h, QStringLiteral("###123"), QStringLiteral("###123"), "heading without space");
}

// An active heading renders content-only: its `### ` prefix is never shown. The underlying markdown
// still preserves the prefix (serialization is unchanged); only the rendered projection drops it.
void testHeadingWithSpaceHidesPrefixWhenActive() {
  Harness h;
  typeString(h.input, QStringLiteral("### 123"));
  require(blockAt(h.session, 0)->type() == BlockType::Heading, "'### 123' should parse as heading");
  require(blockAt(h.session, 0)->headingLevel() == 3, "'### 123' should be h3");
  require(h.session.markdownText() == QStringLiteral("### 123"), "heading markdown should be preserved");
  requireActiveInlineText(h, QStringLiteral("123"), QStringLiteral("123"), "active heading");
}

// A lone `*` (no trailing space) stays a paragraph; only `* ` becomes a list.
void testLoneBulletStaysParagraphUntilSpace() {
  Harness h;
  require(h.input.insertText(QStringLiteral("*")), "insert lone bullet");
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '*' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("*"), "text should be '*'");

  require(h.input.insertText(QStringLiteral(" ")), "insert trailing space");
  require(blockAt(h.session, 0)->type() == BlockType::List, "'* ' should become a list");
  require(h.session.markdownText() == QStringLiteral("* "), "text should be '* '");
}

void testLoneOrderedMarkerStaysParagraphUntilSpace() {
  Harness h;
  typeString(h.input, QStringLiteral("1."));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '1.' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("1."), "text should be '1.'");

  require(h.input.insertText(QStringLiteral(" ")), "insert trailing space");
  require(blockAt(h.session, 0)->type() == BlockType::List, "'1. ' should become an ordered list");
}

void testAllListMarkersStayParagraphUntilSpace() {
  const QVector<QString> markers{
      QStringLiteral("*"),
      QStringLiteral("-"),
      QStringLiteral("+"),
      QStringLiteral("1."),
      QStringLiteral("1)")};
  for (const QString& marker : markers) {
    Harness h;
    typeString(h.input, marker);
    require(blockAt(h.session, 0)->type() == BlockType::Paragraph,
            QStringLiteral("lone list marker '%1' should stay paragraph").arg(marker));
    require(h.session.markdownText() == marker, QStringLiteral("text should remain '%1'").arg(marker));

    require(h.input.insertText(QStringLiteral(" ")), QStringLiteral("space after '%1' should succeed").arg(marker));
    require(blockAt(h.session, 0)->type() == BlockType::List,
            QStringLiteral("'%1 ' should become list").arg(marker));
  }
}

void testLoneBulletWithTextStaysParagraphUntilSpace() {
  Harness h;
  typeString(h.input, QStringLiteral("*x"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "'*x' has no space, should stay paragraph");
  require(h.session.markdownText() == QStringLiteral("*x"), "text should be '*x'");
}

// A fenced code opener (``` or ~~~) stays a paragraph while being typed.
void testLoneFenceStaysParagraph() {
  Harness h;
  typeString(h.input, QStringLiteral("```"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '```' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("```"), "text should be '```'");

  Harness tilde;
  typeString(tilde.input, QStringLiteral("~~~"));
  require(blockAt(tilde.session, 0)->type() == BlockType::Paragraph, "lone '~~~' should stay a paragraph");
}

void testFencePrefixesStayParagraphWhileTyping() {
  const QVector<QString> samples{
      QStringLiteral("```"),
      QStringLiteral("~~~"),
      QStringLiteral("``` js"),
      QStringLiteral("~~~ ```")};
  for (const QString& sample : samples) {
    Harness h;
    typeString(h.input, sample);
    require(blockAt(h.session, 0)->type() == BlockType::Paragraph,
            QStringLiteral("pending fence '%1' should stay paragraph").arg(sample));
    require(h.session.markdownText() == sample, QStringLiteral("text should remain '%1'").arg(sample));
    requireActiveInlineText(h, sample, sample, "pending fence");
  }
}

void testLoneFenceWithLanguageStaysParagraph() {
  Harness h;
  typeString(h.input, QStringLiteral("```js"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '```js' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("```js"), "text should be '```js'");
}

// A display-math opener ($$) stays a paragraph while being typed.
void testLoneDollarMathStaysParagraph() {
  Harness h;
  typeString(h.input, QStringLiteral("$$"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '$$' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("$$"), "text should be '$$'");
}

void testDollarMathPrefixStates() {
  Harness oneDollar;
  typeString(oneDollar.input, QStringLiteral("$"));
  require(blockAt(oneDollar.session, 0)->type() == BlockType::Paragraph, "single '$' should stay paragraph");
  require(oneDollar.session.markdownText() == QStringLiteral("$"), "single dollar text mismatch");

  Harness twoDollar;
  typeString(twoDollar.input, QStringLiteral("$$"));
  require(blockAt(twoDollar.session, 0)->type() == BlockType::Paragraph, "'$$' should stay paragraph until Enter");
  require(twoDollar.session.markdownText() == QStringLiteral("$$"), "double dollar text mismatch");
  requireActiveInlineText(twoDollar, QStringLiteral("$$"), QStringLiteral("$$"), "pending dollar math");
}

// A LaTeX display-math opener (\[) stays a paragraph while being typed.
void testLoneBracketMathStaysParagraph() {
  Harness h;
  typeString(h.input, QStringLiteral("\\["));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "lone '\\[' should stay a paragraph");
  require(h.session.markdownText() == QStringLiteral("\\["), "text should be '\\['");
}

void testBracketMathPrefixStates() {
  Harness slash;
  typeString(slash.input, QStringLiteral("\\"));
  require(blockAt(slash.session, 0)->type() == BlockType::Paragraph, "single backslash should stay paragraph");
  require(slash.session.markdownText() == QStringLiteral("\\"), "single backslash text mismatch");

  Harness bracket;
  typeString(bracket.input, QStringLiteral("\\["));
  require(blockAt(bracket.session, 0)->type() == BlockType::Paragraph, "'\\[' should stay paragraph until Enter");
  require(bracket.session.markdownText() == QStringLiteral("\\["), "bracket opener text mismatch");
  requireActiveInlineText(bracket, QStringLiteral("\\["), QStringLiteral("\\["), "pending bracket math");
}

// Full-document parses (load) must NOT be demoted: a real closed fence stays a code block.
void testRealCodeBlockLoadedIsCodeFence() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("```\ncode\n```"), false);
  setCursor(h.selection, blockAt(h.session, 0), 0);
  require(blockAt(h.session, 0)->type() == BlockType::CodeFence, "loaded closed fence should be a code block");
}

void testRealMathBlockLoadedIsMathBlock() {
  Harness h;
  h.session.setMarkdownText(QStringLiteral("$$\ny = x\n$$"), false);
  setCursor(h.selection, blockAt(h.session, 0), 0);
  require(blockAt(h.session, 0)->type() == BlockType::MathBlock, "loaded math block should stay a math block");
  h.session.setMarkdownText(QStringLiteral("\\[\ny = x\n\\]"), false);
  setCursor(h.selection, blockAt(h.session, 0), 0);
  require(blockAt(h.session, 0)->type() == BlockType::MathBlock, "loaded bracket math should be a math block");
  require(blockAt(h.session, 0)->mathDelimiter() == MathDelimiter::Bracket, "loaded bracket math delimiter flag missing");
}

// Pressing Enter on a paragraph that is exactly a fence/math opener commits it to a block.
void testEnterOnFenceConvertsToCodeBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("```"));
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "precondition: '```' is a paragraph");
  require(h.input.insertParagraphBreak(), "enter on '```' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::CodeFence, "'```' + Enter should become a code block");
  require(h.session.markdownText().contains(QStringLiteral("```\n\n```")), "markdown should contain a code fence");
}

void testEnterOnFenceWithLanguageConvertsToCodeBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("```js"));
  require(h.input.insertParagraphBreak(), "enter on '```js' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::CodeFence, "'```js' + Enter should become a code block");
  require(blockAt(h.session, 0)->codeLanguage() == QStringLiteral("js"), "code block language should be 'js'");
}

void testEnterOnFenceWithSpacedLanguageConvertsToCodeBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("``` js"));
  require(h.input.insertParagraphBreak(), "enter on '``` js' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::CodeFence, "'``` js' + Enter should become a code block");
  require(blockAt(h.session, 0)->codeLanguage() == QStringLiteral("js"), "code block language should trim to 'js'");
}

void testEnterOnTildeFenceConvertsToCodeBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("~~~"));
  require(h.input.insertParagraphBreak(), "enter on '~~~' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::CodeFence, "'~~~' + Enter should become a code block");

  Harness info;
  typeString(info.input, QStringLiteral("~~~ ```"));
  require(blockAt(info.session, 0)->type() == BlockType::Paragraph, "tilde fence with backtick info should stay pending");
  require(info.input.insertParagraphBreak(), "enter on tilde fence with backtick info should succeed");
  require(blockAt(info.session, 0)->type() == BlockType::CodeFence, "tilde fence with backtick info should become a code block");
}

void testEnterOnDollarMathConvertsToMathBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("$$"));
  require(h.input.insertParagraphBreak(), "enter on '$$' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::MathBlock, "'$$' + Enter should become a math block");
}

void testEnterOnBracketMathConvertsToMathBlock() {
  Harness h;
  typeString(h.input, QStringLiteral("\\["));
  require(h.input.insertParagraphBreak(), "enter on '\\[' should succeed");
  require(blockAt(h.session, 0)->type() == BlockType::MathBlock, "'\\[' + Enter should become a math block");
  require(blockAt(h.session, 0)->mathDelimiter() == MathDelimiter::Bracket, "bracket math delimiter flag missing");
  require(h.session.markdownText().contains(QStringLiteral("\\[\n\n\\]")), "markdown should contain a bracket math block");
}

// The edit-driven full-reparse fallback (applyMarkdownText with an offset) must still demote a
// pending marker, even though it bypasses the local-edit path that normally demotes. This is the
// fix for "typing ### sometimes snaps to a heading" when the local edit is rejected.
void testApplyMarkdownTextDemotesHeadingMarkerAtOffset() {
  Harness h;
  h.session.applyMarkdownText(QStringLiteral("###"), true, {0});
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph,
          "applyMarkdownText with offset 0 should demote lone '###' to paragraph");
  require(h.session.markdownText() == QStringLiteral("###"), "markdown should remain '###'");
}

// Without an offset, applyMarkdownText must NOT demote (default off) so load / undo / find /
// import keep producing real structural blocks.
void testApplyMarkdownTextWithoutOffsetCommitsHeading() {
  Harness h;
  h.session.applyMarkdownText(QStringLiteral("###"), true);
  require(blockAt(h.session, 0)->type() == BlockType::Heading,
          "applyMarkdownText without offset must NOT demote (default off)");
}

// The same text through load (setMarkdownText) commits a heading, while the edit fallback demotes.
void testLoadDoesNotDemoteButEditFallbackDoes() {
  Harness loaded;
  loaded.session.setMarkdownText(QStringLiteral("###"), false);
  setCursor(loaded.selection, blockAt(loaded.session, 0), 0);
  require(blockAt(loaded.session, 0)->type() == BlockType::Heading,
          "loading a lone '###' must produce a real heading (no demotion on load)");

  Harness edited;
  edited.session.applyMarkdownText(QStringLiteral("###"), true, {0});
  require(blockAt(edited.session, 0)->type() == BlockType::Paragraph,
          "edit-fallback with offset must demote lone '###' to paragraph");
}

// All pending marker kinds demote on the edit fallback, not just headings.
void testApplyMarkdownTextDemotesListFenceMathMarkers() {
  Harness listH;
  listH.session.applyMarkdownText(QStringLiteral("-"), true, {0});
  require(blockAt(listH.session, 0)->type() == BlockType::Paragraph, "fallback should demote lone '-' to paragraph");

  Harness fenceH;
  fenceH.session.applyMarkdownText(QStringLiteral("```"), true, {0});
  require(blockAt(fenceH.session, 0)->type() == BlockType::Paragraph, "fallback should demote lone '```' to paragraph");

  Harness mathH;
  mathH.session.applyMarkdownText(QStringLiteral("$$"), true, {0});
  require(blockAt(mathH.session, 0)->type() == BlockType::Paragraph, "fallback should demote lone '$$' to paragraph");

  Harness bracketH;
  bracketH.session.applyMarkdownText(QStringLiteral("\\["), true, {0});
  require(blockAt(bracketH.session, 0)->type() == BlockType::Paragraph, "fallback should demote lone bracket-math opener to paragraph");
}

// Carrying a set of offsets (not a single offset) must demote every marker whose offset is
// supplied, and leave the others committed. The "leave others committed" half is what lets a
// loaded lone heading survive an edit-driven replay (it is never a paragraph, so its offset is
// never collected into the set).
void testApplyMarkdownTextDemotesAllPendingMarkers() {
  Harness h;
  // Three lone ATX openers parse as three (empty) headings without demotion.
  h.session.setMarkdownText(QStringLiteral("###\n\n###\n\n###"), false);
  setCursor(h.selection, blockAt(h.session, 0), 0);
  require(blockAt(h.session, 0)->type() == BlockType::Heading, "precondition: lone '###' loads as a heading");
  require(blockAt(h.session, 1)->type() == BlockType::Heading, "precondition: second lone '###' loads as a heading");
  require(blockAt(h.session, 2)->type() == BlockType::Heading, "precondition: third lone '###' loads as a heading");

  // Demote the first (offset 0) and third (offset 10) only; the second (offset 5) is omitted.
  h.session.applyMarkdownText(QStringLiteral("###\n\n###\n\n###"), true, {0, 10});
  require(blockAt(h.session, 0)->type() == BlockType::Paragraph, "offset 0 should demote to paragraph");
  require(blockAt(h.session, 1)->type() == BlockType::Heading, "offset 5 was not supplied, must stay a heading");
  require(blockAt(h.session, 2)->type() == BlockType::Paragraph, "offset 10 should demote to paragraph");
}

void testApplyMarkdownTextDemotesPendingMarkersInContainers() {
  Harness quote;
  quote.session.applyMarkdownText(QStringLiteral("> ###"), true, {2});
  MarkdownNode* quoteBlock = blockAt(quote.session, 0);
  require(quoteBlock->type() == BlockType::BlockQuote, "quote wrapper should remain block quote");
  MarkdownNode* quoteParagraph = firstChildOfType(quoteBlock, BlockType::Paragraph);
  require(quoteParagraph != nullptr, "quote pending marker should demote to paragraph");
  require(quoteParagraph->inlines().size() == 1 && quoteParagraph->inlines().first().text() == QStringLiteral("###"),
          "quote pending marker paragraph text mismatch");

  Harness list;
  list.session.applyMarkdownText(QStringLiteral("- ###"), true, {2});
  MarkdownNode* listBlock = blockAt(list.session, 0);
  require(listBlock->type() == BlockType::List, "list wrapper should remain list");
  MarkdownNode* listItem = childAt(listBlock, 0);
  MarkdownNode* listParagraph = firstChildOfType(listItem, BlockType::Paragraph);
  require(listParagraph != nullptr, "list-item pending marker should demote to paragraph");
  require(listParagraph->inlines().size() == 1 && listParagraph->inlines().first().text() == QStringLiteral("###"),
          "list-item pending marker paragraph text mismatch");

  Harness math;
  math.session.applyMarkdownText(QStringLiteral("> $$"), true, {2});
  MarkdownNode* mathParagraph = firstChildOfType(blockAt(math.session, 0), BlockType::Paragraph);
  require(mathParagraph != nullptr, "quote pending math marker should demote to paragraph");
  require(mathParagraph->inlines().size() == 1 && mathParagraph->inlines().first().text() == QStringLiteral("$$"),
          "quote pending math paragraph text mismatch");
}

void requireUndoRedoPendingMarkerCommit(const QString& marker, BlockType committedType, MathDelimiter mathDelimiter, const char* label) {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QString(), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);
  typeString(controller.inputController(), marker);
  require(blockAt(session, 0)->type() == BlockType::Paragraph,
          QStringLiteral("%1 precondition: typed marker should be pending paragraph").arg(QString::fromUtf8(label)));

  require(controller.inputController().insertParagraphBreak(),
          QStringLiteral("%1 enter should commit pending marker").arg(QString::fromUtf8(label)));
  require(blockAt(session, 0)->type() == committedType,
          QStringLiteral("%1 redo target type mismatch after enter").arg(QString::fromUtf8(label)));
  if (committedType == BlockType::MathBlock) {
    require(blockAt(session, 0)->mathDelimiter() == mathDelimiter,
            QStringLiteral("%1 math delimiter mismatch after enter").arg(QString::fromUtf8(label)));
  }

  controller.undo();
  require(session.markdownText() == marker, QStringLiteral("%1 undo should restore marker text").arg(QString::fromUtf8(label)));
  require(blockAt(session, 0)->type() == BlockType::Paragraph,
          QStringLiteral("%1 undo should restore pending paragraph state").arg(QString::fromUtf8(label)));

  controller.redo();
  require(blockAt(session, 0)->type() == committedType,
          QStringLiteral("%1 redo should restore committed block").arg(QString::fromUtf8(label)));
  if (committedType == BlockType::MathBlock) {
    require(blockAt(session, 0)->mathDelimiter() == mathDelimiter,
            QStringLiteral("%1 math delimiter mismatch after redo").arg(QString::fromUtf8(label)));
  }
}

void testPendingMarkerCommitUndoRedo() {
  requireUndoRedoPendingMarkerCommit(QStringLiteral("$$"), BlockType::MathBlock, MathDelimiter::Dollar, "dollar math");
  requireUndoRedoPendingMarkerCommit(QStringLiteral("\\["), BlockType::MathBlock, MathDelimiter::Bracket, "bracket math");
  requireUndoRedoPendingMarkerCommit(QStringLiteral("```"), BlockType::CodeFence, MathDelimiter::Dollar, "code fence");
}

// Non-empty document: press Enter to create a new paragraph, then type a lone bullet.
// This tests the editParagraph → buildTextEdit → applyLocalEdit path (NOT insertIntoEmptyDocument).
void testLoneBulletStaysParagraphAfterSplit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("hello"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "Enter after 'hello' should succeed");

  const auto& children = session.document().root().children();
  require(children.size() == 2, "should have 2 blocks after Enter");
  require(blockAt(session, 1)->type() == BlockType::Paragraph, "new block should be a paragraph");

  // Cursor is now in the new empty paragraph. Type a lone bullet.
  require(input.insertText(QStringLiteral("*")), "insert '*' in new paragraph should succeed");
  require(blockAt(session, 1)->type() == BlockType::Paragraph,
          "lone '*' in non-empty document should stay a paragraph");
  require(session.markdownText().endsWith(QLatin1Char('*')),
          "markdown should end with '*'");

  // Add a trailing space → commits to list.
  require(input.insertText(QStringLiteral(" ")), "insert space after '*' should succeed");
  require(blockAt(session, 1)->type() == BlockType::List,
          "'* ' in non-empty document should become a list");
}

// Same as above but for ordered marker.
void testLoneOrderedMarkerStaysParagraphAfterSplit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("hello"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "Enter after 'hello' should succeed");

  require(input.insertText(QStringLiteral("1.")), "insert '1.' in new paragraph should succeed");
  require(blockAt(session, 1)->type() == BlockType::Paragraph,
          "lone '1.' in non-empty document should stay a paragraph");

  require(input.insertText(QStringLiteral(" ")), "insert space after '1.' should succeed");
  require(blockAt(session, 1)->type() == BlockType::List,
          "'1. ' in non-empty document should become a list");
}

// Heading marker in non-empty document.
// Typing a pending marker in a virtual empty paragraph between blocks should
// stay as a Paragraph.  This tests the slice-expansion fix: without it,
// chooseTopLevelSlice expands the virtual empty paragraph into the following
// List block, cmark absorbs "*" into the list structure, and demotion fails
// because the multi-line list source doesn't match the single-line marker regex.
void testLoneBulletStaysParagraphBetweenBlocks() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Create a document with a paragraph, two blank lines, then a list.
  // Two blank lines create a virtual empty paragraph between them.
  session.setMarkdownText(QStringLiteral("intro\n\n\n- item1\n- item2"), false);

  // Root children: [Paragraph("intro"), VirtualEmptyParagraph, List]
  const auto& children = session.document().root().children();
  require(children.size() >= 3, "should have at least 3 blocks (intro, virtual paragraph, list)");
  require(children.at(1)->type() == BlockType::Paragraph, "second block should be the virtual empty paragraph");

  // Place cursor in the virtual empty paragraph and type "*"
  setCursor(selection, blockAt(session, 1), 0);
  require(input.insertText(QStringLiteral("*")), "insert '*' in virtual empty paragraph should succeed");
  require(blockAt(session, 1)->type() == BlockType::Paragraph,
          "lone '*' between blocks should stay a paragraph, not become a list");
  // The '*' is inserted in the virtual empty paragraph's position.
  // It should appear before the list in the source, not merged into it.
  require(session.markdownText().contains(QLatin1Char('*')),
          "markdown should contain the typed '*'");

  // Now add a space → should become a list
  require(input.insertText(QStringLiteral(" ")), "insert space after '*' should succeed");
  // The "* " is now a list item, so we should have two lists or a merged list
  require(session.markdownText().contains(QLatin1String("* ")), "markdown should contain '* '");
}

void testLoneBulletStaysParagraphBetweenHeadingAndList() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // User's exact scenario: heading, paragraph, two blank lines, then list
  session.setMarkdownText(
      QStringLiteral("## Lists\n\nUnordered list:\n\n\n- First item\n- Second item with **bold** text"),
      false);

  // Find the virtual empty paragraph between "Unordered list:" and the list
  const auto& children = session.document().root().children();
  require(children.size() >= 4, "should have heading, paragraph, virtual paragraph, list");
  // Children: [Heading, Paragraph("Unordered list:"), VirtualEmptyParagraph, List]
  MarkdownNode* virtualPara = nullptr;
  for (size_t i = 0; i < children.size(); ++i) {
    const auto& ch = children.at(i);
    if (ch->type() == BlockType::Paragraph && ch->sourceRange().byteStart == ch->sourceRange().byteEnd) {
      virtualPara = ch.get();
      break;
    }
  }
  require(virtualPara != nullptr, "should find a virtual empty paragraph");

  // Place cursor in it and type "*"
  setCursor(selection, virtualPara, 0);
  require(input.insertText(QStringLiteral("*")), "insert '*' between blocks should succeed");
  // Look up the block fresh — the edit may have replaced the node
  MarkdownNode* blockAfterEdit = session.document().node(selection.cursorPosition().blockId);
  require(blockAfterEdit != nullptr && blockAfterEdit->type() == BlockType::Paragraph,
          "lone '*' between heading and list should stay a paragraph");
}

void testAllMarkersStayParagraphBetweenBlocks() {
  const QVector<QString> markers{
      QStringLiteral("*"),
      QStringLiteral("-"),
      QStringLiteral("+"),
      QStringLiteral("1."),
      QStringLiteral("1)")};
  for (const QString& marker : markers) {
    DocumentSession session;
    SelectionController selection;
    UndoStack undoStack;
    BrushQueue brushQueue;
    InputController input;
    wireInput(input, session, selection, undoStack, brushQueue);

    session.setMarkdownText(
        QStringLiteral("intro\n\n\n- item1\n- item2"),
        false);
    const auto& ch = session.document().root().children();
    MarkdownNode* vp = nullptr;
    for (size_t i = 0; i < ch.size(); ++i) {
      const auto& c = ch.at(i);
      if (c->type() == BlockType::Paragraph && c->sourceRange().byteStart == c->sourceRange().byteEnd) {
        vp = c.get();
        break;
      }
    }
    require(vp != nullptr, "should find virtual paragraph");
    setCursor(selection, vp, 0);
    typeString(input, marker);
    MarkdownNode* after = session.document().node(selection.cursorPosition().blockId);
    require(after != nullptr && after->type() == BlockType::Paragraph,
            QStringLiteral("lone '%1' between blocks should stay paragraph").arg(marker));
  }
}

void testLoneHeadingStaysParagraphAfterSplit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("hello"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "Enter after 'hello' should succeed");

  typeString(input, QStringLiteral("###"));
  require(blockAt(session, 1)->type() == BlockType::Paragraph,
          "'###' in non-empty document should stay a paragraph");
}

// A lone backtick has no closing partner, so it is plain inline text — never a code span. It must
// render exactly once in both the display and visible projections (regression: it was drawn twice
// because cmark-gfm reports a degenerate zero-width sourcepos for it). Cover several positions so
// the degenerate-range repair in the parser adapter is exercised and does not over-fire.
void testLoneBacktickRendersOnce() {
  {
    Harness h;
    typeString(h.input, QStringLiteral("`"));
    require(h.session.markdownText() == QStringLiteral("`"), "lone backtick source should be a single backtick");
    requireActiveInlineText(h, QStringLiteral("`"), QStringLiteral("`"), "lone backtick");
  }
  {
    Harness h;
    typeString(h.input, QStringLiteral("a`"));
    requireActiveInlineText(h, QStringLiteral("a`"), QStringLiteral("a`"), "trailing lone backtick");
  }
  {
    Harness h;
    typeString(h.input, QStringLiteral("`a"));
    requireActiveInlineText(h, QStringLiteral("`a"), QStringLiteral("`a"), "leading lone backtick");
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testLoneHeadingMarkersStayParagraph();
  testHeadingWithoutSpaceStaysParagraph();
  testHeadingWithSpaceHidesPrefixWhenActive();
  testLoneBulletStaysParagraphUntilSpace();
  testLoneOrderedMarkerStaysParagraphUntilSpace();
  testAllListMarkersStayParagraphUntilSpace();
  testLoneBulletWithTextStaysParagraphUntilSpace();
  testLoneFenceStaysParagraph();
  testFencePrefixesStayParagraphWhileTyping();
  testLoneFenceWithLanguageStaysParagraph();
  testLoneDollarMathStaysParagraph();
  testDollarMathPrefixStates();
  testLoneBracketMathStaysParagraph();
  testBracketMathPrefixStates();
  testRealCodeBlockLoadedIsCodeFence();
  testRealMathBlockLoadedIsMathBlock();
  testEnterOnFenceConvertsToCodeBlock();
  testEnterOnFenceWithLanguageConvertsToCodeBlock();
  testEnterOnFenceWithSpacedLanguageConvertsToCodeBlock();
  testEnterOnTildeFenceConvertsToCodeBlock();
  testEnterOnDollarMathConvertsToMathBlock();
  testEnterOnBracketMathConvertsToMathBlock();
  testLoneBacktickRendersOnce();
  testApplyMarkdownTextDemotesHeadingMarkerAtOffset();
  testApplyMarkdownTextWithoutOffsetCommitsHeading();
  testLoadDoesNotDemoteButEditFallbackDoes();
  testApplyMarkdownTextDemotesListFenceMathMarkers();
  testApplyMarkdownTextDemotesAllPendingMarkers();
  testApplyMarkdownTextDemotesPendingMarkersInContainers();
  testPendingMarkerCommitUndoRedo();
  testLoneBulletStaysParagraphAfterSplit();
  testLoneOrderedMarkerStaysParagraphAfterSplit();
  testLoneHeadingStaysParagraphAfterSplit();
  testLoneBulletStaysParagraphBetweenBlocks();
  testLoneBulletStaysParagraphBetweenHeadingAndList();
  testAllMarkersStayParagraphBetweenBlocks();
  return 0;
}
