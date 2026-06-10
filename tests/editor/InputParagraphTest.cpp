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
  require(session.markdownText() == QStringLiteral("alpha beta"), "backspace merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace merge cursor mismatch");
  EditTransaction mergeUndo = requireTextDeltaCommand(undoStack, "backspace merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "backspace merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "backspace merge inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.deleteForward(), "delete at paragraph end should merge next paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "delete merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete merge cursor mismatch");
  mergeUndo = requireTextDeltaCommand(undoStack, "delete merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "delete merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "delete merge inserted text mismatch");
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
  require(session.markdownText() == QStringLiteral("# Title body"), "backspace heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold** after"), "backspace complex inline merge mismatch");
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
  require(session.markdownText() == QStringLiteral("alpha Title"), "delete heading merge mismatch");
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
  require(session.markdownText() == QStringLiteral("before **bold** after"), "delete complex inline merge mismatch");
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
  RUN_TEST(testLocalReparsePreservesUntouchedNodeIds);
  RUN_TEST(testBlockEditContextSeparatesBlockAndContentRanges);
  RUN_TEST(testTextBlockCommandBuilderCreatesStructuralEnterCommands);
  RUN_TEST(testInputMergeParagraphs);
  RUN_TEST(testInputBackspaceAtParagraphStartDeletesStructuralBoundary);
  RUN_TEST(testInputDeleteAtParagraphEndDeletesStructuralBoundary);
#undef RUN_TEST
  return 0;
}
