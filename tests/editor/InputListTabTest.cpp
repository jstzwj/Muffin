#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>

#include <iostream>

using namespace muffin;

// testListItemInput (lines 980-1003)
void testListItemInput() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "unordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("- alpha!\n- beta"), "unordered list insert should preserve marker");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 0)->id(), "unordered list cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "unordered list cursor offset mismatch");

  setSelection(selection, listItemAt(session, 0, 0), 1, 5);
  require(input.insertText(QStringLiteral("X")), "unordered list selection replace should edit item body");
  require(session.markdownText() == QStringLiteral("- aX!\n- beta"), "unordered list selection replace mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "ordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("1. alpha!\n2. beta"), "ordered list insert should preserve marker");
}

// testListItemEditingCommands (lines 1005-1096)
void testListItemEditingCommands() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split unordered list item");
  require(session.markdownText() == QStringLiteral("- al\n- pha\n- beta"), "unordered list split mismatch");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 1)->id(), "unordered split cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "unordered split cursor offset mismatch");
  EditTransaction listUndo = requireTextDeltaCommand(undoStack, "unordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n- ")), "unordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n3. beta"), "ordered list split mismatch");
  listUndo = requireTextDeltaCommand(undoStack, "ordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n2. ")), "ordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix **bold**\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix b").size(),
                  QStringLiteral("- Plain text can mix **b").size());
  require(input.insertParagraphBreak(), "enter inside strong inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix **b**\n- **old**\n- beta"),
          "strong inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix *italic*\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix ita").size(),
                  QStringLiteral("- Plain text can mix *ita").size());
  require(input.insertParagraphBreak(), "enter inside emphasis inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix *ita*\n- *lic*\n- beta"),
          "emphasis inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix ~~through~~\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix thr").size(),
                  QStringLiteral("- Plain text can mix ~~thr").size());
  require(input.insertParagraphBreak(), "enter inside strike inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix ~~thr~~\n- ~~ough~~\n- beta"),
          "strike inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before `code` after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before co").size(), QStringLiteral("- before `co").size());
  require(input.insertParagraphBreak(), "enter inside code inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before `co`\n- `de` after\n- beta"), "code inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before $x+y$ after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before x").size(), QStringLiteral("- before $x").size());
  require(input.insertParagraphBreak(), "enter inside math inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before $x$\n- $+y$ after\n- beta"), "math inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- \n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.insertParagraphBreak(), "enter on empty list item should exit list");
  require(session.markdownText() == QStringLiteral("\n\n- beta"), "empty list enter exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.deleteBackward(), "backspace at list start should exit top-level list item");
  require(session.markdownText() == QStringLiteral("alpha\n- beta"), "top-level list backspace exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "tab should indent list item");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "list item indent mismatch");
  MarkdownNode* nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  require(nestedList != nullptr && nestedList->children().size() == 1, "list indent should create a nested list");
  require(undoStack.canUndo(), "list indent should be undoable");

  nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.outdentListItem(), "shift-tab should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "list item outdent mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(!input.indentListItem(), "first list item should not structurally indent without a previous sibling");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "first list item structural indent should leave markdown unchanged");

  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);
  session.setMarkdownText(QStringLiteral("- alpha\n  - beta"), false);
  nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  require(input.eventFilter(&view, &backtab), "backtab key should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "backtab key list outdent mismatch");
}

void testExitLastEmptyListItem() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // 2-item list: exit last empty item → paragraph after list
  session.setMarkdownText(QStringLiteral("- alpha\n- "), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.insertParagraphBreak(), "enter on last empty list item should exit list");
  require(session.markdownText() == QStringLiteral("- alpha\n\n"), "last empty list enter text mismatch");
  const auto& children = session.document().root().children();
  require(children.size() >= 2, "root should have at least 2 children after exit");
  require(children.at(1)->type() == BlockType::Paragraph, "second root child should be a paragraph");
  // Cursor should be in the paragraph
  require(selection.cursorPosition().isValid(), "cursor should be valid after exiting last list item");
  require(selection.cursorPosition().blockId == children.at(1)->id(), "cursor should be in trailing paragraph");

  // 3-item list matching user's exact scenario
  session.setMarkdownText(QStringLiteral("- 123\n- 123\n- "), false);
  setCursor(selection, listItemAt(session, 0, 2), 0);
  require(input.insertParagraphBreak(), "enter on last empty item in 3-item list should exit");
  require(session.markdownText() == QStringLiteral("- 123\n- 123\n\n"), "3-item last empty enter text mismatch");
  const auto& children3 = session.document().root().children();
  require(children3.size() >= 2, "root should have at least 2 children after 3-item exit");
  require(children3.at(1)->type() == BlockType::Paragraph, "second root child should be paragraph after 3-item exit");
  require(selection.cursorPosition().isValid(), "cursor should be valid after 3-item exit");
  require(selection.cursorPosition().blockId == children3.at(1)->id(), "cursor should be in paragraph after 3-item exit");
}

void testTyporaListKeyboardBehavior() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta\n3. gamma"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.insertParagraphBreak(), "enter at ordered item start should insert empty item above");
  require(session.markdownText() == QStringLiteral("1. alpha\n2. \n3. beta\n4. gamma"),
          "ordered start-enter should renumber following items");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta\n3. gamma"), false);
  setCursor(selection, listItemAt(session, 0, 1), 2);
  require(input.insertParagraphBreak(), "enter in ordered item middle should split item");
  require(session.markdownText() == QStringLiteral("1. alpha\n2. be\n3. ta\n4. gamma"),
          "ordered split should renumber following items");

  session.setMarkdownText(QStringLiteral("- [x] done"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "task item insert should edit task text only");
  require(session.markdownText() == QStringLiteral("- [x] done!"), "task item insert should preserve checkbox marker");

  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertParagraphBreak(), "enter at task item end should create unchecked task item");
  require(session.markdownText() == QStringLiteral("- [x] done!\n- [ ] "), "task enter should create unchecked task item");

  session.setMarkdownText(QStringLiteral("- [x] done\n- [ ] next"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.deleteBackward(), "backspace at task item start should merge into previous item");
  require(session.markdownText() == QStringLiteral("- [x] done next"), "task backspace merge should remove next checkbox");

  session.setMarkdownText(QStringLiteral("- [x] done\n- [ ] next"), false);
  setCursor(selection, listItemAt(session, 0, 0), 4);
  require(input.deleteForward(), "delete at task item end should merge next item");
  require(session.markdownText() == QStringLiteral("- [x] done next"), "task delete merge should remove next checkbox");

  session.setMarkdownText(QStringLiteral("- alpha\n  - beta\n  - "), false);
  MarkdownNode* nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 1), 0);
  require(input.insertParagraphBreak(), "enter on nested empty list item should outdent first");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta\n- "), "nested empty enter should reduce one list level");
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.insertParagraphBreak(), "enter on top-level empty list item should exit list");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta\n\n"), "top-level empty enter should exit list");
}

// testTabInRenderedTextInsertsZeroWidthSpace (lines 1098-1215)
void testTabInRenderedTextInsertsZeroWidthSpace() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  QVector<BrushQueue::RefreshRequest> tabRefreshes;
  QObject::connect(&brushQueue, &BrushQueue::refreshRequested, [&tabRefreshes](BrushQueue::RefreshRequest request) {
    tabRefreshes.push_back(std::move(request));
  });
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  QKeyEvent shortcutTab(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  require(!input.eventFilter(&view, &shortcutTab), "tab shortcut override should continue to keypress delivery");
  require(shortcutTab.isAccepted(), "tab shortcut override should reserve tab for rendered editor input");
  QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &tab), "tab at paragraph start should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("​alpha"), "paragraph tab should insert U+200B");

  session.setMarkdownText(QStringLiteral("alphabeta"), false);
  setCursor(selection, blockAt(session, 0), 5);
  QKeyEvent middleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &middleTab), "tab in middle of paragraph should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("alpha​beta"), "middle paragraph tab should insert U+200B");
  require(selection.cursorPosition().text.sourceOffset == QStringLiteral("alpha​").size(), "middle paragraph tab source offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after middle paragraph tab should keep editing");
  require(session.markdownText() == QStringLiteral("alpha​xbeta"), "typing after middle paragraph tab should preserve U+200B");

  session.setMarkdownText(QStringLiteral("## Title"), false);
  setCursor(selection, blockAt(session, 0), 2);
  QKeyEvent headingTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &headingTab), "tab in heading should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("## Ti​tle"), "heading tab should insert U+200B inside heading content");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  QKeyEvent listTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &listTab), "tab on list item should still indent structurally");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "list item tab should not fall back to plain spaces");
  require(!session.markdownText().contains(QChar(0x200b)), "unordered list tab should not insert U+200B");
  brushQueue.flush();
  require(!tabRefreshes.isEmpty() && !tabRefreshes.last().fullLayoutDirty, "unordered list structural tab should not request full refresh");
  require(tabRefreshes.last().topLevelRangeDirty.isValid(), "unordered list structural tab should request top-level range refresh");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 1), 2, QStringLiteral("- alpha\n- be").size());
  QKeyEvent listMiddleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &listMiddleTab), "tab in middle of unordered list item should insert U+200B");
  require(session.markdownText() == QStringLiteral("- alpha\n- be​ta"), "unordered list middle tab should not indent structurally");
  brushQueue.flush();
  require(maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List) == nullptr,
          "unordered list middle tab should not create nested list structure");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 1), 0, QStringLiteral("1. alpha\n2. ").size());
  QKeyEvent orderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedListTab), "tab on ordered list item should indent structurally");
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "ordered list tab should add structural leading spaces");
  require(!session.markdownText().contains(QChar(0x200b)), "ordered list tab should not insert U+200B");
  brushQueue.flush();
  require(!tabRefreshes.isEmpty() && !tabRefreshes.last().fullLayoutDirty, "ordered list structural tab should not request full refresh");
  require(tabRefreshes.last().topLevelRangeDirty.isValid(), "ordered list structural tab should request top-level range refresh");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 2, QStringLiteral("1. alpha\n2. be").size());
  QKeyEvent orderedListMiddleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedListMiddleTab), "tab in middle of ordered list item should insert U+200B");
  require(session.markdownText() == QStringLiteral("1. alpha\n2. be​ta"), "ordered list middle tab should not indent structurally");

  session.setMarkdownText(QStringLiteral("- M0/M2 alpha\n- M3 beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 0), 0, QStringLiteral("- ").size());
  QKeyEvent firstUnorderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &firstUnorderedListTab), "tab at first unordered item content start should insert U+200B");
  require(session.markdownText() == QStringLiteral("- ​M0/M2 alpha\n- M3 beta"),
          "first unordered item tab should fall back to content indentation");
  require(maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List) == nullptr,
          "first unordered item tab should not create impossible nested list structure");

  session.setMarkdownText(QStringLiteral("1. M0/M2 alpha\n2. M3 beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), 0, QStringLiteral("1. ").size());
  QKeyEvent firstOrderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &firstOrderedListTab), "tab at first ordered item content start should insert U+200B");
  require(session.markdownText() == QStringLiteral("1. ​M0/M2 alpha\n2. M3 beta"),
          "first ordered item tab should fall back to content indentation");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 1, QStringLiteral("- alpha\n- b").size());
  QKeyEvent unorderedVisualStartTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &unorderedVisualStartTab), "tab near unordered item visual start should indent structurally");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "unordered visual-start tab should indent item");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 1, QStringLiteral("1. alpha\n2. b").size());
  QKeyEvent orderedVisualStartTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedVisualStartTab), "tab near ordered item visual start should indent structurally");
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "ordered visual-start tab should indent item");

  DocumentSession realSession;
  EditorController controller;
  EditorView realView;
  realSession.setMarkdownText(QStringLiteral("alphabeta"), false);
  realView.resize(640, 360);
  realView.setDocument(realSession.document());
  controller.attach(&realSession, &realView);
  setSourceCursor(controller.selection(), blockAt(realSession, 0), 5, 5);
  QKeyEvent realShortcut(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&realView, &realShortcut);
  require(realShortcut.isAccepted(), "real editor view should accept tab shortcut override");
  QKeyEvent realTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&realView, &realTab);
  require(realSession.markdownText() == QStringLiteral("alpha​beta"), "real editor tab should insert U+200B in paragraph");
}

// testListTabFromRenderedClick (lines 1217-1310)
void testListTabFromRenderedClick() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 420);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  view.setDocument(session.document());
  MarkdownNode* secondItem = listItemAt(session, 0, 1);
  const QRectF secondRect = view.nodeRect(secondItem->id());
  const QPointF unorderedStart(secondRect.left() + 4.0, secondRect.center().y());
  QMouseEvent unorderedPress(
      QEvent::MouseButtonPress,
      unorderedStart,
      QPointF(unorderedStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &unorderedPress);
  QMouseEvent unorderedRelease(
      QEvent::MouseButtonRelease,
      unorderedStart,
      QPointF(unorderedStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &unorderedRelease);
  QKeyEvent unorderedTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&view, &unorderedTab);
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "clicking unordered item start then tab should indent item");

  QKeyEvent unorderedBacktab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  QApplication::sendEvent(&view, &unorderedBacktab);
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "shift-tab after unordered indent should outdent item");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  view.setDocument(session.document());
  MarkdownNode* orderedSecond = listItemAt(session, 0, 1);
  const QRectF orderedRect = view.nodeRect(orderedSecond->id());
  const QPointF orderedStart(orderedRect.left() + 4.0, orderedRect.center().y());
  QMouseEvent orderedPress(
      QEvent::MouseButtonPress,
      orderedStart,
      QPointF(orderedStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &orderedPress);
  QMouseEvent orderedRelease(
      QEvent::MouseButtonRelease,
      orderedStart,
      QPointF(orderedStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &orderedRelease);
  QKeyEvent orderedTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&view, &orderedTab);
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "clicking ordered item start then tab should indent item");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  view.setDocument(session.document());
  secondItem = listItemAt(session, 0, 1);
  const QRectF viewportRect = view.nodeRect(secondItem->id());
  const QPointF viewportStart(viewportRect.left() + 4.0, viewportRect.center().y());
  QMouseEvent viewportPress(
      QEvent::MouseButtonPress,
      viewportStart,
      QPointF(viewportStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportPress);
  QMouseEvent viewportRelease(
      QEvent::MouseButtonRelease,
      viewportStart,
      QPointF(viewportStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportRelease);
  QKeyEvent viewportShortcut(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportShortcut);
  require(viewportShortcut.isAccepted(), "viewport tab shortcut override should be accepted");
  QKeyEvent viewportTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportTab);
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "viewport tab after list click should indent item");
  MarkdownNode* indentedViewportItem = listItemAt(session, 0, 0)->children().size() > 1
                                           ? childAt(firstChildOfType(listItemAt(session, 0, 0), BlockType::List), 0)
                                           : nullptr;
  require(indentedViewportItem != nullptr, "viewport tab should parse indented unordered item as nested list item");
  require(listDepthForItem(indentedViewportItem) == 2, "viewport tab should increase unordered list AST depth");
}

// Regression for a cmark-gfm divergence from CommonMark: a lone "-" that can be
// an empty list item must be parsed as a list item, not as a setext heading
// underline (commonmark-spec#95, commonmark.js#222).  Before the fix, pressing
// Tab on an empty middle list item turned the preceding item into a Setext H2.
void testIndentEmptyListItemDoesNotPromotePreviousToHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // The user's exact scenario: an empty middle item is nested via Tab.
  session.setMarkdownText(QStringLiteral("- First item\n- \n- Second item with **bold** text"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "tab should indent the empty middle list item");
  require(session.markdownText() == QStringLiteral("- First item\n  - \n- Second item with **bold** text"),
          "indenting an empty item should only add leading spaces");

  MarkdownNode* firstItem = listItemAt(session, 0, 0);
  require(maybeFirstChildOfType(firstItem, BlockType::Paragraph) != nullptr,
          "preceding item content must stay a paragraph after nesting an empty item");
  require(maybeFirstChildOfType(firstItem, BlockType::Heading) == nullptr,
          "preceding item must not be promoted to a setext heading");
  MarkdownNode* nestedList = maybeFirstChildOfType(firstItem, BlockType::List);
  require(nestedList != nullptr && nestedList->children().size() == 1, "nesting should create one empty child item");
  require(listDepthForItem(childAt(nestedList, 0)) == 2, "nested empty item should be at list depth 2");

  // The bug also reproduces when the source already contains a nested empty item
  // (e.g. re-opening a saved file) — the parser must not promote the parent.
  session.setMarkdownText(QStringLiteral("- First item\n  - \n- Second item with **bold** text\n- Third item"), false);
  MarkdownNode* parsedFirst = listItemAt(session, 0, 0);
  require(maybeFirstChildOfType(parsedFirst, BlockType::Heading) == nullptr,
          "parsed nested empty item must not promote its parent to a heading");
  require(maybeFirstChildOfType(parsedFirst, BlockType::List) != nullptr,
          "parsed nested empty item should be a nested list");

  // The empty nested item must remain editable: typing fills it instead of
  // re-triggering the misparse.
  setCursor(selection, childAt(maybeFirstChildOfType(parsedFirst, BlockType::List), 0), 0);
  require(input.insertText(QStringLiteral("nested")), "typing into the nested empty item should edit it");
  require(session.markdownText() == QStringLiteral("- First item\n  - nested\n- Second item with **bold** text\n- Third item"),
          "typing into nested empty item should keep the list structure");

  // Ordered lists share the bug shape: an empty nested "1." must not be turned
  // into a setext underline either (setext only uses "-" / "=", but guard the
  // structure regardless).
  session.setMarkdownText(QStringLiteral("1. First item\n2. \n3. Second item"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "tab should indent the empty ordered middle item");
  require(session.markdownText() == QStringLiteral("1. First item\n  2. \n3. Second item"),
          "indenting empty ordered item should only add leading spaces");
  require(maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::Heading) == nullptr,
          "ordered preceding item must not become a heading");

  // Multi-character setext underlines are NOT list markers and must keep working
  // (guards the cmark-gfm fix from weakening the Setext feature).
  session.setMarkdownText(QStringLiteral("Title\n====\n"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading && blockAt(session, 0)->headingLevel() == 1,
          "'===' setext underline must still parse as a level-1 heading");
  session.setMarkdownText(QStringLiteral("Title\n----\n"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading && blockAt(session, 0)->headingLevel() == 2,
          "'----' setext underline must still parse as a level-2 heading");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testListItemInput);
  RUN_TEST(testListItemEditingCommands);
  RUN_TEST(testExitLastEmptyListItem);
  RUN_TEST(testTyporaListKeyboardBehavior);
  RUN_TEST(testTabInRenderedTextInsertsZeroWidthSpace);
  RUN_TEST(testListTabFromRenderedClick);
  RUN_TEST(testIndentEmptyListItemDoesNotPromotePreviousToHeading);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
