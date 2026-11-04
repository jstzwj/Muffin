#include "document/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include "ParagraphTestUtils.h"
#include "theme/MarkdownFlow.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

void testSetHeadingLevel() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(paragraph.setHeadingLevel(1), "paragraph -> H1 should succeed");
  require(session.markdownText().toString() == QStringLiteral("# Hello world"), "paragraph -> H1 text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "H1 block type mismatch");
  require(blockAt(session, 0)->headingLevel() == 1, "H1 level mismatch");

  require(paragraph.setHeadingLevel(3), "H1 -> H3 should succeed");
  require(session.markdownText().toString() == QStringLiteral("### Hello world"), "H1 -> H3 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 3, "H3 level mismatch");

  require(paragraph.setHeadingLevel(0), "H3 -> paragraph should succeed");
  require(session.markdownText().toString() == QStringLiteral("Hello world"), "H3 -> paragraph text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "paragraph block type mismatch");

  require(paragraph.setHeadingLevel(0), "paragraph -> paragraph should be no-op");
  require(session.markdownText().toString() == QStringLiteral("Hello world"), "paragraph no-op text mismatch");
}

void testPromoteDemoteHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("### Title"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(paragraph.promoteHeading(), "promote H3 -> H2 should succeed");
  require(session.markdownText().toString() == QStringLiteral("## Title"), "promote H3 -> H2 text mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "promote level mismatch");

  require(paragraph.promoteHeading(), "promote H2 -> H1 should succeed");
  require(session.markdownText().toString() == QStringLiteral("# Title"), "promote H2 -> H1 text mismatch");

  require(!paragraph.promoteHeading(), "promote H1 should fail");

  require(paragraph.demoteHeading(), "demote H1 -> H2 should succeed");
  require(session.markdownText().toString() == QStringLiteral("## Title"), "demote H1 -> H2 text mismatch");

  require(paragraph.demoteHeading(), "demote H2 -> H3 should succeed");
  require(session.markdownText().toString() == QStringLiteral("### Title"), "demote H2 -> H3 text mismatch");

  paragraph.setHeadingLevel(6);
  require(!paragraph.demoteHeading(), "demote H6 should fail");

  session.setMarkdownText(QStringLiteral("plain text"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(!paragraph.demoteHeading(), "demote on paragraph should fail");
  require(!paragraph.promoteHeading(), "promote on paragraph should fail");
}

void testHeadingUndoRedo() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(paragraph.setHeadingLevel(2), "set H2 should succeed");
  require(session.markdownText().toString() == QStringLiteral("## Hello"), "set H2 text mismatch");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "heading undo should use TextDeltaCommand");
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(),
                         undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText().toString() == QStringLiteral("Hello"), "heading undo text mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(),
                         redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText().toString() == QStringLiteral("## Hello"), "heading redo text mismatch");
}

void testHeadingEmptyContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hi"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(2), "short paragraph -> H2 should succeed");
  require(blockAt(session, 0)->type() == BlockType::Heading, "short -> H2 type mismatch");
  require(blockAt(session, 0)->headingLevel() == 2, "short -> H2 level mismatch");

  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.setHeadingLevel(0), "H2 -> paragraph should succeed");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "H2 -> paragraph type mismatch");
}

void testHeadingPreservesCursorInContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.setHeadingLevel(1), "set H1 should succeed");
  require(session.markdownText().toString() == QStringLiteral("# Hello world"), "H1 text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "cursor should stay at content offset 5");
}

void testHeadingMultiBlockDocument() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("First\n\nSecond\n\nThird"), false);
  require(session.document().root().children().size() == 3, "should have 3 blocks");

  setCursor(selection, blockAt(session, 1), 3);
  require(paragraph.setHeadingLevel(2), "convert second block to H2 should succeed");
  require(session.markdownText().toString() == QStringLiteral("First\n\n## Second\n\nThird"),
          "multi-block H2 text mismatch");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should stay paragraph");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third block should stay paragraph");
  require(blockAt(session, 1)->type() == BlockType::Heading, "second block should be heading");
}

void testParagraphAlignmentPreservesContentAndUndo() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);
  const QString original = QStringLiteral("Before\n\n## 中文 **bold** [link](note.md)\n\nAfter\n");
  session.setMarkdownText(original, false);
  const auto offset = original.indexOf(QStringLiteral("中文")) + 1;
  setSourceCursor(selection, blockAt(session, 1), 1, offset);
  require(paragraph.canAlignParagraph(), "heading can be aligned");
  for (auto alignment : {Qt::AlignHCenter, Qt::AlignRight, Qt::AlignJustify, Qt::AlignLeft}) {
    require(paragraph.setParagraphAlignment(alignment), "alignment command succeeds");
    const auto text = session.markdownText().toString();
    require(text.count(QStringLiteral("<div")) == 1 && text.count(QStringLiteral("</div>")) == 1,
            "changing alignment reuses the dedicated wrapper");
    require(text.contains(QStringLiteral("## 中文 **bold** [link](note.md)")) &&
                text.startsWith(QStringLiteral("Before\n\n")) && text.endsWith(QStringLiteral("\n\nAfter\n")),
            "alignment preserves Markdown content and adjacent blocks");
    require(text.at(selection.cursorPosition().text.sourceOffset) == original.at(offset),
            "alignment retains caret source mapping");
    const MarkdownFlow flow(session.document().root());
    const auto* node = session.document().node(selection.cursorPosition().blockId);
    require(node && node->type() == BlockType::Heading && flow.parent(*node) && flow.container(*flow.parent(*node)),
            "alignment uses the shared HTML/Markdown rendering topology");
  }
  const auto state = undoStack.stateId();
  require(paragraph.setParagraphAlignment(Qt::AlignLeft) && undoStack.stateId() == state,
          "repeating alignment does not add undo entries");
  for (int i = 0; i < 4; ++i) {
    const auto undo = undoStack.takeUndo().textDeltaCommand();
    session.applyTextDelta(undo.delta.start, undo.delta.insertedText.size(), undo.delta.removedText, true);
    selection.setCursorPosition(undo.beforeCursor);
  }
  require(session.markdownText().toString() == original, "alignment sequence undoes to exact source");
  for (int i = 0; i < 4; ++i) {
    const auto redo = undoStack.takeRedo().textDeltaCommand();
    session.applyTextDelta(redo.delta.start, redo.delta.removedText.size(), redo.delta.insertedText, true);
    selection.setCursorPosition(redo.afterCursor);
  }
  require(session.markdownText().toString().contains(QStringLiteral("text-align: left")) &&
              session.markdownText().toString().count(QStringLiteral("<div")) == 1,
          "alignment sequence redoes without duplicating wrappers");

  session.setMarkdownText(QStringLiteral("> quote\n\n- item\n"), false);
  setCursor(selection, firstChildOfType(blockAt(session, 0), BlockType::Paragraph), 0);
  require(!paragraph.canAlignParagraph() && !paragraph.setParagraphAlignment(Qt::AlignRight),
          "nested Markdown prefixes are never damaged by HTML alignment");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSetHeadingLevel);
  RUN_TEST(testPromoteDemoteHeading);
  RUN_TEST(testHeadingUndoRedo);
  RUN_TEST(testHeadingEmptyContent);
  RUN_TEST(testHeadingPreservesCursorInContent);
  RUN_TEST(testHeadingMultiBlockDocument);
  RUN_TEST(testParagraphAlignmentPreservesContentAndUndo);
#undef RUN_TEST
  return 0;
}
