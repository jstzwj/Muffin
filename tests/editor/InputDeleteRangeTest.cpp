#include "app/RenderEditorBackend.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>

#include <iostream>

using namespace muffin;

namespace {

struct Harness {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  RenderEditorBackend backend;
  Harness() : backend(controller, session, &view) {
    controller.attach(&session, &view);
    view.resize(720, 460);
  }
  void load(const QString& md) {
    session.setMarkdownText(md, false);
    view.setDocument(session.document());
  }
  // Place the caret at `offset` inside the block that resolves to `block`
  // (e.g. a heading, paragraph, or list-item content node).
  void placeIn(MarkdownNode* block, qsizetype offset) {
    setCursor(controller.selection(), block, offset);
  }
  void placeInTrailing() {
    HitTestResult hit;
    hit.zone = HitTestResult::Zone::BlockAfter;
    hit.blockId = session.document().root().children().back()->id();
    hit.textNodeId = hit.blockId;
    controller.activateHit(hit);
  }
};

}  // namespace

// deleteRange(Word) removes the word at the caret, leaving surrounding text.
void testDeleteWordInParagraph() {
  Harness h;
  h.load(QStringLiteral("hello world"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 6);  // caret at start of "world"
  h.backend.deleteRange(DeleteTarget::Word);
  require(h.session.markdownText() == QStringLiteral("hello "), "delete word should remove \"world\"");
}

// A caret in the middle of a word still selects (and deletes) the whole word.
void testDeleteWordMidWord() {
  Harness h;
  h.load(QStringLiteral("hello world"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 8);  // caret inside "world"
  h.backend.deleteRange(DeleteTarget::Word);
  require(h.session.markdownText() == QStringLiteral("hello "), "delete word from mid-word should remove the whole word");
}

// deleteRange(Word) on a bare caret between words is a safe no-op.
void testDeleteWordNoWordAtCursor() {
  Harness h;
  h.load(QStringLiteral("hello   world"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 6);  // caret in the run of spaces
  h.backend.deleteRange(DeleteTarget::Word);
  require(h.session.markdownText() == QStringLiteral("hello   world"), "delete word on whitespace should be a no-op");
}

// deleteRange(FormatSpan) removes the inline-format span's visible text.
void testDeleteFormatSpanBold() {
  Harness h;
  h.load(QStringLiteral("a **bold** b"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 5);  // caret inside "bold"
  h.backend.deleteRange(DeleteTarget::FormatSpan);
  require(!h.session.markdownText().contains(QStringLiteral("bold")), "delete format span should remove the bold text");
}

// deleteRange(Line) clears a heading's content but keeps the marker ("## ").
void testDeleteLineHeadingKeepsMarker() {
  Harness h;
  h.load(QStringLiteral("## Title"));
  MarkdownNode* heading = blockAt(h.session, 0);
  h.placeIn(heading, 2);  // caret inside the title text
  h.backend.deleteRange(DeleteTarget::Line);
  require(h.session.markdownText() == QStringLiteral("## "), "delete line on a heading should keep the \"## \" marker");
  require(h.session.document().root().children().size() == 1, "heading block should still exist");
}

// deleteRange(Line) on a markerless paragraph removes the block (an empty
// paragraph has no content to preserve, so it collapses — same as Block).
void testDeleteLineParagraphRemovesBlock() {
  Harness h;
  h.load(QStringLiteral("alpha\n\nbeta"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 1);
  h.backend.deleteRange(DeleteTarget::Line);
  require(h.session.markdownText() == QStringLiteral("beta"), "delete line on a paragraph should remove the block");
}

// deleteRange(Block) removes a whole heading block and joins neighbours.
void testDeleteBlockHeading() {
  Harness h;
  h.load(QStringLiteral("## Title\n\nbody"));
  MarkdownNode* heading = blockAt(h.session, 0);
  h.placeIn(heading, 2);
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText() == QStringLiteral("body"), "delete block should remove the heading and its separators");
  require(h.session.document().root().children().size() == 1, "one block should remain");
}

// deleteRange(Block) on the only block leaves a valid empty document.
void testDeleteBlockOnlyBlock() {
  Harness h;
  h.load(QStringLiteral("alpha"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 1);
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText().isEmpty(), "deleting the only block should empty the document");
  require(!h.session.markdownText().contains(QLatin1String("alpha")), "no block content should remain");
}

// deleteRange(Block) removes a code fence entirely.
void testDeleteBlockCodeFence() {
  Harness h;
  h.load(QStringLiteral("```cpp\ncode\n```"));
  MarkdownNode* code = blockAt(h.session, 0);
  require(code->type() == BlockType::CodeFence, "first block should be a code fence");
  h.placeIn(code, 2);
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText().isEmpty(), "deleting the code fence should empty the document");
}

// deleteRange(Block) inside a list removes the entire list (v1: list-item-level
// removal is not supported; the top-level List node is removed).
void testDeleteBlockListRemovesWholeList() {
  Harness h;
  h.load(QStringLiteral("- one\n- two"));
  MarkdownNode* list = blockAt(h.session, 0);
  require(list->type() == BlockType::List, "first block should be a list");
  // Caret in the first item's content.
  MarkdownNode* item = listItemAt(h.session, 0, 0);
  MarkdownNode* itemPara = firstChildOfType(item, BlockType::Paragraph);
  h.placeIn(itemPara, 1);
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText().isEmpty(), "delete block in a list should remove the whole list");
}

// deleteRange(Block) on the trailing caret is a no-op (must not delete the last block).
void testDeleteBlockTrailingCaretIsNoOp() {
  Harness h;
  h.load(QStringLiteral("alpha"));
  h.placeInTrailing();
  require(h.controller.selection().cursorPosition().afterBlock, "caret should be on the trailing paragraph");
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText() == QStringLiteral("alpha"), "delete block from trailing caret must not change the document");
}

// deleteRange(Block) produces a single undoable RemoveNodeCommand; redo re-applies.
void testDeleteBlockUndoRedo() {
  Harness h;
  h.load(QStringLiteral("## Title\n\nbody"));
  MarkdownNode* heading = blockAt(h.session, 0);
  h.placeIn(heading, 2);
  const QString before = h.session.markdownText();

  h.backend.deleteRange(DeleteTarget::Block);
  require(h.session.markdownText() == QStringLiteral("body"), "block should be removed");

  require(h.controller.canUndo(), "delete block should be undoable");
  h.controller.undo();
  require(h.session.markdownText() == before, "undo should restore the deleted block");

  require(h.controller.canRedo(), "redo should be available after undo");
  h.controller.redo();
  require(h.session.markdownText() == QStringLiteral("body"), "redo should re-remove the block");
}

// deleteRange(Forward) preserves the legacy "delete selection else one char" semantics.
void testDeleteRangeForwardCollapsesSelection() {
  Harness h;
  h.load(QStringLiteral("alpha"));
  MarkdownNode* para = blockAt(h.session, 0);
  h.placeIn(para, 0);
  h.backend.deleteRange(DeleteTarget::Forward);
  require(h.session.markdownText() == QStringLiteral("lpha"), "forward delete should remove one char");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testDeleteWordInParagraph();
  testDeleteWordMidWord();
  testDeleteWordNoWordAtCursor();
  testDeleteFormatSpanBold();
  testDeleteLineHeadingKeepsMarker();
  testDeleteLineParagraphRemovesBlock();
  testDeleteBlockHeading();
  testDeleteBlockOnlyBlock();
  testDeleteBlockCodeFence();
  testDeleteBlockListRemovesWholeList();
  testDeleteBlockTrailingCaretIsNoOp();
  testDeleteBlockUndoRedo();
  testDeleteRangeForwardCollapsesSelection();
  QApplication::clipboard()->clear();
  return 0;
}
