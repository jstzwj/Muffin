#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>
#include <variant>

using namespace muffin;

// testDefinitionTitleEditDoesNotRestoreSingleQuotedSourceText (lines 1568-1594)
void testDefinitionTitleEditDoesNotRestoreSingleQuotedSourceText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[ref]: url 'old title'"), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "single quoted title definition should parse");
  const DefinitionBlock definition = link->definition();
  setSourceSelection(
      selection,
      link,
      link,
      definition.titleRange.start - definition.markerRange.start,
      definition.titleRange.start,
      definition.titleRange.end - definition.markerRange.start,
      definition.titleRange.end);

  require(input.insertText(QStringLiteral("new title")), "replacing title should edit definition");
  require(session.markdownText() == QStringLiteral("[ref]: url 'new title'"),
          QStringLiteral("edited title should keep single quote shape: '%1'").arg(session.markdownText()));
  require(!session.markdownText().contains(QStringLiteral("old title")),
          QStringLiteral("stale single-quoted title should not be restored: '%1'").arg(session.markdownText()));
}

// testFootnoteDefinitionEnterAtNoteEndCreatesParagraphAfter (lines 1596-1616)
void testFootnoteDefinitionEnterAtNoteEndCreatesParagraphAfter() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[^1]: note"), false);
  MarkdownNode* footnote = blockAt(session, 0);
  require(footnote->type() == BlockType::FootnoteDefinition, "footnote should parse as definition block");
  const DefinitionBlock definition = footnote->definition();
  setSourceCursor(selection, footnote, definition.noteRange.end - definition.markerRange.start, definition.noteRange.end);

  require(input.insertParagraphBreak(), "enter at footnote note end should create paragraph after definition");
  require(session.markdownText() == QStringLiteral("[^1]: note\n\n"),
          QStringLiteral("footnote enter markdown mismatch: '%1'").arg(session.markdownText()));
  require(session.document().root().children().size() == 2, "footnote enter should create trailing empty paragraph");
  require(blockAt(session, 1)->type() == BlockType::Paragraph, "footnote enter should focus trailing paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "footnote enter cursor should move to trailing paragraph");
}

// testEmptyHeadingBackspaceConvertsToParagraph (lines 1618-1655)
void testEmptyHeadingBackspaceConvertsToParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("### \n\nafter"), false);
  require(session.document().root().children().size() == 2, "expected heading + paragraph");
  require(blockAt(session, 0)->type() == BlockType::Heading, "first block should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on empty heading should convert to paragraph");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "empty heading should become paragraph after backspace");
  require(!session.markdownText().contains(QLatin1Char('#')), "backspace should remove heading prefix");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "cursor should stay on converted block");

  session.setMarkdownText(QStringLiteral("## "), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "solo heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on solo empty heading should convert to paragraph");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "solo empty heading should become paragraph");
  require(session.markdownText().isEmpty() || !session.markdownText().contains(QLatin1Char('#')),
          "solo empty heading backspace should remove marker");

  session.setMarkdownText(QStringLiteral("## Title\n\nafter"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "titled heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on non-empty heading start should not convert");
  require(blockAt(session, 0)->type() == BlockType::Heading, "non-empty heading should remain heading after backspace");
  require(session.markdownText().contains(QLatin1String("## Title")), "non-empty heading backspace should preserve marker");
}

// testEmptyHeadingDeleteRemovesBlock (lines 1657-1692)
void testEmptyHeadingDeleteRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before\n\n### \n\nafter"), false);
  require(session.document().root().children().size() == 3, "expected para + heading + para");
  require(blockAt(session, 1)->type() == BlockType::Heading, "middle block should be heading");
  setCursor(selection, blockAt(session, 1), 0);

  require(input.deleteForward(), "delete on empty heading should remove block");
  require(session.markdownText() == QStringLiteral("before\n\nafter"),
          QStringLiteral("empty heading delete text mismatch: '%1'").arg(session.markdownText()));
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "cursor should move to next paragraph");

  session.setMarkdownText(QStringLiteral("### "), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "solo empty heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteForward(), "delete on solo empty heading with no next block should be handled");
  require(session.markdownText() == QStringLiteral("### "),
          "solo empty heading delete with no next block should be a no-op");

  session.setMarkdownText(QStringLiteral("## Title\n\nafter"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "titled heading should be heading");
  setCursor(selection, blockAt(session, 0), 6);

  require(input.deleteForward(), "delete at non-empty heading end should merge, not remove");
  require(session.markdownText().contains(QLatin1String("Title")), "non-empty heading delete should preserve content");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testDefinitionTitleEditDoesNotRestoreSingleQuotedSourceText);
  RUN_TEST(testFootnoteDefinitionEnterAtNoteEndCreatesParagraphAfter);
  RUN_TEST(testEmptyHeadingBackspaceConvertsToParagraph);
  RUN_TEST(testEmptyHeadingDeleteRemovesBlock);
#undef RUN_TEST
  return 0;
}
