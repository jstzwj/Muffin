#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "document/DocumentSession.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include "ParagraphTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

namespace {
// The first top-level block of a list document is the List; its children are the items.
MarkdownNode* listItem(const DocumentSession& session, qsizetype index) {
  MarkdownNode* list = blockAt(session, 0);
  require(list != nullptr, "document should contain a list");
  require(index >= 0 && index < static_cast<qsizetype>(list->children().size()), "list item index out of range");
  return list->children().at(static_cast<size_t>(index)).get();
}
}  // namespace

void testToggleUncheckedToChecked() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- [ ] buy milk"), false);
  MarkdownNode* item = listItem(session, 0);
  require(item->type() == BlockType::ListItem, "first child should be a list item");
  require(item->isTaskItem(), "first item should be a task item");
  require(!item->taskChecked(), "first item should start unchecked");

  require(paragraph.toggleTaskListItem(item->id()), "toggle should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [x] buy milk"), "unchecked item should become [x]");
}

void testToggleCheckedToUnchecked() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- [x] done"), false);
  require(listItem(session, 0)->taskChecked(), "item should start checked");

  require(paragraph.toggleTaskListItem(listItem(session, 0)->id()), "toggle should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [ ] done"), "checked item should become [ ]");
}

void testTogglePreservesSiblingsAndContent() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- [ ] a\n- [x] b\n- [ ] c"), false);
  // Toggle only the middle item.
  require(paragraph.toggleTaskListItem(listItem(session, 1)->id()), "toggling the middle item should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [ ] a\n- [ ] b\n- [ ] c"),
          "only the toggled item's marker should change; siblings and content stay untouched");
}

void testToggleUppercaseXNormalizesToLowercase() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- [X] done"), false);
  require(paragraph.toggleTaskListItem(listItem(session, 0)->id()), "toggling an uppercase-X item should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [ ] done"), "[X] should uncheck to [ ]");
  // Toggling back re-checks with a normalized lowercase x.
  require(paragraph.toggleTaskListItem(listItem(session, 0)->id()), "toggle back should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [x] done"), "re-check should normalize to lowercase [x]");
}

void testToggleUndoRestores() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  const QString original = QStringLiteral("- [ ] task");
  session.setMarkdownText(original, false);
  setSourceCursor(selection, listItem(session, 0), 0, 0);

  require(paragraph.toggleTaskListItem(listItem(session, 0)->id()), "toggle should succeed");
  require(session.markdownText().toString() == QStringLiteral("- [x] task"), "should be checked after toggle");

  const EditTransaction undo = requireTextDeltaCommand(undoStack, "toggle task undo should be a TextDeltaCommand");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  require(session.markdownText().toString() == original, "undo should restore the original marker");
}

void testToggleNonTaskItemFails() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  const QString original = QStringLiteral("- plain bullet");
  session.setMarkdownText(original, false);
  MarkdownNode* item = listItem(session, 0);
  require(item->type() == BlockType::ListItem, "should be a list item");
  require(!item->isTaskItem(), "a plain bullet is not a task item");

  require(!paragraph.toggleTaskListItem(item->id()), "toggling a non-task item should fail");
  require(session.markdownText().toString() == original, "a failed toggle should leave the source unchanged");
  require(!paragraph.toggleTaskListItem(NodeId{}), "toggling an invalid id should fail");
}

void testTogglePreservesCaret() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- [ ] alpha\n- [x] beta"), false);
  // Caret sits inside the first item's content; toggling the second must not move it
  // because a 1-character marker swap shifts no offsets.
  setSourceCursor(selection, listItem(session, 0), 2, 8);
  const qsizetype caretBefore = selection.cursorPosition().text.sourceOffset;

  require(paragraph.toggleTaskListItem(listItem(session, 1)->id()), "toggling the second item should succeed");
  require(selection.cursorPosition().text.sourceOffset == caretBefore,
          "a 1-character marker swap must not shift the caret");
  require(session.markdownText().toString() == QStringLiteral("- [ ] alpha\n- [ ] beta"), "second item should be unchecked");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testToggleUncheckedToChecked);
  RUN_TEST(testToggleCheckedToUnchecked);
  RUN_TEST(testTogglePreservesSiblingsAndContent);
  RUN_TEST(testToggleUppercaseXNormalizesToLowercase);
  RUN_TEST(testToggleUndoRestores);
  RUN_TEST(testToggleNonTaskItemFails);
  RUN_TEST(testTogglePreservesCaret);
#undef RUN_TEST
  return 0;
}
