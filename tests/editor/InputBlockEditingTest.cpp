#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>
#include <variant>

using namespace muffin;

// testInputEmptyCodeFenceBackspaceRemovesBlock (lines 1312-1364)
void testInputEmptyCodeFenceBackspaceRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("before\n\n```cpp\n```\n\nafter"), false);
  require(session.document().root().children().size() == 3, "expected 3 blocks: para, code, para");
  MarkdownNode* code = blockAt(session, 1);
  require(code->type() == BlockType::CodeFence, "second block should be code fence");
  require(code->literal().isEmpty(), "code fence literal should be empty");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");
  require(codeFence.isEditing(), "code fence should be editing");

  require(input.deleteBackward(), "backspace on empty code fence should succeed");
  require(!session.markdownText().contains(QStringLiteral("```")), "empty code fence should be removed after backspace");
  require(session.document().root().children().size() >= 1, "should have at least 1 block after removing code fence");
  require(!codeFence.isEditing(), "code fence should no longer be editing after removal");

  require(undoStack.canUndo(), "removing empty code fence should push undo");
  EditTransaction undo = undoStack.takeUndo();
  require(undo.isRemoveNodeCommand(), "expected RemoveNodeCommand for empty block removal");
  require(undo.removeNodeCommand().nodeType == BlockType::CodeFence, "removed node type should be CodeFence");
}

// testInputEmptyCodeFenceDeleteRemovesBlock (lines 1366-1405)
void testInputEmptyCodeFenceDeleteRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("```cpp\n```"), false);
  require(session.document().root().children().size() == 1, "expected 1 code fence block");
  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "first block should be code fence");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  require(input.deleteForward(), "delete on empty code fence should succeed");
  require(!session.markdownText().contains(QStringLiteral("```")), "empty code fence should be removed after delete");
}

// testInputNonEmptyCodeFenceBackspaceDoesNotRemoveBlock (lines 1407-1446)
void testInputNonEmptyCodeFenceBackspaceDoesNotRemoveBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("```cpp\nhello\n```"), false);
  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "first block should be code fence");
  require(!code->literal().isEmpty(), "code fence should have content");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  require(input.deleteBackward(), "backspace should succeed");
  require(session.markdownText().contains(QStringLiteral("```cpp")), "non-empty code fence should not be removed");
}

// testDefinitionBlockFieldEditing (lines 1448-1490)
void testDefinitionBlockFieldEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[]: "), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "empty link template should parse as link definition");
  const DefinitionBlock emptyLink = link->definition();
  setSourceCursor(selection, link, emptyLink.labelRange.start - emptyLink.markerRange.start, emptyLink.labelRange.start);
  require(input.insertText(QStringLiteral("1")), "typing label should edit link definition");
  link = blockAt(session, 0);
  const DefinitionBlock labeledLink = link->definition();
  require(labeledLink.label == QStringLiteral("1"), "link label should update");

  setSourceCursor(selection, link, labeledLink.destinationRange.start - labeledLink.markerRange.start, labeledLink.destinationRange.start);
  require(input.insertText(QStringLiteral("https://example.com")), "typing url should edit link definition");
  link = blockAt(session, 0);
  const DefinitionBlock urlLink = link->definition();
  require(urlLink.destination == QStringLiteral("https://example.com"), "link destination should update");

  HitTestResult titleHit;
  titleHit.blockId = link->id();
  titleHit.textNodeId = link->id();
  titleHit.textOffset = urlLink.titleRange.start - urlLink.markerRange.start;
  titleHit.sourceOffset = urlLink.titleRange.start;
  titleHit.zone = HitTestResult::Zone::Text;
  titleHit.definitionField = HitTestResult::DefinitionField::Title;
  selection.setHitResult(titleHit);
  require(input.insertText(QStringLiteral("Example")), "typing title should edit link definition");
  require(session.markdownText() == QStringLiteral("[1]: https://example.com  \"Example\""), "link definition markdown mismatch");

  session.setMarkdownText(QStringLiteral("[^]: "), false);
  MarkdownNode* footnote = blockAt(session, 0);
  require(footnote->type() == BlockType::FootnoteDefinition, "empty footnote template should parse as footnote definition");
  const DefinitionBlock emptyFootnote = footnote->definition();
  setSourceCursor(selection, footnote, emptyFootnote.noteRange.start - emptyFootnote.markerRange.start, emptyFootnote.noteRange.start);
  require(input.insertText(QStringLiteral("note")), "typing note should edit footnote definition");
  require(session.markdownText() == QStringLiteral("[^]: note"), "footnote markdown mismatch");
}

// testEmptyDefinitionBackspaceDeletesBlock (lines 1492-1510)
void testEmptyDefinitionBackspaceDeletesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\n[]: "), false);
  MarkdownNode* link = firstBlockOfType(session, BlockType::LinkDefinition);
  require(link->type() == BlockType::LinkDefinition, "document should contain link definition");
  require(link->definition().label.isEmpty(), QStringLiteral("empty link label expected: '%1'").arg(link->definition().label));
  require(link->definition().destination.isEmpty(), QStringLiteral("empty link destination expected: '%1'").arg(link->definition().destination));
  require(link->definition().title.isEmpty(), QStringLiteral("empty link title expected: '%1'").arg(link->definition().title));
  setSourceCursor(selection, link, 0, link->definition().markerRange.start);
  require(input.deleteBackward(), "backspace should delete empty link definition");
  require(session.markdownText() == QStringLiteral("alpha"),
          QStringLiteral("empty link definition deletion mismatch: '%1'").arg(session.markdownText()));
}

// testOptionalLinkDefinitionTitleInsertionAddsQuotes (lines 1512-1538)
void testOptionalLinkDefinitionTitleInsertionAddsQuotes() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "link definition should parse");
  const DefinitionBlock definition = link->definition();
  require(definition.title.isEmpty(), "link definition should start without title");
  require(!definition.titleQuoted, "link definition should start without title quotes");
  HitTestResult titleHit;
  titleHit.blockId = link->id();
  titleHit.textNodeId = link->id();
  titleHit.textOffset = definition.titleRange.start - definition.markerRange.start;
  titleHit.sourceOffset = definition.titleRange.start;
  titleHit.zone = HitTestResult::Zone::Text;
  titleHit.definitionField = HitTestResult::DefinitionField::Title;
  selection.setHitResult(titleHit);

  require(input.insertText(QStringLiteral("Example")), "typing optional title should add quoted title");
  require(session.markdownText() == QStringLiteral("[1]: url  \"Example\""),
          QStringLiteral("optional title insertion mismatch: '%1'").arg(session.markdownText()));
}

// testDefinitionDestinationEditDoesNotRestoreStaleSourceText (lines 1540-1566)
void testDefinitionDestinationEditDoesNotRestoreStaleSourceText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[ref]: <https://old.example/a b> \"\""), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "link definition should parse");
  const DefinitionBlock definition = link->definition();
  setSourceSelection(
      selection,
      link,
      link,
      definition.destinationRange.start - definition.markerRange.start,
      definition.destinationRange.start,
      definition.destinationRange.end - definition.markerRange.start,
      definition.destinationRange.end);

  require(input.insertText(QStringLiteral("https://new.example/path")), "replacing destination should edit definition");
  require(session.markdownText() == QStringLiteral("[ref]: <https://new.example/path> \"\""),
          QStringLiteral("edited destination should keep angle destination and empty title shape: '%1'").arg(session.markdownText()));
  require(!session.markdownText().contains(QStringLiteral("https://old.example/a b")),
          QStringLiteral("stale sourceText destination should not be restored: '%1'").arg(session.markdownText()));
}

// Enter at the end of an indented code block cannot persist a trailing empty line (cmark strips
// it), so the editor shows a phantom blank line held only in the node and commits it once a key
// is typed on it. Exercises the handleKeyPress dispatch: Enter creates the phantom, Backspace
// undoes it, a printable commits it.
void testInputIndentedCodePhantomEnterLine() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  EditorView view;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, &view);
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("    code"), false);
  view.setDocument(session.document());
  require(blockAt(session, 0)->isIndentedCode(), "expected an indented code block");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = blockAt(session, 0)->id();
  hit.textNodeId = hit.blockId;
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  // Enter at the end creates a phantom line without touching the source.
  require(pressKey(input, &view, Qt::Key_Return), "Enter keypress should be handled");
  require(session.markdownText() == QStringLiteral("    code"), "phantom Enter must not mutate source");
  require(codeFence.hasPendingTrailingNewline(), "phantom line should be present after Enter");

  // Backspace undoes the phantom (still no source change).
  require(pressKey(input, &view, Qt::Key_Backspace), "backspace keypress should be handled");
  require(!codeFence.hasPendingTrailingNewline(), "phantom should clear on backspace");
  require(session.markdownText() == QStringLiteral("    code"), "backspace on phantom must not mutate source");

  // Re-create the phantom, then type a character to commit it as a real indented line.
  require(pressKey(input, &view, Qt::Key_Return), "phantom Enter recreated");
  QKeyEvent letter(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
  require(input.eventFilter(&view, &letter), "letter keypress should commit the phantom");
  require(!codeFence.hasPendingTrailingNewline(), "phantom should clear after commit");
  require(session.markdownText().contains(QStringLiteral("    code\n    x")), "committed line should be indented code");
  require(!session.markdownText().contains(QLatin1String("```")), "must stay indented, not become fenced");
}

// A second Enter on the phantom empty line exits the indented code block to a new paragraph
// below it (indented code cannot hold a second trailing empty line; Enter-on-empty leaves).
void testInputIndentedCodeSecondEnterExitsToParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  EditorView view;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, &view);
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("    code"), false);
  view.setDocument(session.document());
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = blockAt(session, 0)->id();
  hit.textNodeId = hit.blockId;
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  // First Enter: phantom line (source unchanged, still one block).
  require(pressKey(input, &view, Qt::Key_Return), "first Enter should be handled");
  require(codeFence.hasPendingTrailingNewline(), "phantom line should be set");
  require(session.document().root().children().size() == 1, "phantom Enter must not add a block");

  // Second Enter: exit to a new paragraph below the code block.
  require(pressKey(input, &view, Qt::Key_Return), "second Enter should be handled");
  require(session.document().root().children().size() == 2, "second Enter should create a paragraph after the code");
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "code block should remain first");
  require(blockAt(session, 1)->type() == BlockType::Paragraph, "a paragraph should follow the code block");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "cursor should land in the new paragraph");
  require(!session.markdownText().contains(QLatin1String("```")), "code must stay indented, not become fenced");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInputEmptyCodeFenceBackspaceRemovesBlock);
  RUN_TEST(testInputEmptyCodeFenceDeleteRemovesBlock);
  RUN_TEST(testInputNonEmptyCodeFenceBackspaceDoesNotRemoveBlock);
  RUN_TEST(testDefinitionBlockFieldEditing);
  RUN_TEST(testEmptyDefinitionBackspaceDeletesBlock);
  RUN_TEST(testOptionalLinkDefinitionTitleInsertionAddsQuotes);
  RUN_TEST(testDefinitionDestinationEditDoesNotRestoreStaleSourceText);
  RUN_TEST(testInputIndentedCodePhantomEnterLine);
  RUN_TEST(testInputIndentedCodeSecondEnterExitsToParagraph);
#undef RUN_TEST
  return 0;
}
