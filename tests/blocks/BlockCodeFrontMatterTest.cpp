#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"
#include "theme/RenderTheme.h"

#include "../editor/EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>

#include <cstdlib>
#include <iostream>
#include <variant>

using namespace muffin;

// testFrontMatterEnterEditsContentAndBlockAfterCreatesParagraph (lines 89-156)
void testFrontMatterEnterEditsContentAndBlockAfterCreatesParagraph() {
  {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);

    session.setMarkdownText(QStringLiteral("---\ntitle: 123213\n---\nBody"), false);
    MarkdownNode* frontMatter = firstBlockOfType(session, BlockType::FrontMatter);

    HitTestResult frontMatterHit;
    frontMatterHit.zone = HitTestResult::Zone::FrontMatter;
    frontMatterHit.blockId = frontMatter->id();
    frontMatterHit.textNodeId = frontMatter->id();
    frontMatterHit.textOffset = QStringLiteral("title: 123").size();
    controller.activateHit(frontMatterHit);
    require(controller.frontMatterLiteral().isEditing(), "front matter click should enter edit mode");
    require(controller.inputController().insertParagraphBreak(), "enter inside front matter should edit literal content");
    require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n---")),
            "first front matter enter should split the current literal line immediately");
    frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
    require(frontMatter->literal() == QStringLiteral("title: 123\n213"), "front matter literal should preserve first inserted newline");

    frontMatterHit.blockId = frontMatter->id();
    frontMatterHit.textNodeId = frontMatter->id();
    frontMatterHit.textOffset = frontMatter->literal().size();
    controller.activateHit(frontMatterHit);
    require(controller.inputController().insertParagraphBreak(), "enter at front matter end should edit literal content");
    require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n\n---")),
            "front matter enter at end should preserve a new literal blank line");

    frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
    HitTestResult blockAfterHit;
    blockAfterHit.zone = HitTestResult::Zone::BlockAfter;
    blockAfterHit.blockId = frontMatter->id();
    blockAfterHit.textNodeId = frontMatter->id();
    controller.activateHit(blockAfterHit);
    require(!controller.frontMatterLiteral().isEditing(), "block-after click should leave front matter edit mode");
    require(controller.selection().currentHit().zone == HitTestResult::Zone::BlockAfter, "block-after click should become current hit");
    MarkdownNode* blockAfterNode = session.document().node(controller.selection().cursorPosition().blockId);
    require(blockAfterNode != nullptr, "block-after cursor should reference an existing node");
    const SourceRange blockAfterRange = blockAfterNode->sourceRange();
    require(blockAfterRange.byteEnd >= blockAfterRange.byteStart, "block-after cursor node should have a valid source range");
    require(blockAfterRange.byteEnd <= session.markdownText().size(), "block-after cursor node source range should fit markdown text");
    require(controller.inputController().insertParagraphBreak(), "enter after front matter block should create a paragraph boundary");
    require(session.markdownText().startsWith(QStringLiteral("---\ntitle: 123\n213\n\n---\n\n\nBody")),
            "block-after enter should create an empty paragraph after front matter");
  }

  {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);

    session.setMarkdownText(QStringLiteral("---\ntitle: 123213\n---"), false);
    MarkdownNode* frontMatter = firstBlockOfType(session, BlockType::FrontMatter);
    HitTestResult blockAfterHit;
    blockAfterHit.zone = HitTestResult::Zone::BlockAfter;
    blockAfterHit.blockId = frontMatter->id();
    blockAfterHit.textNodeId = frontMatter->id();
    controller.activateHit(blockAfterHit);

    require(!controller.frontMatterLiteral().isEditing(), "block-after text click should not enter front matter edit mode");
    require(controller.inputController().insertText(QStringLiteral("OK")), "typing after front matter should create a paragraph");
    require(session.markdownText() == QStringLiteral("---\ntitle: 123213\n---\nOK"),
            "typing after front matter should insert normal paragraph text after the block");
    require(firstBlockOfType(session, BlockType::Paragraph) != nullptr, "typed block-after text should parse as a paragraph");
  }
}

// testComplexBlockActivationRoutesInput (lines 158-192)
void testComplexBlockActivationRoutesInput() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("| Name | Value |\n| --- | --- |\n| one | two |\n\n```cpp\nx\n```"), false);
  MarkdownNode* table = firstBlockOfType(session, BlockType::Table);
  MarkdownNode* tableCell = childAt(childAt(table, 1), 0);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 0;
  controller.activateHit(codeHit);
  require(controller.codeFenceController().isEditing(), "code click should enter code edit mode");
  require(controller.selection().cursorPosition().text.textOffset == 0, "code click should preserve hit offset");

  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = tableCell->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 0;
  cellHit.textOffset = 3;
  controller.activateHit(cellHit);
  require(!controller.codeFenceController().isEditing(), "table click should leave code edit mode");
  require(controller.tableController().currentCell().isValid(), "table click should activate current cell");

  require(controller.inputController().insertText(QStringLiteral("Z")), "table input should edit the active cell");
  require(session.markdownText().contains(QStringLiteral("oneZ")), "table input should update table cell text");
  require(!session.markdownText().contains(QStringLiteral("xZ")), "table input should not continue editing code fence");
  require(!session.markdownText().contains(QStringLiteral("x\n\n```")), "table input should not grow code fence trailing blank lines");
}

// testCodeActivationKeepsClickedOffsetForTyping (lines 367-386)
void testCodeActivationKeepsClickedOffsetForTyping() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nabcd\n```"), false);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 2;
  controller.activateHit(codeHit);

  require(controller.codeFenceController().isEditing(), "code activation should enter edit mode");
  require(controller.selection().cursorPosition().text.textOffset == 2, "code activation should keep clicked offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing in active code block should work");
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code typing should use clicked offset");
}

// testCodeFenceUndoUsesReplaceNodeCommand (lines 388-419)
void testCodeFenceUndoUsesReplaceNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nabcd\n```"), false);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = 2;
  controller.activateHit(codeHit);

  require(controller.inputController().insertText(QStringLiteral("X")), "code input should work");
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code input text mismatch");
  require(controller.undoStack().canUndo(), "code input should push undo");

  const EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isReplaceNodeCommand(), "code input undo should use ReplaceNodeCommand");
  require(transaction.replaceNodeCommand().nodeType == BlockType::CodeFence, "code replace command type mismatch");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("```cpp\nabcd\n```"), "code replace undo text mismatch");
  require(controller.selection().hasCursor(), "code replace undo should keep cursor");

  controller.redo();
  require(session.markdownText().contains(QStringLiteral("abXcd")), "code replace redo text mismatch");
  require(controller.selection().hasCursor(), "code replace redo should keep cursor");
}

// testWholeBlockDeleteUsesRemoveNodeCommand (lines 421-446)
void testWholeBlockDeleteUsesRemoveNodeCommand() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  MarkdownNode* middle = blockAt(session, 1);
  setSelection(controller.selection(), middle, 0, 4);

  require(controller.inputController().deleteSelection(), "whole block delete should work");
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete markdown mismatch");
  require(controller.undoStack().canUndo(), "whole block delete should push undo");
  EditTransaction transaction = controller.undoStack().takeUndo();
  require(transaction.isRemoveNodeCommand(), "whole block delete should use RemoveNodeCommand");
  require(transaction.removeNodeCommand().nodeType == BlockType::Paragraph, "remove node command type mismatch");
  require(transaction.removeNodeCommand().removedNode != nullptr, "remove node command snapshot missing");
  require(transaction.removeNodeCommand().delta.removedText.contains(QStringLiteral("beta")), "remove node command removed text mismatch");
  require(transaction.removeNodeCommand().delta.insertedText.isEmpty(), "remove node command inserted text should be empty");
  controller.undoStack().push(transaction);

  controller.undo();
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta\n\ngamma"), "whole block delete undo mismatch");
  controller.redo();
  require(session.markdownText() == QStringLiteral("alpha\n\ngamma"), "whole block delete redo mismatch");
  require(controller.selection().hasCursor(), "whole block delete redo should keep cursor");
}

// testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock (lines 500-530)
void testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```cpp\nfirst\n```\n\n```cpp\nsecond\n```"), false);
  MarkdownNode* firstCode = blockAt(session, 0);
  MarkdownNode* secondCode = blockAt(session, 1);
  require(firstCode->type() == BlockType::CodeFence, "first block should be code fence");
  require(secondCode->type() == BlockType::CodeFence, "second block should be code fence");

  HitTestResult firstHit;
  firstHit.zone = HitTestResult::Zone::Code;
  firstHit.blockId = firstCode->id();
  firstHit.textNodeId = firstCode->id();
  firstHit.textOffset = 0;
  controller.activateHit(firstHit);
  require(controller.inputController().insertText(QStringLiteral("A")), "first code input should work");

  HitTestResult secondHit;
  secondHit.zone = HitTestResult::Zone::Code;
  secondHit.blockId = blockAt(session, 1)->id();
  secondHit.textNodeId = blockAt(session, 1)->id();
  secondHit.textOffset = 0;
  controller.activateHit(secondHit);
  require(controller.inputController().insertText(QStringLiteral("B")), "second code input should work");

  require(session.markdownText().contains(QStringLiteral("Afirst")), "first code edit should remain in first code block");
  require(session.markdownText().contains(QStringLiteral("Bsecond")), "second code edit should target second code block");
  require(!session.markdownText().contains(QStringLiteral("BAfirst")), "second code input should not write into first code block");
}

// testCodeFenceEnterInsertsNewline (lines 532-558)
void testCodeFenceEnterInsertsNewline() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("```\n123\n```"), false);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);
  require(code->literal() == QStringLiteral("123"), "code block literal should be '123'");

  HitTestResult codeHit;
  codeHit.zone = HitTestResult::Zone::Code;
  codeHit.blockId = code->id();
  codeHit.textNodeId = code->id();
  codeHit.textOffset = code->literal().size();
  controller.activateHit(codeHit);
  require(controller.codeFenceController().isEditing(), "clicking code block should enter edit mode");
  require(controller.selection().cursorPosition().text.textOffset == code->literal().size(),
          "cursor should be at end of literal after click");

  require(controller.inputController().insertParagraphBreak(), "enter at end of code block content should succeed");
  code = firstBlockOfType(session, BlockType::CodeFence);
  require(code != nullptr, "code block should still exist after enter");
  require(code->literal() == QStringLiteral("123\n"), "enter should append newline to literal");
}

// testCodeFenceArrowKeyEnterInsertsNewline (lines 560-590)
void testCodeFenceArrowKeyEnterInsertsNewline() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("hello\n\n```\n123\n```"), false);
  view.setDocument(session.document());
  MarkdownNode* paragraph = blockAt(session, 0);
  MarkdownNode* code = firstBlockOfType(session, BlockType::CodeFence);
  require(paragraph->type() == BlockType::Paragraph, "first block should be paragraph");
  require(code != nullptr, "code block should exist");
  require(code->literal() == QStringLiteral("123"), "code block literal should be '123'");

  setCursor(controller.selection(), paragraph, paragraph->literal().size());
  sendKey(&view, Qt::Key_Down);
  require(controller.codeFenceController().isEditing(), "keyboard navigation should enter code edit mode");
  require(controller.selection().cursorPosition().blockId == code->id(), "cursor should move to code block");
  require(controller.selection().cursorPosition().text.textOffset == 0, "down arrow should land at start of code block literal");

  sendKey(&view, Qt::Key_Right);
  sendKey(&view, Qt::Key_Right);
  sendKey(&view, Qt::Key_Right);
  require(controller.selection().cursorPosition().text.textOffset == code->literal().size(), "cursor should be at end of code literal");

  require(controller.inputController().insertParagraphBreak(), "enter at end of code block should succeed");
  code = firstBlockOfType(session, BlockType::CodeFence);
  require(code != nullptr, "code block should still exist");
  require(code->literal() == QStringLiteral("123\n"), "enter should append newline to literal");
}

// testKeyboardSwitchingBetweenCodeBlocksRoutesInputToTargetBlock (lines 592-623)
void testKeyboardSwitchingBetweenCodeBlocksRoutesInputToTargetBlock() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("```\nfirst\n```\n\n```\nsecond\n```"), false);
  view.setDocument(session.document());
  MarkdownNode* firstCode = blockAt(session, 0);
  MarkdownNode* secondCode = blockAt(session, 1);
  require(firstCode->type() == BlockType::CodeFence, "first block should be code fence");
  require(secondCode->type() == BlockType::CodeFence, "second block should be code fence");

  HitTestResult firstHit;
  firstHit.zone = HitTestResult::Zone::Code;
  firstHit.blockId = firstCode->id();
  firstHit.textNodeId = firstCode->id();
  firstHit.textOffset = 0;
  controller.activateHit(firstHit);
  require(controller.inputController().insertText(QStringLiteral("A")), "first code input should work");

  sendKey(&view, Qt::Key_Down);
  secondCode = blockAt(session, 1);
  require(controller.selection().cursorPosition().blockId == secondCode->id(), "cursor should move to second code block");
  require(controller.codeFenceController().currentCodeFenceId() == secondCode->id(), "active code editor should switch to second block");
  require(controller.inputController().insertText(QStringLiteral("B")), "second code input should work after keyboard switch");

  require(session.markdownText().contains(QStringLiteral("Afirst")), "first code edit should remain in first code block");
  require(session.markdownText().contains(QStringLiteral("Bsecond")), "second code edit should target second code block");
  require(!session.markdownText().contains(QStringLiteral("BAfirst")), "second keyboard-routed input should not write into first code block");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testFrontMatterEnterEditsContentAndBlockAfterCreatesParagraph);
  RUN_TEST(testComplexBlockActivationRoutesInput);
  RUN_TEST(testCodeActivationKeepsClickedOffsetForTyping);
  RUN_TEST(testCodeFenceUndoUsesReplaceNodeCommand);
  RUN_TEST(testWholeBlockDeleteUsesRemoveNodeCommand);
  RUN_TEST(testSwitchingBetweenCodeBlocksRoutesInputToClickedBlock);
  RUN_TEST(testCodeFenceEnterInsertsNewline);
  RUN_TEST(testCodeFenceArrowKeyEnterInsertsNewline);
  RUN_TEST(testKeyboardSwitchingBetweenCodeBlocksRoutesInputToTargetBlock);
#undef RUN_TEST
  return 0;
}
