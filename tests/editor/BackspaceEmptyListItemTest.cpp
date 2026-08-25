#include "document/DocumentSession.h"
#include "document/SourceRangeUtil.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BlockEditContext.h"
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
  require(h.session.markdownText().toString() == QStringLiteral("* 123\n* 123"),
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
  require(h.session.markdownText().toString() == QStringLiteral("1. 123\n2. 123"),
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
  require(h.session.markdownText().toString() == QStringLiteral("* 123\n* 456"),
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
  require(h.session.markdownText().toString() == QStringLiteral(""), "sole empty item should outdent to an empty document");
  require(h.selection.hasCursor(), "caret must remain valid after outdenting the sole item");
}

// A marker-only line ("3423.", "-") parses as an EMPTY list item in CommonMark, so
// listLineInfoFor must recognize it: every edit path that looks the marker line up
// (fill/outdent/merge/renumber) used to silently no-op on such items — backspace below one
// claimed success forever while deleting nothing (the reported "cannot delete the ordered
// list" bug). The line end acts as the (empty) content start. Delimiter-attached text
// ("234.text") stays a paragraph, and thematic breaks ("---") stay non-list lines.
void testMarkerOnlyLineIsAListLine() {
  const ListLineInfo markerOnlyOrdered = listLineInfoFor(QStringLiteral("3423."));
  require(markerOnlyOrdered.valid && markerOnlyOrdered.ordered,
          "'3423.' at end-of-line should parse as an empty ordered list item");
  require(markerOnlyOrdered.contentStart == 5, "content start should sit at the line end");
  require(markerOnlyOrdered.orderedNumber == 3423, "authored number should be 3423");
  require(markerOnlyOrdered.marker == QStringLiteral("3423."), "marker should be the full marker text");

  const ListLineInfo markerOnlyBullet = listLineInfoFor(QStringLiteral("-"));
  require(markerOnlyBullet.valid && !markerOnlyBullet.ordered,
          "a bare '-' at end-of-line should parse as an empty bullet item");

  require(!listLineInfoFor(QStringLiteral("234.text")).valid,
          "'234.text' (no space after the delimiter) must stay a paragraph");
  require(!listLineInfoFor(QStringLiteral("---")).valid, "'---' is a thematic break, not a list line");
  require(!listLineInfoFor(QStringLiteral("3423")).valid, "'3423' without a delimiter is not a list line");
  require(listLineInfoFor(QStringLiteral("234. ")).valid,
          "'234. ' (trailing space) keeps parsing as an ordered list line");
}

// The reported document: an empty ordered item "3423." (marker-only, no trailing space)
// followed by a paragraph whose own "234." has no space either. Backspacing from the
// paragraph's end used to stall forever once the caret reached the paragraph start: the
// empty item's fill() failed, so the merge into it returned an unhandled command that
// applyTextCommand still reported as success. Now every backspace makes progress and the
// document deletes all the way to empty.
void testBackspaceThroughMarkerOnlyItemDeletesList() {
  Harness h;
  h.load(QStringLiteral("3423.\n\n234.《光标在这里》"));
  require(h.session.document().root().children().size() == 2,
          "document should hold the list and the paragraph");
  MarkdownNode* paragraph = h.session.document().root().children().at(1).get();
  require(paragraph->type() == BlockType::Paragraph, "'234.《…》' without a space is a paragraph");
  setCursor(h.selection, paragraph, 11);  // end of "234.《光标在这里》"

  for (int step = 0; step < 20; ++step) {
    require(h.input.deleteBackward(), "each backspace should be handled");
    require(h.selection.hasCursor(), "caret must stay valid throughout");
  }
  require(h.session.markdownText().toString().isEmpty(),
          "backspacing through the whole document should delete the list entirely");
}

// Companion: with a space after the second marker ("234. ") the second line IS a list item.
// Deleting its content and folding it away leaves the sole empty "3423." item — backspace
// from there must keep making progress (outdent, then characters) instead of no-op'ing.
void testBackspaceOnSoleMarkerOnlyItemMakesProgress() {
  Harness h;
  h.load(QStringLiteral("3423.\n\n234. 《光标在这里》"));
  setCursor(h.selection, listItemAt(h.session, 0, 1), 7);  // end of 《光标在这里》

  const QString before = h.session.markdownText().toString();
  for (int step = 0; step < 16; ++step) {
    require(h.input.deleteBackward(), "backspace should be handled");
  }
  const QString after = h.session.markdownText().toString();
  require(before != after && after.isEmpty(), "repeated backspace must delete the document down to empty");
  require(h.selection.hasCursor(), "caret must remain valid after full deletion");
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
  testMarkerOnlyLineIsAListLine();
  testBackspaceThroughMarkerOnlyItemDeletesList();
  testBackspaceOnSoleMarkerOnlyItemMakesProgress();
  QApplication::clipboard()->clear();
  return 0;
}
