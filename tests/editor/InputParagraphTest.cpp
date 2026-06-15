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

// testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph (lines 435-537)
void testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  QVector<BrushQueue::RefreshRequest> requests;
  QObject::connect(&brushQueue, &BrushQueue::refreshRequested, [&requests](BrushQueue::RefreshRequest request) {
    requests.push_back(std::move(request));
  });
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId originalAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "paragraph start enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph start enter should create virtual empty block");
  require(session.lastLocalEditChangedTopLevelStructure(), "paragraph start enter should mark top-level structure changed");
  brushQueue.flush();
  require(requests.size() == 1, "paragraph start enter should request one refresh");
  require(!requests.last().fullLayoutDirty, "paragraph start enter should not require full layout refresh");
  require(requests.last().topLevelRangeDirty.isValid(), "paragraph start enter should request top-level range refresh");
  require(requests.last().topLevelRangeDirty.first == 0, "paragraph start enter range first mismatch");
  require(requests.last().topLevelRangeDirty.oldCount == 1, "paragraph start enter range old count mismatch");
  require(requests.last().topLevelRangeDirty.newCount == 2, "paragraph start enter range new count mismatch");
  require(requests.last().layoutDirtyBlocks.isEmpty(), "paragraph start enter range refresh should not keep block dirty ids");
  require(blockAt(session, 1)->id() == originalAlphaId, "paragraph start enter should preserve original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\nxalpha"), "typing after paragraph start enter should insert at original paragraph start");
  require(!session.lastLocalEditChangedTopLevelStructure(), "typing after paragraph start enter should keep original paragraph identity stable");
  brushQueue.flush();
  require(requests.size() == 2, "typing after paragraph start enter should request another refresh");
  require(!requests.last().fullLayoutDirty, "typing after paragraph start enter should request block refresh");
  require(requests.last().layoutDirtyBlocks.size() == 1, "typing after paragraph start enter should dirty one block");
  require(requests.last().layoutDirtyBlocks.contains(blockAt(session, 1)->id()), "typing after paragraph start enter should dirty original paragraph");
  require(blockAt(session, 1)->id() == originalAlphaId, "typing after paragraph start enter should keep original paragraph id");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId repeatedAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph for repeat case");
  require(input.insertParagraphBreak(), "repeated enter in leading empty paragraph should create another empty paragraph");
  require(input.insertParagraphBreak(), "third enter in leading empty paragraph should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\n\n\nalpha"), "leading empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 4, "leading empty paragraph repeated enter should create virtual blocks");
  require(blockAt(session, 3)->id() == repeatedAlphaId, "repeated paragraph start enter should keep original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 3)->id(), "repeated paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "repeated paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("123")), "typing after repeated paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\n\n\n123alpha"), "typing after repeated paragraph start enter text mismatch");
  require(session.document().root().children().size() == 4, "typing after repeated paragraph start enter should preserve virtual paragraph count");
  require(blockAt(session, 3)->id() == repeatedAlphaId, "typing after repeated paragraph start enter should keep original paragraph once");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "enter at paragraph end should create empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n"), "paragraph end enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph end enter should create virtual empty block");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph end enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph end enter cursor offset mismatch");
  require(input.insertParagraphBreak(), "enter in trailing empty paragraph should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\n"), "trailing empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 3, "trailing empty paragraph repeated enter should create another virtual block");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(), "trailing empty paragraph repeated enter cursor block mismatch");
  require(input.insertText(QStringLiteral("after")), "typing into trailing empty paragraph should work");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\nafter"), "typing into trailing empty paragraph text mismatch");

  session.setMarkdownText(QString(12, QLatin1Char('\n')), false);
  const qsizetype emptyRunCount = session.document().root().children().size();
  const NodeId thirdEmptyId = blockAt(session, 2)->id();
  const NodeId fourthEmptyId = blockAt(session, 3)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.insertParagraphBreak(), "enter in middle empty paragraph should create immediate empty paragraph");
  require(session.markdownText() == QString(14, QLatin1Char('\n')), "middle empty paragraph enter text mismatch");
  require(session.document().root().children().size() == emptyRunCount + 1, "middle empty paragraph enter block count mismatch");
  require(blockAt(session, 2)->id() == thirdEmptyId, "middle empty paragraph enter should preserve current empty id");
  require(blockAt(session, 4)->id() == fourthEmptyId, "middle empty paragraph enter should shift old next empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 3)->id(), "middle empty paragraph enter cursor should move to inserted empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "middle empty paragraph enter cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("alpha") + QString(13, QLatin1Char('\n')) + QStringLiteral("beta"), false);
  const qsizetype surroundedRunCount = session.document().root().children().size();
  const NodeId alphaId = blockAt(session, 0)->id();
  const NodeId surroundedThirdEmptyId = blockAt(session, 3)->id();
  const NodeId surroundedFourthEmptyId = blockAt(session, 4)->id();
  const NodeId betaId = blockAt(session, 7)->id();
  setCursor(selection, blockAt(session, 3), 0);
  require(input.insertParagraphBreak(), "enter in surrounded middle empty paragraph should create immediate empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha") + QString(15, QLatin1Char('\n')) + QStringLiteral("beta"),
          "surrounded middle empty paragraph enter text mismatch");
  require(session.document().root().children().size() == surroundedRunCount + 1,
          "surrounded middle empty paragraph enter block count mismatch");
  require(blockAt(session, 0)->id() == alphaId, "surrounded middle empty paragraph enter should preserve alpha id");
  require(blockAt(session, 3)->id() == surroundedThirdEmptyId, "surrounded middle empty paragraph enter should preserve current empty id");
  require(blockAt(session, 5)->id() == surroundedFourthEmptyId, "surrounded middle empty paragraph enter should shift old next empty paragraph");
  require(blockAt(session, 8)->id() == betaId, "surrounded middle empty paragraph enter should preserve beta id");
  require(selection.cursorPosition().blockId == blockAt(session, 4)->id(),
          "surrounded middle empty paragraph enter cursor should move to inserted empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "surrounded middle empty paragraph enter cursor offset mismatch");
}

void testBlockQuoteEnterKeepsInsertedBlankLineInsideQuote() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  const QString markdown = QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      ">\n"
      "> It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      "> > Nested quote.");
  session.setMarkdownText(markdown, false);
  MarkdownNode* quote = blockAt(session, 0);
  MarkdownNode* nestedQuote = childAt(quote, 2);
  MarkdownNode* nestedParagraph = childAt(nestedQuote, 0);
  setCursor(selection, nestedParagraph, 13);

  require(input.insertParagraphBreak(), "enter after nested quote paragraph should stay inside quote");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      ">\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      "> > Nested quote.\n"
                                      "> >\n"
                                      "> > "),
          "nested blockquote enter text mismatch");
  require(blockAt(session, 0)->type() == BlockType::BlockQuote, "blockquote should remain one top-level quote");
  require(session.document().root().children().size() == 1, "enter should not create a top-level blank paragraph outside quote");

  require(selection.cursorPosition().text.textOffset == 0, "nested blockquote enter cursor offset mismatch");
  require(undoStack.canUndo(), "blockquote enter should create undo command");
  EditTransaction enterUndo = undoStack.takeUndo();
  require(enterUndo.isSnapshot(), "blockquote structural enter should use snapshot undo");
  require(enterUndo.before().markdownText == markdown, "blockquote enter undo before text mismatch");
  require(enterUndo.after().markdownText == session.markdownText(), "blockquote enter undo after text mismatch");

  session.setMarkdownText(markdown, false);
  quote = blockAt(session, 0);
  MarkdownNode* secondParagraph = childAt(quote, 1);
  setCursor(selection, secondParagraph, QStringLiteral("It can also contain formatting, code, and nested quotes.").size());

  require(input.insertParagraphBreak(), "enter after quote paragraph before nested quote should stay inside quote");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      ">\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      "> \n"
                                      ">\n"
                                      "> > Nested quote."),
          "outer blockquote enter before nested quote text mismatch");
  require(input.insertText(QStringLiteral("Continuation")), "typing after quote enter should edit inserted quote paragraph");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      ">\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      "> Continuation\n"
                                      ">\n"
                                      "> > Nested quote."),
          "typing after outer blockquote enter text mismatch");
}

void testBlockQuoteEmptyParagraphEnterOutdentsQuoteLevel() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  const QString markdown = QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      "> It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      ">\n"
      "> > Nested quote.");
  session.setMarkdownText(markdown, false);
  MarkdownNode* quote = blockAt(session, 0);
  MarkdownNode* emptyParagraph = childAt(quote, 1);
  setCursor(selection, emptyParagraph, 0);

  require(input.insertParagraphBreak(), "enter on empty quote paragraph should outdent quote level");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      "\n"
                                      "\n"
                                      "> > Nested quote."),
          "empty quote paragraph outdent text mismatch");
  require(session.document().root().children().size() == 3, "outdented quote line should create top-level empty paragraph");
  require(input.insertText(QStringLiteral("Plain")), "typing after quote outdent should edit plain paragraph");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      "\n"
                                      "Plain\n"
                                      "> > Nested quote."),
          "typing after quote outdent text mismatch");

  session.setMarkdownText(QStringLiteral("> > alpha\n> >\n> >\n> > beta"), false);
  quote = blockAt(session, 0);
  MarkdownNode* nestedQuote = childAt(quote, 0);
  emptyParagraph = childAt(nestedQuote, 1);
  setCursor(selection, emptyParagraph, 0);

  require(input.insertParagraphBreak(), "enter on nested empty quote paragraph should outdent one quote level");
  require(session.markdownText() == QStringLiteral("> > alpha\n> >\n> \n> \n> > beta"), "nested empty quote paragraph outdent text mismatch");
  require(input.insertText(QStringLiteral("outer")), "typing after nested quote outdent should edit parent quote paragraph");
  require(session.markdownText() == QStringLiteral("> > alpha\n> >\n> \n> outer\n> > beta"), "typing after nested quote outdent text mismatch");
}

void testBlockQuoteBackspaceOutdentsQuoteLevel() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Single level: "> hello" @0 -> "hello" (top-level paragraph), caret at content start.
  session.setMarkdownText(QStringLiteral("> hello"), false);
  MarkdownNode* quote = blockAt(session, 0);
  MarkdownNode* paragraph = childAt(quote, 0);
  setCursor(selection, paragraph, 0);
  require(input.deleteBackward(), "single-level quote backspace should outdent");
  require(session.markdownText() == QStringLiteral("hello"), "single-level quote outdent text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "single-level quote outdent should yield top-level paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "single-level quote outdent cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "single-level quote outdent cursor offset mismatch");
  require(undoStack.canUndo(), "single-level quote outdent should be undoable");
  EditTransaction outdentUndo = undoStack.takeUndo();
  require(outdentUndo.isSnapshot(), "quote outdent should use snapshot undo");
  require(outdentUndo.before().markdownText == QStringLiteral("> hello"), "single-level quote outdent undo before mismatch");

  // Nested: "> > nested" @0 -> "> nested" -> "nested" (two backspaces, caret stays at offset 0).
  session.setMarkdownText(QStringLiteral("> > nested"), false);
  quote = blockAt(session, 0);
  paragraph = childAt(childAt(quote, 0), 0);
  setCursor(selection, paragraph, 0);
  require(input.deleteBackward(), "nested quote backspace should drop one level");
  require(session.markdownText() == QStringLiteral("> nested"), "nested quote outdent text mismatch");
  require(input.deleteBackward(), "outdented quote backspace should exit quote");
  require(session.markdownText() == QStringLiteral("nested"), "nested quote second outdent text mismatch");

  // Three levels: "> > > deep" -> "> > deep" -> "> deep" -> "deep".
  session.setMarkdownText(QStringLiteral("> > > deep"), false);
  quote = blockAt(session, 0);
  paragraph = childAt(childAt(childAt(quote, 0), 0), 0);
  setCursor(selection, paragraph, 0);
  require(input.deleteBackward(), "three-level quote first backspace");
  require(session.markdownText() == QStringLiteral("> > deep"), "three-level quote first outdent mismatch");
  require(input.deleteBackward(), "three-level quote second backspace");
  require(session.markdownText() == QStringLiteral("> deep"), "three-level quote second outdent mismatch");
  require(input.deleteBackward(), "three-level quote third backspace");
  require(session.markdownText() == QStringLiteral("deep"), "three-level quote third outdent mismatch");

  // User's exact scenario: nested quote paragraph outdents one level into the outer quote.
  const QString markdown = QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      "> It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      ">\n"
      "> > Nested quote.");
  session.setMarkdownText(markdown, false);
  quote = blockAt(session, 0);
  MarkdownNode* nestedQuote = childAt(quote, quote->children().size() - 1);
  paragraph = childAt(nestedQuote, 0);
  setCursor(selection, paragraph, 0);
  require(input.deleteBackward(), "nested quote paragraph backspace should outdent one level");
  require(session.markdownText() == QStringLiteral(
                                      "> A block quote can contain paragraphs.\n"
                                      "> It can also contain **formatting**, `code`, and nested quotes.\n"
                                      ">\n"
                                      ">\n"
                                      "> Nested quote."),
          "user scenario quote outdent text mismatch");
  quote = blockAt(session, 0);
  MarkdownNode* outdented = childAt(quote, quote->children().size() - 1);
  require(outdented->type() == BlockType::Paragraph, "outdented nested paragraph should sit directly under outer quote");
  require(selection.cursorPosition().blockId == outdented->id(), "user scenario outdent cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "user scenario outdent cursor offset mismatch");

  // Heading inside a quote: "> # Title" @0 -> "# Title" (quote removed, heading kept) — confirms
  // the outdent branch wins over the heading-to-paragraph branch.
  session.setMarkdownText(QStringLiteral("> # Title"), false);
  quote = blockAt(session, 0);
  MarkdownNode* heading = firstChildOfType(quote, BlockType::Heading);
  setCursor(selection, heading, 0);
  require(input.deleteBackward(), "quoted heading backspace should outdent quote");
  require(session.markdownText() == QStringLiteral("# Title"), "quoted heading outdent text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "quoted heading outdent should keep heading at top level");
}

void testBlockQuoteBackspaceOutdentsMultilineQuoteParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Multi-line marked nested quote: every line of the paragraph drops one level together.
  session.setMarkdownText(QStringLiteral("> > foo\n> > bar"), false);
  MarkdownNode* quote = blockAt(session, 0);
  MarkdownNode* paragraph = childAt(childAt(quote, 0), 0);
  setCursor(selection, paragraph, 0);
  require(input.deleteBackward(), "multiline nested quote backspace should outdent one level");
  require(session.markdownText() == QStringLiteral("> foo\n> bar"), "multiline nested quote outdent text mismatch");

  // Multi-line lazy continuation (line 2 carries no ">"): only line 1 loses its marker,
  // the lazy line is unchanged. Result is robust whether cmark keeps "bar" lazy-in-quote
  // or splits it into a separate paragraph.
  session.setMarkdownText(QStringLiteral("> foo\nbar"), false);
  MarkdownNode* first = blockAt(session, 0);
  if (first->type() == BlockType::BlockQuote) {
    first = childAt(first, 0);
  }
  setCursor(selection, first, 0);
  require(input.deleteBackward(), "multiline lazy quote backspace should outdent");
  require(session.markdownText() == QStringLiteral("foo\nbar"), "multiline lazy quote outdent text mismatch");
}

void testBlockQuoteBackspacePreservesQuoteWhenNotFirstChild() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Non-first child (beta has a previous sibling): backspace merges with the previous quote
  // block, it must NOT outdent beta out of the quote.
  session.setMarkdownText(QStringLiteral("> alpha\n>\n> beta"), false);
  MarkdownNode* quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "baseline should be a block quote");
  MarkdownNode* beta = childAt(quote, quote->children().size() - 1);
  setCursor(selection, beta, 0);
  require(input.deleteBackward(), "non-first quote child backspace should be handled");
  require(firstBlockOfType(session, BlockType::BlockQuote) != nullptr,
          "non-first quote child backspace should not outdent the quote away");
  require(session.markdownText() != QStringLiteral("> alpha\n>\nbeta"),
          "non-first quote child backspace should not drop beta to top level");

  // Mid-block caret: backspace deletes one character, no outdent.
  session.setMarkdownText(QStringLiteral("> hello"), false);
  quote = blockAt(session, 0);
  MarkdownNode* paragraph = childAt(quote, 0);
  setCursor(selection, paragraph, 2);
  require(input.deleteBackward(), "mid-block quote backspace should delete a character");
  require(session.markdownText() == QStringLiteral("> hllo"), "mid-block quote backspace text mismatch");

  // Caret at the start of a lazy-continuation line is a non-zero paragraph offset, so it never
  // reaches the outdent branch: the line-1 quote marker must survive.
  session.setMarkdownText(QStringLiteral("> foo\nbar"), false);
  MarkdownNode* first = blockAt(session, 0);
  if (first->type() == BlockType::BlockQuote) {
    first = childAt(first, 0);
  }
  setCursor(selection, first, 4);
  require(input.deleteBackward(), "lazy-continuation backspace should be handled");
  require(session.markdownText().startsWith(QStringLiteral("> fo")),
          "lazy-continuation backspace should preserve the quote marker on line 1");
}

void testBlockQuoteBackspaceOutdentsEmptyFirstChildQuote() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Empty first-child quote line pops out to the top level (hoist
  // the empty paragraph out; the quote survives with the remaining content). The leading
  // blank ">" lines become outer-level blank lines so no ">" is orphaned, yielding a clean
  // [top-level empty paragraph][quote] split rather than two malformed quotes.
  session.setMarkdownText(QStringLiteral(">\n>\n> text"), false);
  MarkdownNode* quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "baseline should be a block quote");
  MarkdownNode* emptyFirst = childAt(quote, 0);
  require(emptyFirst->type() == BlockType::Paragraph, "first child should be the virtual empty paragraph");
  const NodeId emptyId = emptyFirst->id();
  setCursor(selection, emptyFirst, 0);
  require(input.deleteBackward(), "empty first-child quote backspace should outdent");
  require(session.markdownText() == QStringLiteral("\n\n> text"), "empty first-child outdent text mismatch");
  require(session.document().root().children().size() == 2, "empty first-child outdent should pop empty to top level");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "empty first-child outdent should yield top-level empty paragraph");
  require(blockAt(session, 1)->type() == BlockType::BlockQuote, "empty first-child outdent should keep remaining quote");
  require(blockAt(session, 0)->id() == emptyId, "empty first-child outdent should preserve the empty paragraph identity");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(),
          "empty first-child outdent cursor should stay in the popped empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "empty first-child outdent cursor offset mismatch");

  // Empty first child followed by a nested quote: the empty pops to top level, the nested
  // quote survives intact.
  session.setMarkdownText(QStringLiteral(">\n>\n> > nested"), false);
  quote = blockAt(session, 0);
  emptyFirst = childAt(quote, 0);
  setCursor(selection, emptyFirst, 0);
  require(input.deleteBackward(), "empty first-child before nested quote backspace should outdent");
  require(session.markdownText() == QStringLiteral("\n\n> > nested"), "empty first-child before nested quote outdent text mismatch");
  require(session.document().root().children().size() == 2, "empty first-child before nested quote outdent should pop empty to top level");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "nested-quote case should yield top-level empty paragraph");
  require(blockAt(session, 1)->type() == BlockType::BlockQuote, "nested-quote case should keep the nested quote");

  // Nested empty first child: the inner quote's leading empty drops to the OUTER quote (one
  // level), the inner quote keeps its content. The outer quote now holds [empty, inner quote].
  session.setMarkdownText(QStringLiteral("> >\n> >\n> > beta"), false);
  quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "nested baseline should start with the outer quote");
  MarkdownNode* innerQuote = childAt(quote, 0);
  require(innerQuote->type() == BlockType::BlockQuote, "outer quote should contain the inner quote");
  emptyFirst = childAt(innerQuote, 0);
  require(emptyFirst->type() == BlockType::Paragraph, "inner quote first child should be the virtual empty");
  setCursor(selection, emptyFirst, 0);
  require(input.deleteBackward(), "nested empty first-child backspace should drop one level");
  require(session.document().root().children().size() == 1, "nested empty outdent should keep a single outer quote");
  quote = blockAt(session, 0);
  require(quote->type() == BlockType::BlockQuote, "nested empty outdent should keep the outer quote");
  require(quote->children().size() == 2, "nested empty outdent should leave [empty, inner quote] in the outer quote");
  require(quote->children().at(0)->type() == BlockType::Paragraph, "outer quote first child should be the dropped empty");
  require(quote->children().at(1)->type() == BlockType::BlockQuote, "outer quote second child should be the inner quote");
}

// The three multi-paragraph nested-quote scenarios reported by the user. They all reduce to the
// same rule — the FIRST child of a quote outdents on backspace, a NON-first child merges
// into its predecessor — once you account for cmark splitting a quote at a truly blank line
// (so "> .../<blank>/"> ..." is two sibling quotes, and the second quote's first paragraph is a
// first child and therefore outdents rather than merging).
void testBlockQuoteBackspaceUserScenarios() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // EX1: cursor on "asd" inside a standalone ">> > asd" (a depth-3 quote split off by the blank
  // line above). It is the first (only) child of its quote, so backspace drops one level: ">> asd".
  const QString ex1Before = QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      ">\n"
      "\n"
      ">It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      ">> Nested quote.\n"
      ">>\n"
      ">> \n"
      "\n"
      ">> > asd");
  session.setMarkdownText(ex1Before, false);
  // The "asd" paragraph is root child #2 (the split-off quote) -> BQ -> BQ -> BQ -> P.
  MarkdownNode* deepAsd = childAt(childAt(childAt(blockAt(session, 2), 0), 0), 0);
  require(deepAsd->parent()->previousSibling() == nullptr, "EX1 deep paragraph should be the quote's first child");
  setCursor(selection, deepAsd, 0);
  require(input.deleteBackward(), "EX1 deep nested quote backspace should outdent");
  require(session.markdownText() == QStringLiteral(
                                        "> A block quote can contain paragraphs.\n"
                                        ">\n"
                                        "\n"
                                        ">It can also contain **formatting**, `code`, and nested quotes.\n"
                                        ">\n"
                                        ">> Nested quote.\n"
                                        ">>\n"
                                        ">> \n"
                                        "\n"
                                        ">> asd"),
          "EX1 deep nested quote outdent text mismatch");

  // EX2: cursor on "It can also...", the FIRST paragraph of the second (split-off) quote. It
  // outdents to the top level; the nested quote that followed it stays quoted.
  const QString ex2Before = QStringLiteral(
      "> A block quote can contain paragraphs.\n"
      ">\n"
      "\n"
      ">It can also contain **formatting**, `code`, and nested quotes.\n"
      ">\n"
      ">> Nested quote.\n"
      ">>\n"
      ">> \n"
      ">>\n"
      ">> asd");
  session.setMarkdownText(ex2Before, false);
  // "It can also" is root child #1 (the second quote) -> first child paragraph.
  MarkdownNode* itCanAlso = childAt(blockAt(session, 1), 0);
  require(itCanAlso->previousSibling() == nullptr, "EX2 paragraph should be the first child of its quote");
  setCursor(selection, itCanAlso, 0);
  require(input.deleteBackward(), "EX2 first quote paragraph backspace should outdent to top level");
  require(session.markdownText() == QStringLiteral(
                                        "> A block quote can contain paragraphs.\n"
                                        ">\n"
                                        "\n"
                                        "It can also contain **formatting**, `code`, and nested quotes.\n"
                                        ">\n"
                                        ">> Nested quote.\n"
                                        ">>\n"
                                        ">> \n"
                                        ">>\n"
                                        ">> asd"),
          "EX2 outdent text mismatch");
  require(blockAt(session, 1)->type() == BlockType::Paragraph, "EX2 should leave a top-level paragraph");
  require(firstBlockOfType(session, BlockType::BlockQuote) != nullptr, "EX2 should keep the nested quote");

  // EX3: cursor on "asd", which follows a nested quote and is therefore a NON-first child. It
  // merges into the preceding block's text by direct concatenation (no
  // inserted space), pulling "asd" up into the nested quote.
  session.setMarkdownText(QStringLiteral(">> Nested quote.\n>\n>asd"), false);
  // "asd" is root child #0 (the outer quote) -> second child paragraph (after the inner quote).
  MarkdownNode* trailingAsd = childAt(blockAt(session, 0), 1);
  require(trailingAsd->previousSibling() != nullptr, "EX3 paragraph should NOT be the first child");
  setCursor(selection, trailingAsd, 0);
  require(input.deleteBackward(), "EX3 non-first quote paragraph backspace should merge");
  require(session.markdownText() == QStringLiteral(">> Nested quote.asd"), "EX3 merge text mismatch");
}

// testLocalReparsePreservesUntouchedNodeIds (lines 539-563)
void testLocalReparsePreservesUntouchedNodeIds() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  const NodeId secondId = blockAt(session, 1)->id();
  const NodeId thirdId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertText(QStringLiteral("!")), "local text input should work");
  require(session.markdownText() == QStringLiteral("alpha!\n\nbeta\n\ngamma"), "local input text mismatch");
  require(blockAt(session, 1)->id() == secondId, "local input should preserve untouched second paragraph id");
  require(blockAt(session, 2)->id() == thirdId, "local input should preserve untouched third paragraph id");

  session.setMarkdownText(QStringLiteral("\n\nalpha"), false);
  const NodeId alphaId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("x")), "typing into leading empty paragraph should work");
  require(session.markdownText() == QStringLiteral("x\n\nalpha"), "leading empty paragraph fill text mismatch");
  require(blockAt(session, 1)->id() == alphaId, "filling leading empty paragraph should preserve following paragraph id");
}

// testBlockEditContextSeparatesBlockAndContentRanges (lines 565-580)
void testBlockEditContextSeparatesBlockAndContentRanges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "block edit context should resolve heading");
  require(context.blockRange.byteStart == 0, "heading block range should start at marker");
  require(context.blockRange.byteEnd == 9, "heading block range end mismatch");
  require(context.contentRange.byteStart == 3, "heading content range should skip marker");
  require(context.contentRange.byteEnd == 9, "heading content range end mismatch");
  require(context.contentText == QStringLiteral("Status"), "heading content text mismatch");
}

// testTextBlockCommandBuilderCreatesStructuralEnterCommands (lines 582-653)
void testTextBlockCommandBuilderCreatesStructuralEnterCommands() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "builder test context should resolve heading");

  TextBlockCommandBuilder builder(&session, &resolver);
  TextBlockCommandBuilder::Command before =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(before.valid && before.handled, "builder should produce heading-start enter command");
  require(before.hasLocalEdit(), "builder heading-start command should be local edit");
  require(before.sourceStart == 0, "builder heading-start source start mismatch");
  require(before.removedLength == 0, "builder heading-start removed length mismatch");
  require(before.insertedText == QStringLiteral("\n\n"), "builder heading-start inserted text mismatch");
  require(before.preferredCursor.blockId == blockAt(session, 0)->id(), "builder heading-start cursor should prefer original heading");
  require(before.preferredCursor.text.textOffset == 0, "builder heading-start cursor offset mismatch");
  require(before.fallbackSourceOffset == 2, "builder heading-start fallback source offset mismatch");
  require(!before.preferLaterEmptyAtOffset, "builder heading-start should not prefer inserted empty paragraph");

  setCursor(selection, blockAt(session, 0), 6);
  require(resolver.current(context), "builder heading-end context should resolve");
  TextBlockCommandBuilder::Command after =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(after.valid && after.handled, "builder should produce heading-end enter command");
  require(after.hasLocalEdit(), "builder heading-end command should be local edit");
  require(after.sourceStart == 9, "builder heading-end source start mismatch");
  require(after.removedLength == 0, "builder heading-end removed length mismatch");
  require(after.insertedText == QStringLiteral("\n\n"), "builder heading-end inserted text mismatch");
  require(!after.preferredCursor.isValid(), "builder heading-end should use fallback cursor for new block");

  session.setMarkdownText(QString(12, QLatin1Char('\n')), false);
  const NodeId middleEmptyId = blockAt(session, 2)->id();
  const qsizetype middleEmptyOffset = blockAt(session, 2)->sourceRange().byteEnd;
  setCursor(selection, blockAt(session, 2), 0);
  require(resolver.current(context), "builder middle-empty enter context should resolve");
  TextBlockCommandBuilder::Command middleEmptyEnter =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(middleEmptyEnter.valid && middleEmptyEnter.handled, "builder should produce middle-empty enter command");
  require(middleEmptyEnter.hasLocalEdit(), "builder middle-empty enter should be local edit");
  require(middleEmptyEnter.sourceStart == middleEmptyOffset, "builder middle-empty enter source start mismatch");
  require(middleEmptyEnter.removedLength == 0, "builder middle-empty enter removed length mismatch");
  require(middleEmptyEnter.insertedText == QStringLiteral("\n\n"), "builder middle-empty enter inserted text mismatch");
  require(!middleEmptyEnter.preferredCursor.isValid(), "builder middle-empty enter should use fallback cursor for inserted block");
  require(middleEmptyEnter.fallbackSourceOffset == middleEmptyOffset + 2, "builder middle-empty enter fallback source offset mismatch");
  require(middleEmptyEnter.nodeHints.size() == 1, "builder middle-empty enter should provide one node hint");
  require(middleEmptyEnter.nodeHints.first().nodeId == middleEmptyId, "builder middle-empty enter hint id mismatch");
  require(middleEmptyEnter.nodeHints.first().targetSourceOffset == blockAt(session, 2)->sourceRange().byteStart,
          "builder middle-empty enter hint offset mismatch");
  require(!middleEmptyEnter.preferLaterEmptyAtOffset, "builder middle-empty enter should not prefer later empty fallback");

  session.setMarkdownText(QStringLiteral("\n\n\n\nalpha"), false);
  const NodeId currentEmptyId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 1), 0);
  require(resolver.current(context), "builder empty-empty backspace context should resolve");
  TextBlockCommandBuilder::Command emptyBackspace =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Backspace);
  require(emptyBackspace.valid && emptyBackspace.handled, "builder should produce empty-empty backspace command");
  require(emptyBackspace.hasLocalEdit(), "builder empty-empty backspace should be local edit");
  require(emptyBackspace.insertedText.isEmpty(), "builder empty-empty backspace inserted text mismatch");
  require(emptyBackspace.preferredCursor.blockId == currentEmptyId, "builder empty-empty backspace should prefer current empty paragraph");
  require(emptyBackspace.preferredCursor.text.textOffset == 0, "builder empty-empty backspace cursor offset mismatch");
  require(emptyBackspace.nodeHints.size() == 1, "builder empty-empty backspace should provide one node hint");
  require(emptyBackspace.nodeHints.first().nodeId == currentEmptyId, "builder empty-empty backspace hint id mismatch");
  require(emptyBackspace.nodeHints.first().targetSourceOffset == emptyBackspace.fallbackSourceOffset,
          "builder empty-empty backspace hint offset mismatch");
  require(emptyBackspace.preferLaterEmptyAtOffset, "builder empty-empty backspace should prefer later empty fallback");
}

// testInputMergeParagraphs (lines 655-682)
void testInputMergeParagraphs() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 1), 0);

  require(input.deleteBackward(), "backspace at paragraph start should merge previous paragraph");
  require(session.markdownText() == QStringLiteral("alphabeta"), "backspace merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace merge cursor mismatch");
  EditTransaction mergeUndo = requireTextDeltaCommand(undoStack, "backspace merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "backspace merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(""), "backspace merge inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.deleteForward(), "delete at paragraph end should merge next paragraph");
  require(session.markdownText() == QStringLiteral("alphabeta"), "delete merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete merge cursor mismatch");
  mergeUndo = requireTextDeltaCommand(undoStack, "delete merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "delete merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(""), "delete merge inserted text mismatch");
}

// testInputBackspaceAtParagraphStartDeletesStructuralBoundary (lines 684-753)
void testInputBackspaceAtParagraphStartDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "backspace at document start should be handled as no-op");
  require(session.markdownText() == QStringLiteral("alpha"), "backspace at document start should not change text");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace at document start cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace at document start cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\n\n\nbeta"), false);
  const NodeId betaId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.deleteBackward(), "backspace after empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta"), "backspace empty paragraph boundary mismatch");
  require(blockAt(session, 1)->id() == betaId, "backspace empty paragraph should preserve following paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "backspace empty boundary cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace empty boundary cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n\n\nalpha"), false);
  const NodeId secondEmptyId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace from second empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "backspace second empty paragraph text mismatch");
  require(session.document().root().children().size() == 2, "backspace second empty paragraph block count mismatch");
  require(blockAt(session, 0)->id() == secondEmptyId, "backspace second empty paragraph should preserve current empty id");
  require(selection.hasCursor(), "backspace second empty paragraph should keep cursor");
  require(session.document().node(selection.cursorPosition().blockId) != nullptr, "backspace second empty paragraph cursor block should exist");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace second empty paragraph cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace second empty paragraph cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n\n\n\n\nalpha"), false);
  const NodeId thirdEmptyId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.deleteBackward(), "backspace from third empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("\n\n\n\nalpha"), "backspace third empty paragraph text mismatch");
  require(session.document().root().children().size() == 3, "backspace third empty paragraph block count mismatch");
  require(blockAt(session, 1)->id() == thirdEmptyId, "backspace third empty paragraph should preserve current empty id");
  require(selection.hasCursor(), "backspace third empty paragraph should keep cursor");
  require(session.document().node(selection.cursorPosition().blockId) != nullptr, "backspace third empty paragraph cursor block should exist");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "backspace third empty paragraph cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace third empty paragraph cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace from heading after empty paragraph should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "backspace should preserve current heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "backspace should keep current block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\nbody"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after heading should merge into heading");
  require(session.markdownText() == QStringLiteral("# Titlebody"), "backspace heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold**after"), "backspace complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "backspace complex inline cursor offset mismatch");
}

// testInputDeleteAtParagraphEndDeletesStructuralBoundary (lines 755-784)
void testInputDeleteAtParagraphEndDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\n# Title"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.deleteForward(), "delete before heading should merge heading into paragraph");
  require(session.markdownText() == QStringLiteral("alphaTitle"), "delete heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteForward(), "delete from empty paragraph before heading should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "delete should preserve next heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "delete should keep next block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "delete preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.deleteForward(), "delete after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold**after"), "delete complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "delete complex inline cursor offset mismatch");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph);
  RUN_TEST(testBlockQuoteEnterKeepsInsertedBlankLineInsideQuote);
  RUN_TEST(testBlockQuoteEmptyParagraphEnterOutdentsQuoteLevel);
  RUN_TEST(testBlockQuoteBackspaceOutdentsQuoteLevel);
  RUN_TEST(testBlockQuoteBackspaceOutdentsMultilineQuoteParagraph);
  RUN_TEST(testBlockQuoteBackspacePreservesQuoteWhenNotFirstChild);
  RUN_TEST(testBlockQuoteBackspaceOutdentsEmptyFirstChildQuote);
  RUN_TEST(testBlockQuoteBackspaceUserScenarios);
  RUN_TEST(testLocalReparsePreservesUntouchedNodeIds);
  RUN_TEST(testBlockEditContextSeparatesBlockAndContentRanges);
  RUN_TEST(testTextBlockCommandBuilderCreatesStructuralEnterCommands);
  RUN_TEST(testInputMergeParagraphs);
  RUN_TEST(testInputBackspaceAtParagraphStartDeletesStructuralBoundary);
  RUN_TEST(testInputDeleteAtParagraphEndDeletesStructuralBoundary);
#undef RUN_TEST
  return 0;
}
