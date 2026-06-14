#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>

#include <iostream>

using namespace muffin;

namespace {
struct Harness {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  Harness() { wireInput(input, session, selection, undoStack, brushQueue); }
  void load(const QString& md) { session.setMarkdownText(md, false); }
};
}  // namespace

// Regression for the reported bug: on an empty trailing list item, backspace used to outdent
// (delete only the "* " marker), collapsing the line to trailing whitespace that parses to no
// block at all — so the caret offset landed out of range and the caret vanished (hasCursor=false).
// The fix folds the empty item back into its predecessor: the marker line is deleted and the caret
// retreats to the previous item's content end (the symmetric inverse of Enter creating the item).
void testBackspaceEmptyTrailingUnorderedItem() {
  Harness h;
  h.load(QStringLiteral("* 123\n* 123\n* "));
  setCursor(h.selection, listItemAt(h.session, 0, 2), 0);

  require(h.input.deleteBackward(), "backspace on empty trailing item should be handled");
  require(h.session.markdownText() == QStringLiteral("* 123\n* 123"),
          "empty trailing item should be removed, leaving the two real items");
  require(h.selection.hasCursor(), "caret must remain valid (the reported disappearance bug)");
  require(h.selection.cursorPosition().blockId == listItemAt(h.session, 0, 1)->id(),
          "caret should retreat into the previous item");
  require(h.selection.cursorPosition().text.textOffset == 3,
          "caret should sit at the end of the previous item's content");
}

// Ordered lists behave identically: the empty trailing item folds back, no spurious renumber.
void testBackspaceEmptyTrailingOrderedItem() {
  Harness h;
  h.load(QStringLiteral("1. 123\n2. 123\n3. "));
  setCursor(h.selection, listItemAt(h.session, 0, 2), 0);

  require(h.input.deleteBackward(), "backspace on empty ordered trailing item should be handled");
  require(h.session.markdownText() == QStringLiteral("1. 123\n2. 123"),
          "empty ordered trailing item should be removed");
  require(h.selection.hasCursor(), "caret must remain valid for ordered lists");
  require(h.selection.cursorPosition().blockId == listItemAt(h.session, 0, 1)->id(),
          "caret should retreat into the previous ordered item");
  require(h.selection.cursorPosition().text.textOffset == 3,
          "caret should sit at the end of the previous ordered item's content");
}

// A middle empty item folds the same way and keeps the list contiguous (no stray blank line that
// would split it into two lists).
void testBackspaceEmptyMiddleItem() {
  Harness h;
  h.load(QStringLiteral("* 123\n* \n* 456"));
  setCursor(h.selection, listItemAt(h.session, 0, 1), 0);

  require(h.input.deleteBackward(), "backspace on empty middle item should be handled");
  require(h.session.markdownText() == QStringLiteral("* 123\n* 456"),
          "empty middle item should be removed, list stays contiguous");
  require(h.selection.hasCursor(), "caret must remain valid after middle-item removal");
  require(h.selection.cursorPosition().blockId == listItemAt(h.session, 0, 0)->id(),
          "caret should retreat into the preceding item");
  require(h.selection.cursorPosition().text.textOffset == 3,
          "caret should sit at the end of the preceding item's content");
}

// Guard: a sole empty item has no predecessor to merge into, so it outdents (exits the list) as
// before — the fix only reroutes items that actually have a previous sibling.
void testBackspaceSoleEmptyItemStillOutdents() {
  Harness h;
  h.load(QStringLiteral("* "));
  setCursor(h.selection, listItemAt(h.session, 0, 0), 0);

  require(h.input.deleteBackward(), "backspace on sole empty item should be handled");
  require(h.session.markdownText() == QStringLiteral(""), "sole empty item should outdent to an empty document");
  require(h.selection.hasCursor(), "caret must remain valid after outdenting the sole item");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testBackspaceEmptyTrailingUnorderedItem();
  testBackspaceEmptyTrailingOrderedItem();
  testBackspaceEmptyMiddleItem();
  testBackspaceSoleEmptyItemStillOutdents();
  QApplication::clipboard()->clear();
  return 0;
}
