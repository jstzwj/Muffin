#include "EditorViewTestUtils.h"

#include "editor/EditorViewGeometry.h"
#include "render/DocumentLayout.h"

using namespace muffin;

void testEditorViewHitTestActivatesInlineSourceEditing() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());
  const QRectF blockRect = view.nodeRect(blockAt(session, 0)->id());
  require(!blockRect.isEmpty(), "view should layout inline paragraph");
  const InlineLayout* inlineLayout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(inlineLayout != nullptr, "view hit test should find inline layout");

  const QPointF documentPos = blockRect.topLeft() + inlineLayout->cursorRectForSourceOffset(11).center();
  HitTestResult hit = view.hitTest(documentPos);
  require(hit.isValid() && hit.zone == HitTestResult::Zone::Text, "view hit test should return text hit");
  require(hit.sourceOffset == 11, "view hit test source offset mismatch");

  controller.activateHit(hit);
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "view hit should resolve source offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing after view hit should edit inline");
  require(session.markdownText().toString() == QStringLiteral("before **boXld** after"), "view hit inline insert mismatch");
}

void testEditorViewInlineProjectionStateChanges() {
  DocumentSession session;
  EditorView view;
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const QRectF collapsedRect = view.nodeRect(block->id());
  const InlineLayout* collapsedLayout = view.blockAtViewportPos(collapsedRect.center())->inlineLayout();
  require(collapsedLayout != nullptr, "collapsed inline layout should exist");
  const QRectF collapsedCursor = collapsedLayout->cursorRectForSourceOffset(9);

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const InlineLayout* expandedLayout = view.blockAtViewportPos(expandedRect.center())->inlineLayout();
  require(expandedLayout != nullptr, "expanded inline layout should exist");
  const QRectF expandedCursor = expandedLayout->cursorRectForSourceOffset(9);
  require(expandedCursor.left() != collapsedCursor.left(), "cursor entering inline should expand marker layout");

  const QPointF expandedDocumentPos = expandedRect.topLeft() + expandedCursor.center();
  const HitTestResult expandedHit = view.hitTest(expandedDocumentPos);
  require(expandedHit.isValid() && expandedHit.sourceOffset == 9, "expanded inline hit-test should round-trip source offset");

  CursorPosition outside;
  outside.blockId = block->id();
  outside.text.nodeId = block->id();
  outside.text.textOffset = 0;
  outside.text.sourceOffset = 0;
  view.setCursorPosition(outside);
  const QRectF recollapsedRect = view.nodeRect(block->id());
  const InlineLayout* recollapsedLayout = view.blockAtViewportPos(recollapsedRect.center())->inlineLayout();
  require(recollapsedLayout != nullptr, "recollapsed inline layout should exist");
  require(recollapsedLayout->cursorRectForSourceOffset(9).left() == collapsedCursor.left(), "cursor leaving inline should collapse marker layout");

  SelectionRange selection;
  selection.anchor = inside;
  selection.focus = inside;
  selection.focus.text.textOffset = 3;
  selection.focus.text.sourceOffset = 11;
  view.setSelectionRange(selection);
  const QRectF selectedRect = view.nodeRect(block->id());
  const InlineLayout* selectedLayout = view.blockAtViewportPos(selectedRect.center())->inlineLayout();
  require(selectedLayout != nullptr, "selection inline layout should exist");
  // Reveal follows the focus: this selection's focus sits inside the bold inline, so its markers
  // stay expanded (only the focus inline reveals; the selection extent would not).
  require(selectedLayout->cursorRectForSourceOffset(9).left() != collapsedCursor.left(),
          "selection with focus inside inline should keep its markers expanded");
}

void testEditorViewInlineMarkerSourceSelection() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const NodeId blockId = block->id();

  CursorPosition inside = inlineCursor(blockId, QStringLiteral("before b").size(), QStringLiteral("before **b").size());
  view.setCursorPosition(inside);
  const QRectF expandedBlockRect = view.nodeRect(blockId);
  const InlineLayout* expandedLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker source"));

  const QRectF betweenStarsCursor = expandedLayout->cursorRectForSourceOffset(QStringLiteral("before *").size());
  require(!betweenStarsCursor.isEmpty(), "cursor between strong opener stars should exist");
  const HitTestResult betweenStarsHit = view.hitTest(expandedBlockRect.topLeft() + betweenStarsCursor.center());
  require(betweenStarsHit.isValid(), "hit between strong opener stars should be valid");
  require(betweenStarsHit.sourceOffset == QStringLiteral("before *").size(), "hit between strong opener stars should keep source offset");

  SelectionRange markerSelection;
  markerSelection.anchor = inlineCursor(blockId, QStringLiteral("before ").size(), QStringLiteral("before ").size());
  markerSelection.focus = inlineCursor(blockId, QStringLiteral("before ").size(), QStringLiteral("before **").size());
  view.setSelectionRange(markerSelection);
  const BlockLayout* selectedBlock = requireViewBlock(view, blockId, QStringLiteral("marker source selection"));
  const QVector<QRectF> markerRects = selectedBlock->selectionRects(markerSelection, RenderTheme::defaultTheme());
  require(!markerRects.isEmpty(), "strong opener marker source selection should draw");
  qreal markerWidth = 0;
  for (const QRectF& rect : markerRects) {
    markerWidth += rect.width();
  }
  require(markerWidth > 2.0, "strong opener marker source selection should have visible width");

  const QRectF currentBlockRect = view.nodeRect(blockId);
  const InlineLayout* currentLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker drag start"));
  const QPointF dragStart = currentBlockRect.topLeft() + currentLayout->cursorRectForSourceOffset(QStringLiteral("before ").size()).center();
  QMouseEvent press(
      QEvent::MouseButtonPress,
      dragStart,
      QPointF(dragStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  const QRectF dragBlockRect = view.nodeRect(blockId);
  const InlineLayout* dragLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker drag"));
  const QPointF dragEnd = dragBlockRect.topLeft() + dragLayout->cursorRectForSourceOffset(QStringLiteral("before **bold").size()).center();
  QMouseEvent move(
      QEvent::MouseMove,
      dragEnd,
      QPointF(dragEnd),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);
  require(!controller.selection().selection().isCollapsed(), "dragging from marker into content should create source selection");
  const qsizetype expectedAnchorSource = QStringLiteral("before ").size();
  const qsizetype expectedFocusSource = QStringLiteral("before **bold").size();
  require(controller.selection().selection().anchor.text.sourceOffset == expectedAnchorSource,
          "marker drag anchor should stay at opener source offset");
  require(controller.selection().selection().focus.text.sourceOffset == expectedFocusSource,
          "marker drag focus should stay at content source offset");
}

void testEditorViewInlineClickDoesNotSelectAfterMarkerExpansion() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **xyz** after"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* collapsedLayout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(collapsedLayout != nullptr, "inline click test collapsed layout should exist");

  const QPointF clickPos = blockRect.topLeft() + collapsedLayout->cursorRectForSourceOffset(11).center();
  QMouseEvent press(
      QEvent::MouseButtonPress,
      clickPos,
      QPointF(clickPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);
  require(controller.selection().hasCursor(), "inline click press should activate cursor");
  require(controller.selection().selection().isCollapsed(), "inline click press should keep collapsed selection");

  QMouseEvent release(
      QEvent::MouseButtonRelease,
      clickPos,
      QPointF(clickPos),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &release);
  require(controller.selection().selection().isCollapsed(), "inline click release should not create selection after marker expansion");
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "inline click release should keep original source cursor");
}

void testEditorViewDragSelectionContinuesAcrossMoves() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("abcdefghijklmnopqrstuvwxyz"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* layout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(layout != nullptr, "drag selection test inline layout should exist");

  const QPointF startPos = blockRect.topLeft() + layout->cursorRectForSourceOffset(0).center();
  const QPointF firstMovePos = blockRect.topLeft() + layout->cursorRectForSourceOffset(10).center();
  const QPointF secondMovePos = blockRect.topLeft() + layout->cursorRectForSourceOffset(20).center();

  QMouseEvent press(
      QEvent::MouseButtonPress,
      startPos,
      QPointF(startPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  QMouseEvent stationaryMove(
      QEvent::MouseMove,
      startPos,
      QPointF(startPos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &stationaryMove);
  require(controller.selection().selection().isCollapsed(), "stationary first drag move should keep collapsed cursor");

  QMouseEvent firstMove(
      QEvent::MouseMove,
      firstMovePos,
      QPointF(firstMovePos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &firstMove);
  require(!controller.selection().selection().isCollapsed(), "first drag move should create a selection");
  require(controller.selection().selection().focus.text.sourceOffset == 10, "first drag move focus offset mismatch");

  QMouseEvent secondMove(
      QEvent::MouseMove,
      secondMovePos,
      QPointF(secondMovePos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &secondMove);
  require(controller.selection().selection().focus.text.sourceOffset == 20, "second drag move should keep extending selection");

  QMouseEvent release(
      QEvent::MouseButtonRelease,
      secondMovePos,
      QPointF(secondMovePos),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &release);
  require(controller.selection().selection().focus.text.sourceOffset == 20, "drag release focus offset mismatch");
}

void testEditorViewVerticalDragSelectionHitsWrappedLine() {
  const QString markdown = QStringLiteral(
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau");
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText(markdown, false);

  const auto lineStartCursorsForWidth = [&session, &view](int width, MarkdownNode* block) {
    view.resize(width, 500);
    view.setDocument(session.document());
    const QRectF blockRect = view.nodeRect(block->id());
    const BlockLayout* blockLayout = view.blockAtViewportPos(blockRect.center());
    const InlineLayout* layout = blockLayout ? blockLayout->inlineLayout() : nullptr;
    QVector<QRectF> lineStartCursors;
    if (!layout) {
      return lineStartCursors;
    }
    for (qsizetype offset = 0; offset <= layout->plainText().size(); ++offset) {
      const QRectF cursor = layout->cursorRect(offset);
      require(!cursor.isEmpty(), QStringLiteral("vertical drag cursor %1 should exist").arg(offset));
      if (lineStartCursors.isEmpty() || cursor.center().y() > lineStartCursors.last().center().y() + 0.5) {
        lineStartCursors.push_back(cursor);
        if (lineStartCursors.size() == 2) {
          break;
        }
      }
    }
    return lineStartCursors;
  };

  MarkdownNode* block = blockAt(session, 0);
  QVector<QRectF> lineStartCursors;
  for (int width : {220, 180, 150, 130, 110}) {
    lineStartCursors = lineStartCursorsForWidth(width, block);
    if (lineStartCursors.size() >= 2) {
      break;
    }
  }
  view.setDocument(session.document());
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* layout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(layout != nullptr, "vertical drag test inline layout should exist");

  qsizetype firstLineOffset = -1;
  lineStartCursors.clear();
  for (qsizetype offset = 0; offset <= layout->plainText().size(); ++offset) {
    const QRectF cursor = layout->cursorRect(offset);
    require(!cursor.isEmpty(), QStringLiteral("vertical drag cursor %1 should exist").arg(offset));
    if (lineStartCursors.isEmpty() || cursor.center().y() > lineStartCursors.last().center().y() + 0.5) {
      lineStartCursors.push_back(cursor);
      if (lineStartCursors.size() == 1) {
        firstLineOffset = offset;
      } else if (lineStartCursors.size() == 2) {
        break;
      }
    }
  }
  require(firstLineOffset == 0, "vertical drag first line should start at offset 0");
  require(lineStartCursors.size() >= 2, "vertical drag fixture should wrap to a second line");

  const qreal dragX = lineStartCursors.at(0).center().x() + 40.0;
  const QPointF startPos(blockRect.left() + dragX, blockRect.top() + lineStartCursors.at(0).center().y());
  const QPointF secondLineSameX(blockRect.left() + dragX, blockRect.top() + lineStartCursors.at(1).center().y());

  QMouseEvent press(
      QEvent::MouseButtonPress,
      startPos,
      QPointF(startPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  QMouseEvent move(
      QEvent::MouseMove,
      secondLineSameX,
      QPointF(secondLineSameX),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  const QRectF focusCursor = view.hitTest(secondLineSameX).cursorRect;
  require(qAbs(focusCursor.center().y() - (blockRect.top() + lineStartCursors.at(1).center().y())) < 1.0,
          QStringLiteral("vertical drag hit should resolve to the second visual line"));
  require(!controller.selection().selection().isCollapsed(), "vertical drag should create a selection");
  require(controller.selection().selection().focus.text.sourceOffset == view.hitTest(secondLineSameX).sourceOffset,
          QStringLiteral("vertical drag focus should follow the second visual line hit"));
  const BlockLayout* selectedBlock = view.blockAtViewportPos(blockRect.center());
  require(selectedBlock != nullptr, "vertical drag wrapped paragraph block should stay visible");
  const QVector<QRectF> selectionRects = selectedBlock->selectionRects(controller.selection().selection(), RenderTheme::defaultTheme());
  require(!selectionRects.isEmpty(), "vertical drag should produce visible selection rects without horizontal pre-drag");
  bool hasSecondLineRect = false;
  for (const QRectF& rect : selectionRects) {
    if (qAbs(rect.center().y() - (blockRect.top() + lineStartCursors.at(1).center().y())) < 1.0) {
      hasSecondLineRect = true;
      break;
    }
  }
  require(hasSecondLineRect, "vertical drag selection rects should include the second visual line");
}

// Drag from the virtual trailing paragraph (below the last block) back up into
// an earlier block must select across blocks, treating the trailing position as
// the end of the last block — and the selection must serialize for copy.
void testKeyboardDownMovesWithinWrappedParagraphLine() {
  const QString markdown = QStringLiteral(
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau");
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(150, 500);
  session.setMarkdownText(markdown, false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const InlineLayout* layout = requireViewInlineLayout(view, block->id(), QStringLiteral("wrapped keyboard down"));
  require(layout->visualLineCount() >= 2, "keyboard down fixture should wrap to at least two visual lines");

  const qreal localX = layout->visualLineRect(0).left() + 35.0;
  const qsizetype startSource = layout->sourceOffsetAtVisualLineX(0, localX);
  const qsizetype expectedSource = layout->sourceOffsetAtVisualLineX(1, localX);
  setSourceCursor(controller.selection(), block, startSource, startSource);

  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down should be handled in wrapped paragraph");
  const CursorPosition cursor = controller.selection().cursorPosition();
  require(cursor.blockId == block->id(), "Down inside wrapped paragraph should stay in the same block");
  const InlineLayout* layoutAfter = requireViewInlineLayout(view, block->id(), QStringLiteral("wrapped keyboard down after"));
  require(layoutAfter->visualLineIndexForSourceOffset(cursor.text.sourceOffset) == 1, "Down should land on visual line 1");
  const qsizetype actualSource = layoutAfter->sourceOffsetAtVisualLineX(1, localX);
  require(cursor.text.sourceOffset == actualSource, "Down should preserve x on the next visual line");
  Q_UNUSED(expectedSource)
}

void testKeyboardVerticalCrossesParagraphsByVisualEdges() {
  const QString first = QStringLiteral(
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau");
  const QString second = QStringLiteral("next paragraph keeps going");
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(150, 500);
  session.setMarkdownText(first + QStringLiteral("\n\n") + second, false);
  view.setDocument(session.document());

  MarkdownNode* firstBlock = blockAt(session, 0);
  MarkdownNode* secondBlock = blockAt(session, 1);
  const InlineLayout* firstLayout = requireViewInlineLayout(view, firstBlock->id(), QStringLiteral("wrapped cross first"));
  requireViewInlineLayout(view, secondBlock->id(), QStringLiteral("wrapped cross second"));
  require(firstLayout->visualLineCount() >= 2, "cross-paragraph fixture should wrap first paragraph");

  const int lastLine = firstLayout->visualLineCount() - 1;
  const qreal localX = firstLayout->visualLineRect(lastLine).left() + 30.0;
  const qreal documentX = view.nodeRect(firstBlock->id()).left() + localX;
  setSourceCursor(controller.selection(), firstBlock,
                  firstLayout->sourceOffsetAtVisualLineX(lastLine, localX),
                  firstLayout->sourceOffsetAtVisualLineX(lastLine, localX));

  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down from last visual line should be handled");
  CursorPosition cursor = controller.selection().cursorPosition();
  require(cursor.blockId == secondBlock->id(), "Down from last visual line should enter next paragraph");
  const InlineLayout* secondLayoutDown = requireViewInlineLayout(view, secondBlock->id(), QStringLiteral("wrapped cross second after down"));
  const qsizetype expectedSecondSource = secondLayoutDown->sourceOffsetAtVisualLineX(0, documentX - view.nodeRect(secondBlock->id()).left());
  require(cursor.text.sourceOffset == secondBlock->sourceRange().byteStart + expectedSecondSource,
          "Down into next paragraph should preserve document x on first visual line");
  require(secondLayoutDown->visualLineIndexForSourceOffset(cursor.text.sourceOffset - secondBlock->sourceRange().byteStart) == 0,
          "Down into next paragraph should land on its first visual line");

  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up should return to previous paragraph");
  cursor = controller.selection().cursorPosition();
  require(cursor.blockId == firstBlock->id(), "Up from first visual line should return to previous paragraph");
  const InlineLayout* firstLayoutUp = requireViewInlineLayout(view, firstBlock->id(), QStringLiteral("wrapped cross first after up"));
  require(firstLayoutUp->visualLineIndexForSourceOffset(cursor.text.sourceOffset - firstBlock->sourceRange().byteStart) == lastLine,
          "Up should land on the previous paragraph's last visual line");
}

HitTestResult tableCellHit(EditorView& view, NodeId tableId, int row, int column, qsizetype localSource) {
  const BlockLayout* table = requireViewBlock(view, tableId, QStringLiteral("keyboard table fresh"));
  const auto& tableRows = table->tableRows();
  require(row >= 0 && row < static_cast<int>(tableRows.size()), "table hit row out of range");
  const auto& cells = tableRows.at(static_cast<size_t>(row)).cells;
  require(column >= 0 && column < static_cast<int>(cells.size()), "table hit column out of range");
  const auto& cell = cells.at(static_cast<size_t>(column));
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table->nodeId();
  hit.textNodeId = cell.nodeId;
  hit.tableRow = row;
  hit.tableColumn = column;
  hit.textOffset = localSource;
  hit.sourceOffset = cell.contentSourceStart + localSource;
  hit.blockRect = table->rect();
  hit.cursorRect = cell.text.cursorRectForSourceOffset(localSource).translated(cell.rect.topLeft());
  return hit;
}

void testKeyboardTableArrowsMoveByCellGrid() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral(
      "before\n\n"
      "| A | B |\n"
      "|---|---|\n"
      "| c1 | d1 |\n"
      "| c2 | d2 |\n"
      "\n"
      "after"), false);
  view.setDocument(session.document());

  MarkdownNode* tableNode = blockAt(session, 1);
  require(requireViewBlock(view, tableNode->id(), QStringLiteral("keyboard table"))->tableRows().size() >= 3,
          "table fixture should expose header and two body rows");
  const NodeId tableId = tableNode->id();

  controller.activateHit(tableCellHit(view, tableId, 2, 1, 1));
  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up in table should be handled");
  HitTestResult hit = controller.selection().currentHit();
  require(hit.zone == HitTestResult::Zone::TableCell, "Up in table should preserve table hit zone");
  require(hit.tableRow == 1 && hit.tableColumn == 1, "Up in table should move to the cell above");

  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down in table should be handled");
  hit = controller.selection().currentHit();
  require(hit.tableRow == 2 && hit.tableColumn == 1, "Down in table should move back to the cell below");

  controller.activateHit(tableCellHit(view, tableId, 1, 1, 0));
  require(pressKey(controller.inputController(), &view, Qt::Key_Left), "Left from a cell start should be handled");
  hit = controller.selection().currentHit();
  require(hit.tableRow == 1 && hit.tableColumn == 0, "Left from cell start should move to previous cell");
  require(hit.textOffset == 2, "Left from cell start should land at previous cell end");

  controller.activateHit(tableCellHit(view, tableId, 1, 1, 2));
  require(pressKey(controller.inputController(), &view, Qt::Key_Right), "Right from a row-end cell should be handled");
  hit = controller.selection().currentHit();
  require(hit.tableRow == 2 && hit.tableColumn == 0, "Right from row end should wrap to next row first cell");
  require(hit.textOffset == 0, "Right from row end should land at next cell start");
}

// Up/Down inside a list move between sibling items at the preserved horizontal column. A nested
// caret used to be a silent no-op because the cross-block fallthrough only walked top-level blocks.
void testKeyboardUpDownMovesBetweenListItems() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 500);  // wide enough that each short item is a single visual line

  session.setMarkdownText(QStringLiteral("- alpha\n- bravo\n- charlie"), false);
  view.setDocument(session.document());
  MarkdownNode* item0 = listItemAt(session, 0, 0);
  MarkdownNode* item1 = listItemAt(session, 0, 1);
  MarkdownNode* item2 = listItemAt(session, 0, 2);
  require(item0 != nullptr && item1 != nullptr && item2 != nullptr, "fixture should build three list items");

  setCursor(controller.selection(), item0, 2);  // "al|pha"
  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down inside a list should be handled");
  CursorPosition cursor = controller.selection().cursorPosition();
  require(cursor.blockId == item1->id(), "Down should move to the next list item");
  require(cursor.text.textOffset == 2, "Down should preserve the column across list items");

  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down to the third item should be handled");
  cursor = controller.selection().cursorPosition();
  require(cursor.blockId == item2->id(), "Down should move to the third list item");

  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up should return to the previous item");
  cursor = controller.selection().cursorPosition();
  require(cursor.blockId == item1->id(), "Up should move back to the second list item");
  require(cursor.text.textOffset == 2, "Up should preserve the column across list items");
}

// Up from the first list item and Down from the last list item leave the list for the neighbouring
// block (the container-climbing fallthrough reaches the block before/after the whole list).
void testKeyboardUpDownLeavesListAtBoundaries() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 500);

  session.setMarkdownText(QStringLiteral("before\n\n- alpha\n- bravo\n\nafter"), false);
  view.setDocument(session.document());
  MarkdownNode* before = blockAt(session, 0);
  MarkdownNode* firstItem = listItemAt(session, 1, 0);
  MarkdownNode* lastItem = listItemAt(session, 1, 1);
  MarkdownNode* after = blockAt(session, 2);
  require(before != nullptr && firstItem != nullptr && lastItem != nullptr && after != nullptr,
          "fixture should have before / list / after");

  setCursor(controller.selection(), firstItem, 0);
  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up from the first item should be handled");
  require(controller.selection().cursorPosition().blockId == before->id(),
          "Up from the first list item should leave the list to the previous block");

  setCursor(controller.selection(), lastItem, 0);
  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down from the last item should be handled");
  require(controller.selection().cursorPosition().blockId == after->id(),
          "Down from the last list item should leave the list to the next block");
}

// Down from a parent item that contains a nested sublist descends to its first nested child, then
// onwards through the nested children to the parent's sibling; Up retraces the exact same path.
void testKeyboardUpDownDescendsIntoAndOutOfNestedSublist() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 500);

  session.setMarkdownText(QStringLiteral("- parent\n  - child one\n  - child two\n- sibling"), false);
  view.setDocument(session.document());
  MarkdownNode* parent = listItemAt(session, 0, 0);
  MarkdownNode* sibling = listItemAt(session, 0, 1);
  require(parent != nullptr && sibling != nullptr, "fixture should build parent and sibling items");
  MarkdownNode* nestedList = nullptr;
  for (const auto& child : parent->children()) {
    if (child->type() == BlockType::List) {
      nestedList = child.get();
      break;
    }
  }
  require(nestedList != nullptr, "parent item should contain a nested list");
  MarkdownNode* childOne = childAt(nestedList, 0);
  MarkdownNode* childTwo = childAt(nestedList, 1);
  require(childOne != nullptr && childTwo != nullptr, "nested list should expose two child items");

  setCursor(controller.selection(), parent, 0);
  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down from the parent should be handled");
  require(controller.selection().cursorPosition().blockId == childOne->id(),
          "Down from a parent item should descend to its first nested child, not skip to the sibling");
  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down to the second child should be handled");
  require(controller.selection().cursorPosition().blockId == childTwo->id(),
          "Down should move to the second nested child");
  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down to the sibling should be handled");
  require(controller.selection().cursorPosition().blockId == sibling->id(),
          "Down from the last nested child should move to the parent's sibling");

  // Up retraces the path: sibling -> childTwo -> childOne -> parent.
  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up from the sibling should be handled");
  require(controller.selection().cursorPosition().blockId == childTwo->id(),
          "Up from the sibling should return to the last nested child, not the parent");
  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up to the first child should be handled");
  require(controller.selection().cursorPosition().blockId == childOne->id(),
          "Up should return to the first nested child");
  require(pressKey(controller.inputController(), &view, Qt::Key_Up), "Up to the parent should be handled");
  require(controller.selection().cursorPosition().blockId == parent->id(),
          "Up from the first nested child should return to the parent item");
}

// Drag from the virtual trailing paragraph (below the last block) back up into
// an earlier block must select across blocks, treating the trailing position as
// the end of the last block — and the selection must serialize for copy.
void testEditorViewDragFromTrailingParagraphSelectsBack() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  view.setDocument(session.document());
  MarkdownNode* alpha = blockAt(session, 0);
  MarkdownNode* beta = blockAt(session, 1);
  const QRectF alphaRect = view.nodeRect(alpha->id());
  const QRectF betaRect = view.nodeRect(beta->id());
  require(!alphaRect.isEmpty() && !betaRect.isEmpty(), "two-paragraph fixture should lay out both blocks");
  const InlineLayout* alphaInline = requireViewInlineLayout(view, alpha->id(), QStringLiteral("alpha"));

  const RenderTheme theme = view.theme();
  // Press on the virtual trailing paragraph below beta, then drag up into alpha.
  const QPointF trailingPos(betaRect.left(), betaRect.bottom() + theme.blockSpacing() + 5.0);
  const HitTestResult trailingHit = view.hitTest(trailingPos);
  require(trailingHit.zone == HitTestResult::Zone::BlockAfter, "press point below last block should be BlockAfter");
  require(trailingHit.textOffset == 4, "BlockAfter hit should resolve to the last block's selectable length (4 for \"beta\")");

  const QPointF alphaPos = alphaRect.topLeft() + alphaInline->cursorRect(2).center();

  QMouseEvent press(QEvent::MouseButtonPress, trailingPos, QPointF(trailingPos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);
  QMouseEvent move(QEvent::MouseMove, alphaPos, QPointF(alphaPos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  const SelectionRange selection = controller.selection().selection();
  require(!selection.isCollapsed(), "dragging from the trailing paragraph should create a selection");
  require(selection.anchor.blockId != selection.focus.blockId, "trailing drag should span both paragraphs");
  require(selection.focus.blockId == alpha->id(), "trailing drag focus should land in the alpha block");
  require(selectedMarkdown(session, controller.selection()) == QStringLiteral("pha\n\nbeta"),
          "trailing drag should select from alpha offset 2 through the end of beta");
}

// Regression: dragging UP from a nested list item to before its parent list item painted NOTHING,
// while the forward drag highlighted fine. The parent and the nested item share one top-level List
// slot (topLevelIndexFor returns the same index for both), so the old blocksBetween walk — which
// assumed it reached the start endpoint before the end one — met the focus (parent, above) before
// the anchor (nested, below), collected nothing, and paintSelection drew zero rects. blocksBetween
// is now direction-independent, so the same blocks must come back either way.
void testEditorViewBackwardNestedSelectionCoversBothBlocks() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("- Third item\n  - Nested item\n  - Another nested item"), false);

  MarkdownNode* list = blockAt(session, 0);
  require(list != nullptr && list->type() == BlockType::List, "fixture should parse as one top-level list");
  MarkdownNode* thirdItem = childAt(list, 0);
  require(thirdItem != nullptr && thirdItem->type() == BlockType::ListItem, "first list item should exist");
  const NodeId thirdId = thirdItem->id();

  // Locate the nested list item (a ListItem nested inside thirdItem, i.e. not thirdItem itself),
  // walking whatever intermediate Paragraph/nested-List nodes cmark inserts.
  NodeId nestedId;
  const auto findNestedItem = [&](const auto& self, const MarkdownNode& node) -> void {
    if (nestedId.isValid()) {
      return;
    }
    for (const auto& child : node.children()) {
      if (child->type() == BlockType::ListItem && child->id() != thirdId) {
        nestedId = child->id();
        return;
      }
      self(self, *child);
    }
  };
  findNestedItem(findNestedItem, *thirdItem);
  require(nestedId.isValid(), "fixture should contain a nested list item");
  require(nestedId != thirdId, "nested item must differ from its parent");

  DocumentLayout layout;
  layout.rebuild(session.document(), RenderTheme::defaultTheme(), 1000.0);
  require(layout.topLevelIndexFor(thirdId) == layout.topLevelIndexFor(nestedId),
          "parent and nested item must share one top-level List slot");
  require(layout.topLevelIndexFor(thirdId) >= 0, "list slot should be built");

  const QVector<const BlockLayout*> forward = editor_geometry::blocksBetween(layout, thirdId, nestedId);
  const QVector<const BlockLayout*> backward = editor_geometry::blocksBetween(layout, nestedId, thirdId);
  require(!forward.isEmpty() && !backward.isEmpty(), "a same-slot selection must cover blocks in both directions");
  require(backward.size() == forward.size(), "backward drag must cover exactly the same blocks as forward");

  bool forwardHasThird = false, forwardHasNested = false;
  bool backwardHasThird = false, backwardHasNested = false;
  for (const BlockLayout* block : forward) {
    if (block->nodeId() == thirdId) forwardHasThird = true;
    if (block->nodeId() == nestedId) forwardHasNested = true;
  }
  for (const BlockLayout* block : backward) {
    if (block->nodeId() == thirdId) backwardHasThird = true;
    if (block->nodeId() == nestedId) backwardHasNested = true;
  }
  require(forwardHasThird && forwardHasNested, "forward selection must span both endpoints");
  require(backwardHasThird && backwardHasNested, "backward selection must span both endpoints");

  require(editor_geometry::blockComesBefore(layout, thirdId, nestedId), "parent should precede the nested item");
  require(!editor_geometry::blockComesBefore(layout, nestedId, thirdId), "nested item should not precede its parent");
}

// Regression for the double-highlight that surfaced once backward nested-list selection started
// painting. paintSelection iterated the blocksBetween list — which contains a list item AND its
// nested descendants — and called the RECURSIVE selectionRectsForOffsets on each entry, so every
// nested item was painted once by its own entry and again by each owning ancestor's recursion: a
// darker, double-painted band. The fix paints each block's OWN content only
// (selectionRectsSelfForOffsets). This pins the primitive: the parent's own selection rects stay in
// the parent's text row and never reach the nested item, whereas the recursive variant does reach
// it — which is exactly why it must not be used over a pre-flattened block list.
void testListItemOwnSelectionRectsDoNotLeakToNested() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("- Third item\n  - Nested item\n  - Another nested item"), false);

  MarkdownNode* list = blockAt(session, 0);
  require(list != nullptr && list->type() == BlockType::List, "fixture should parse as one top-level list");
  MarkdownNode* thirdItem = childAt(list, 0);
  const NodeId thirdId = thirdItem->id();

  NodeId nestedId;
  const auto findNestedItem = [&](const auto& self, const MarkdownNode& node) -> void {
    if (nestedId.isValid()) {
      return;
    }
    for (const auto& child : node.children()) {
      if (child->type() == BlockType::ListItem && child->id() != thirdId) {
        nestedId = child->id();
        return;
      }
      self(self, *child);
    }
  };
  findNestedItem(findNestedItem, *thirdItem);
  require(nestedId.isValid(), "fixture should contain a nested list item");

  DocumentLayout layout;
  const RenderTheme theme = RenderTheme::defaultTheme();
  layout.rebuild(session.document(), theme, 1000.0);
  const BlockLayout* thirdBlock = layout.block(thirdId);
  const BlockLayout* nestedBlock = layout.block(nestedId);
  require(thirdBlock != nullptr && nestedBlock != nullptr, "both list items should be laid out");
  require(thirdBlock->inlineLayout() != nullptr && nestedBlock->inlineLayout() != nullptr, "both items should have inline layouts");

  const qreal thirdTop = thirdBlock->rect().top();
  const qreal thirdTextBottom = thirdTop + thirdBlock->inlineLayout()->height();
  const qreal nestedTop = nestedBlock->rect().top();
  require(nestedTop >= thirdTextBottom - 1.0, "nested item should lay out below its parent's own text row");

  const qsizetype thirdLen = thirdBlock->inlineLayout()->plainText().size();
  const QVector<QRectF> ownRects = thirdBlock->selectionRectsSelfForOffsets(0, thirdLen, theme);
  require(!ownRects.isEmpty(), "parent own selection should produce rects");
  for (const QRectF& r : ownRects) {
    require(r.bottom() <= thirdTextBottom + 1.0,
            "self-only selection rects must stay in the parent's own text row (no descendant leak)");
  }

  // The recursive variant DOES descend into children — this is the call that double-painted when
  // used over the flattened list, and the reason paintSelection now uses the self-only form.
  const QVector<QRectF> recursiveRects = thirdBlock->selectionRectsForOffsets(0, thirdLen, theme);
  require(recursiveRects.size() > ownRects.size(), "recursive selection must descend into nested children");
  bool reachedNestedRow = false;
  for (const QRectF& r : recursiveRects) {
    if (r.top() >= nestedTop - 1.0) {
      reachedNestedRow = true;
      break;
    }
  }
  require(reachedNestedRow, "recursive selection must reach the nested item's row (the leak the self-only fix removes)");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testEditorViewHitTestActivatesInlineSourceEditing);
  RUN_TEST(testEditorViewInlineProjectionStateChanges);
  RUN_TEST(testEditorViewInlineMarkerSourceSelection);
  RUN_TEST(testEditorViewInlineClickDoesNotSelectAfterMarkerExpansion);
  RUN_TEST(testEditorViewDragSelectionContinuesAcrossMoves);
  RUN_TEST(testEditorViewVerticalDragSelectionHitsWrappedLine);
  RUN_TEST(testKeyboardDownMovesWithinWrappedParagraphLine);
  RUN_TEST(testKeyboardVerticalCrossesParagraphsByVisualEdges);
  RUN_TEST(testKeyboardTableArrowsMoveByCellGrid);
  RUN_TEST(testKeyboardUpDownMovesBetweenListItems);
  RUN_TEST(testKeyboardUpDownLeavesListAtBoundaries);
  RUN_TEST(testKeyboardUpDownDescendsIntoAndOutOfNestedSublist);
  RUN_TEST(testEditorViewDragFromTrailingParagraphSelectsBack);
  RUN_TEST(testEditorViewBackwardNestedSelectionCoversBothBlocks);
  RUN_TEST(testListItemOwnSelectionRectsDoNotLeakToNested);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
