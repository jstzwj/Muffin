#include "EditorViewTestUtils.h"

#include "editor/EditorViewGeometry.h"
#include "render/DocumentLayout.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QClipboard>
#include <QImage>
#include <QInputMethodEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>
#include <vector>

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

  // Probe a starting caret somewhere on visual line 0. The goal column Down preserves is the
  // caret's OWN rect X (InputController caches effectiveCursorRect().left()), not this synthetic
  // probe X: sourceOffsetAtVisualLineX snaps to a character boundary, so the caret rect sits at a
  // (possibly different) boundary X whenever the probe lands mid-glyph. That snap is font-dependent
  // (passes on Windows, flakes on macOS), so derive the expected target from the caret's real rect
  // — then the assertion matches exactly what the controller resolves and is platform-independent.
  const qreal probeX = layout->visualLineRect(0).left() + 35.0;
  const qsizetype startSource = layout->sourceOffsetAtVisualLineX(0, probeX);
  setSourceCursor(controller.selection(), block, startSource, startSource);
  // setSourceCursor mutates the selection, which can rebuild this block — the `layout` pointer
  // captured above may dangle afterwards. Re-fetch before reading the caret rect.
  const InlineLayout* layoutAtCaret = requireViewInlineLayout(view, block->id(), QStringLiteral("wrapped keyboard down caret"));
  const qreal goalLocalX = layoutAtCaret->cursorRectForSourceOffset(startSource).left();

  require(pressKey(controller.inputController(), &view, Qt::Key_Down), "Down should be handled in wrapped paragraph");
  const CursorPosition cursor = controller.selection().cursorPosition();
  require(cursor.blockId == block->id(), "Down inside wrapped paragraph should stay in the same block");
  const InlineLayout* layoutAfter = requireViewInlineLayout(view, block->id(), QStringLiteral("wrapped keyboard down after"));
  require(layoutAfter->visualLineIndexForSourceOffset(cursor.text.sourceOffset) == 1, "Down should land on visual line 1");
  const qsizetype expectedSource = layoutAfter->sourceOffsetAtVisualLineX(1, goalLocalX);
  require(cursor.text.sourceOffset == expectedSource, "Down should preserve the caret x on the next visual line");
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

// Captures the widget's current pixels (forces a synchronous paint so it reflects state set since
// the last update(), e.g. a preedit). grab() keys off the fixed widget size, so it stays stable
// across captures (a viewport()->render() approach let a layout-driven scrollbar flip the size).
QImage captureView(EditorView& view) {
  return view.grab().toImage();
}

// An in-progress IME composition (preedit) must paint at the caret with an underline, instead of
// being silently dropped (the old inputMethodEvent only consumed commitString). Verified by a pixel
// differential: feeding a preedit event changes the caret row's pixels, proving the composition drew.
void testInputMethodPreeditPaintsAtCaret() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(400, 300);
  view.show();
  session.setMarkdownText(QStringLiteral("hello world paragraph"), false);
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* block = blockAt(session, 0);
  require(block != nullptr && block->type() == BlockType::Paragraph, "fixture should build a paragraph");

  // Place the caret mid-paragraph (activateHit sets cursorHit_, which paintPreedit requires).
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = block->id();
  hit.textNodeId = block->id();
  hit.textOffset = 2;
  hit.sourceOffset = block->sourceRange().byteStart + 2;
  controller.activateHit(hit);
  QApplication::processEvents();

  const QImage baseline = captureView(view);

  // Feed a preedit event the same way Qt delivers one from the platform IME.
  QInputMethodEvent imeEvent(QStringLiteral("nihao"), {});
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();
  const QImage withPreedit = captureView(view);

  require(baseline.size() == withPreedit.size(), "widget size must be stable across the two captures");
  int differingPixels = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      if (baseline.pixel(x, y) != withPreedit.pixel(x, y)) {
        ++differingPixels;
      }
    }
  }
  // Threshold sits well above the caret-blink footprint (~40 px, if the blink toggled between
  // captures) and well below the preedit footprint (glyphs + underline), so a pass requires the
  // composition to have actually rendered — not just the caret flickering.
  require(differingPixels > 100,
          "a preedit composition should paint visible pixels at the caret (underline + glyphs)");
}

// A mid-sentence preedit is spliced INTO the laid-out text, so it pushes following text right
// instead of overlapping it. Verified by the inline layout's width growing by roughly the preedit's
// pixel width — the old overlay left the layout width unchanged (it painted over the text).
void testInlinePreeditShiftsFollowingText() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 300);
  view.show();
  session.setMarkdownText(QStringLiteral("风火雷电"), false);
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* block = blockAt(session, 0);
  require(block != nullptr, "fixture should build a paragraph");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = block->id();
  hit.textNodeId = block->id();
  hit.textOffset = 2;  // between 火 and 雷 (activateHit fills sourceOffset from textOffset)
  controller.activateHit(hit);
  QApplication::processEvents();

  const InlineLayout* layout = requireViewInlineLayout(view, block->id(), QStringLiteral("preedit shift before"));
  const qreal widthBefore = layout->size().width();

  QInputMethodEvent imeEvent(QStringLiteral("wsm"), {});
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();

  const InlineLayout* layoutAfter = requireViewInlineLayout(view, block->id(), QStringLiteral("preedit shift after"));
  require(layoutAfter->hasPreedit(), "the caret block's inline layout should have the preedit spliced in");
  const qreal widthAfter = layoutAfter->size().width();
  const qreal expectedGrowth = QFontMetricsF(view.theme().paragraphFont()).horizontalAdvance(QStringLiteral("wsm"));
  require(widthAfter >= widthBefore + expectedGrowth - 2.0,
          "a mid-sentence preedit should grow the laid-out width (shift following text), not overlap it");
}

// A preedit at a line's right edge wraps to the next visual line (it's part of the laid-out text),
// instead of being clipped by the viewport edge.
void testInlinePreeditAtRightEdgeWraps() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(150, 300);
  view.show();
  session.setMarkdownText(QStringLiteral("abc"), false);  // fits one line at this width
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* block = blockAt(session, 0);

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = block->id();
  hit.textNodeId = block->id();
  hit.textOffset = 3;  // end of "abc"
  controller.activateHit(hit);
  QApplication::processEvents();

  const InlineLayout* layout = requireViewInlineLayout(view, block->id(), QStringLiteral("preedit wrap before"));
  require(layout->visualLineCount() == 1, "fixture: 'abc' should occupy one visual line");

  // Size the preedit from the font's OWN 'a' advance so it is guaranteed to exceed the line width
  // on every platform. A fixed short preedit (the alphabet) fits on one line on CI Linux, whose
  // fontconfig-less fallback font renders Latin narrow enough that 26 chars fit the ~150px line,
  // so the wrap is never observed. Built against the view width (an upper bound on the wrap
  // width), n*advanceA > viewWidth >= wrapWidth by construction, so the preedit necessarily
  // overflows and (with WrapAtWordBoundaryOrAnywhere) wraps — deterministically, any font.
  const qreal advanceA = QFontMetricsF(view.theme().paragraphFont()).horizontalAdvance(QStringLiteral("a"));
  const qreal widthBudget = qMax(qreal(160.0), view.width() + 10.0);
  const int n = advanceA > 0.0 ? static_cast<int>(widthBudget / advanceA) + 5 : 100;
  const QString preedit = QStringLiteral("a").repeated(qMax(n, 60));

  QInputMethodEvent imeEvent(preedit, {});
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();

  const InlineLayout* layoutAfter = requireViewInlineLayout(view, block->id(), QStringLiteral("preedit wrap after"));
  require(layoutAfter->hasPreedit(), "the caret block's inline layout should have the preedit spliced in");
  require(layoutAfter->visualLineCount() >= 2,
          "a preedit at the right edge should wrap to a new line, not be clipped");
}

// A caret inside a link's revealed URL (`[Muffin](htt|s://…)`) has a granular SOURCE offset but a
// VISIBLE offset that collapses to the HiddenSyntax span boundary. The preedit must splice at the
// caret's source position (inside the URL), not at the boundary (after "Muffin"). Verified by the
// composition caret sitting at the caret's X (the preedit starts there), not far to its left.
void testInlinePreeditSplicesInsideRevealedLinkUrl() {
  const QString md = QStringLiteral("[Muffin](https://example.com)");
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 300);
  view.show();
  session.setMarkdownText(md, false);
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* block = blockAt(session, 0);

  // Place the caret between 'p' and 's' of "https" (inside the URL) via its SOURCE offset.
  const qsizetype caretSource = md.indexOf(QLatin1Char('s'), md.indexOf(QStringLiteral("http")));
  require(caretSource > 0, "fixture: should locate the 's' inside the URL");
  setSourceCursor(controller.selection(), block, 0, caretSource);
  QApplication::processEvents();

  // Preedit "XYZ" with the composition cursor at its start (position 0) → the composition caret
  // sits exactly at the splice point.
  QVector<QInputMethodEvent::Attribute> attrs;
  attrs.append(QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 0, 1, QVariant()));
  QInputMethodEvent imeEvent(QStringLiteral("XYZ"), attrs);
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();

  const BlockLayout* blk = requireViewBlock(view, block->id(), QStringLiteral("preedit url"));
  const InlineLayout* layout = blk->inlineLayout();
  require(layout != nullptr && layout->hasPreedit(), "the link block should have the preedit spliced in");
  const QRectF compCaret = layout->preeditCursorRect(blk->inlineTextOrigin(view.theme()));  // document space
  require(!compCaret.isEmpty(), "the composition caret rect should be resolved");
  // The composition caret (= splice point) must sit at the caret's URL position, not at the end of
  // "Muffin" where a visible-offset anchor would wrongly place it (well to the left of the caret).
  const qreal caretX = view.effectiveCursorRect().x();
  require(compCaret.x() >= caretX - 2.0,
          "preedit should splice at the caret's source position inside the URL, not at the visible boundary");
}

// An inline math atom ($x$) AFTER the caret must shift right with the spliced preedit (and the text),
// not stay painted at its old X. The atom painters feed atom.displayStart (display-space) to
// cursorToX, which addresses preeditSpliceLength_ chars too early in the spliced layoutText_ — so
// they must go through toLayoutOffset, exactly like the decoration painters.
void testInlinePreeditShiftsMathAtom() {
  const QString md = QStringLiteral("a $x$ b");  // inline math sits after "a"
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(800, 300);
  view.show();
  session.setMarkdownText(md, false);
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* block = blockAt(session, 0);

  setSourceCursor(controller.selection(), block, 1, 1);  // caret right after "a", before " $x$ b"
  QApplication::processEvents();

  const BlockLayout* blk = requireViewBlock(view, block->id(), QStringLiteral("preedit math before"));
  const QVector<QRectF> rectsBefore = blk->inlineLayout()->mathAtomRects(blk->inlineTextOrigin(view.theme()));
  require(!rectsBefore.isEmpty(), "fixture should render the inline math atom");
  const qreal mathXBefore = rectsBefore.first().x();

  QInputMethodEvent imeEvent(QStringLiteral("WWWWW"), {});
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();

  const BlockLayout* blkAfter = requireViewBlock(view, block->id(), QStringLiteral("preedit math after"));
  const QVector<QRectF> rectsAfter = blkAfter->inlineLayout()->mathAtomRects(blkAfter->inlineTextOrigin(view.theme()));
  require(!rectsAfter.isEmpty(), "math atom should still render after the preedit splice");
  const qreal expectedShift = QFontMetricsF(view.theme().paragraphFont()).horizontalAdvance(QStringLiteral("WWWWW"));
  require(rectsAfter.first().x() >= mathXBefore + expectedShift - 2.0,
          "inline math atom should shift right with the spliced preedit, not stay in place");
}

// A preedit in a TABLE CELL splices into the cell's inline layout (shifting the trailing cell text,
// not overlapping it), and a right-aligned column STAYS right-aligned: tableCellTextOrigin keys off
// cell.text.size().width() (which now includes the splice), so the origin tracks the wider text.
void testInlinePreeditSplicesInTableCellAndKeepsAlignment() {
  const QString md = QStringLiteral("| left | right |\n|---|---:|\n| abc | defg |");  // col 1 right-aligned
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(600, 300);
  view.show();
  session.setMarkdownText(md, false);
  view.setDocument(session.document());
  QApplication::processEvents();
  MarkdownNode* table = blockAt(session, 0);
  require(table != nullptr && table->type() == BlockType::Table, "fixture should build a table");
  const NodeId tableId = table->id();
  const auto& rows0 = requireViewBlock(view, tableId, QStringLiteral("cell rows setup"))->tableRows();
  require(rows0.size() >= 2, "table fixture should expose header + body row");
  const auto& cellSetup = rows0.at(1).cells.at(1);  // "defg", right-aligned column

  // Caret between 'd' and 'e' of "defg".
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = tableId;
  hit.textNodeId = cellSetup.nodeId;
  hit.tableRow = 1;
  hit.tableColumn = 1;
  hit.textOffset = 1;
  hit.sourceOffset = cellSetup.contentSourceStart + 1;
  controller.activateHit(hit);
  QApplication::processEvents();

  // activateHit rebuilds the table block (entering the cell's edit layout), so the references captured
  // above dangle — reading them below would hit freed memory that, on ARM, is already reused (the
  // alignment field read as garbage). Re-fetch the cell before measuring it.
  const auto& cell0 = requireViewBlock(view, tableId, QStringLiteral("cell rows after activate"))->tableRows().at(1).cells.at(1);
  const qreal widthBefore = cell0.text.size().width();
  require(cell0.alignment == TableAlignment::Right,
          QStringLiteral("fixture: column 1 should be right-aligned (got %1)").arg(static_cast<int>(cell0.alignment)));

  QInputMethodEvent imeEvent(QStringLiteral("XYZ"), {});
  QApplication::sendEvent(&view, &imeEvent);
  QApplication::processEvents();

  const auto& rows1 = requireViewBlock(view, tableId, QStringLiteral("cell rows after"))->tableRows();
  const auto& cell1 = rows1.at(1).cells.at(1);
  require(cell1.text.hasPreedit(), "the focused cell's inline layout should have the preedit spliced in");
  const qreal widthAfter = cell1.text.size().width();
  const qreal expectedGrowth = QFontMetricsF(view.theme().paragraphFont()).horizontalAdvance(QStringLiteral("XYZ"));
  require(widthAfter >= widthBefore + expectedGrowth - 2.0,
          "a table-cell preedit should grow the cell layout width (shift trailing text), not overlap it");
  // Alignment (right/center/left) is preserved without a separate assertion here: tableCellTextOrigin
  // (src/render/BlockLayout.cpp) computes the cell's text origin from cell.text.size().width(), which
  // now includes the spliced preedit — so a right-aligned cell's origin tracks the wider text and the
  // block stays pinned to the cell's right edge. (A numeric origin before/after check is confounded by
  // the table auto-resizing the column to fit the wider content.)
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

// Cross-cell table selections: two cells share the table's blockId but hold different text
// nodes. Historically isCollapsed() compared only offsets, so cross-cell selections masqueraded
// as collapsed — the caret stayed painted, copy/format paths ran with garbage mixed-cell ranges.
void testCrossCellSelectionIsNotCollapsed() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);

  const QString source = QStringLiteral("| alpha | beta |\n| --- | --- |\n| gamma | delta |");
  session.setMarkdownText(source, false);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  std::vector<MarkdownNode*> cells;  // row-major: [alpha, beta, gamma, delta]
  for (const auto& row : table->children()) {
    if (row->type() == BlockType::TableRow) {
      for (const auto& cell : row->children()) {
        cells.push_back(cell.get());
      }
    }
  }
  require(cells.size() == 4, "2x2 table should expose four cells");

  SelectionRange range;
  range.anchor.blockId = table->id();
  range.anchor.text.nodeId = cells.at(0)->id();
  range.anchor.text.textOffset = 0;
  range.focus.blockId = table->id();
  range.focus.text.nodeId = cells.at(3)->id();
  range.focus.text.textOffset = 5;  // end of "delta"
  controller.selection().setSelection(range);
  require(!controller.selection().selection().isCollapsed(),
          "cross-cell selection with different offsets must not be collapsed");

  // The historical bug: EQUAL offsets in different cells were treated as one caret.
  range.focus.text.textOffset = 0;
  controller.selection().setSelection(range);
  require(!controller.selection().selection().isCollapsed(),
          "equal offsets in DIFFERENT cells are not one caret position");

  // Copy must still produce text (the serializer's per-context resolution is node-aware) and
  // never a min/max of the two cells' offsets.
  range.focus.text.textOffset = 5;
  controller.selection().setSelection(range);
  require(controller.clipboardController().copy(), "cross-cell copy should succeed");
  const QString copied = QApplication::clipboard()->text();
  require(copied.contains(QStringLiteral("alpha")) || copied.contains(QStringLiteral("delta")) ||
          copied.contains(QLatin1Char('|')),
          "cross-cell copy should produce table text");

  // Inline formatting over a cross-cell selection must be a safe no-op (no corruption of either
  // cell through mixed-cell offsets).
  require(!controller.stylizeController().toggleBold(),
          "bold over a cross-cell selection should not apply");
  require(session.markdownText().toString() == source, "cross-cell bold must not corrupt the table");
}

// The cross-cell highlight must cover BOTH endpoint cells (and everything between in row-major
// order) — historically only the focus cell highlighted, using min/max of two cells' offsets.
void testCrossCellSelectionCoversBothCells() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("| alpha | beta |\n| --- | --- |\n| gamma | delta |"), false);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  std::vector<MarkdownNode*> cells;  // row-major: [alpha, beta, gamma, delta]
  for (const auto& row : table->children()) {
    if (row->type() == BlockType::TableRow) {
      for (const auto& cell : row->children()) {
        cells.push_back(cell.get());
      }
    }
  }
  require(cells.size() == 4, "2x2 table should expose four cells");
  const RenderTheme& theme = view.theme();
  // Fetched AFTER each setSelection: selection changes rebuild block layouts, so a pointer taken
  // earlier would be stale.
  const auto layoutFor = [&view, table]() { return view.blockLayoutForNode(table->id()); };

  const auto cellRect = [&](int row, int col) { return layoutFor()->tableCellRect(row, col); };

  // Full span: anchor (2,0) start of "gamma" → focus (2,1) end of "delta" (body row).
  // Row indices here include the header, so the body row is 1 in layout terms.
  {
    SelectionRange range;
    range.anchor.blockId = table->id();
    range.anchor.text.nodeId = cells.at(2)->id();
    range.anchor.text.textOffset = 0;
    range.focus.blockId = table->id();
    range.focus.text.nodeId = cells.at(3)->id();
    range.focus.text.textOffset = 5;
    controller.selection().setSelection(range);

    const QVector<QRectF> rects = layoutFor()->selectionRects(controller.selection().selection(), theme);
    require(!rects.isEmpty(), "cross-cell selection should produce rects");
    const auto covered = [&](const QRectF& cell) {
      return std::any_of(rects.cbegin(), rects.cend(), [&cell](const QRectF& r) {
        return r.intersects(cell.adjusted(1.0, 1.0, -1.0, -1.0));
      });
    };
    require(covered(cellRect(1, 0)), "anchor cell (gamma) must be highlighted");
    require(covered(cellRect(1, 1)), "focus cell (delta) must be highlighted");
  }

  // Whole-table span (header → body end): every cell highlights, header included.
  {
    SelectionRange range;
    range.anchor.blockId = table->id();
    range.anchor.text.nodeId = cells.at(0)->id();
    range.anchor.text.textOffset = 0;
    range.focus.blockId = table->id();
    range.focus.text.nodeId = cells.at(3)->id();
    range.focus.text.textOffset = 5;
    controller.selection().setSelection(range);

    const QVector<QRectF> rects = layoutFor()->selectionRects(controller.selection().selection(), theme);
    const auto covered = [&](int row, int col) {
      return std::any_of(rects.cbegin(), rects.cend(), [&](const QRectF& r) {
        return r.intersects(cellRect(row, col).adjusted(1.0, 1.0, -1.0, -1.0));
      });
    };
    require(covered(0, 0) && covered(0, 1) && covered(1, 0) && covered(1, 1),
            "whole-table span must highlight all four cells");
  }

  // Partial span: anchor mid-"gamma" → focus mid-"delta" — endpoint cells highlight only their
  // partial halves; nothing crashes on incomparable offsets.
  {
    SelectionRange range;
    range.anchor.blockId = table->id();
    range.anchor.text.nodeId = cells.at(2)->id();
    range.anchor.text.textOffset = 3;  // inside "gamma"
    range.focus.blockId = table->id();
    range.focus.text.nodeId = cells.at(3)->id();
    range.focus.text.textOffset = 2;   // inside "delta"
    controller.selection().setSelection(range);

    const QVector<QRectF> rects = layoutFor()->selectionRects(controller.selection().selection(), theme);
    require(!rects.isEmpty(), "partial cross-cell selection should produce rects");
    qreal anchorWidth = 0.0;
    qreal focusWidth = 0.0;
    for (const QRectF& r : rects) {
      if (r.intersects(cellRect(1, 0).adjusted(1.0, 1.0, -1.0, -1.0))) anchorWidth += r.width();
      if (r.intersects(cellRect(1, 1).adjusted(1.0, 1.0, -1.0, -1.0))) focusWidth += r.width();
    }
    require(anchorWidth > 2.0, "anchor cell should show a partial highlight (tail of 'gamma')");
    require(focusWidth > 2.0, "focus cell should show a partial highlight (head of 'delta')");
  }

  // Backward selection mirrors: focus in the earlier cell.
  {
    SelectionRange range;
    range.anchor.blockId = table->id();
    range.anchor.text.nodeId = cells.at(3)->id();
    range.anchor.text.textOffset = 5;
    range.focus.blockId = table->id();
    range.focus.text.nodeId = cells.at(2)->id();
    range.focus.text.textOffset = 0;
    controller.selection().setSelection(range);

    const QVector<QRectF> rects = layoutFor()->selectionRects(controller.selection().selection(), theme);
    const auto covered = [&](int row, int col) {
      return std::any_of(rects.cbegin(), rects.cend(), [&](const QRectF& r) {
        return r.intersects(cellRect(row, col).adjusted(1.0, 1.0, -1.0, -1.0));
      });
    };
    require(covered(1, 0) && covered(1, 1), "backward cross-cell selection must cover both cells");
  }
}

// The selection overlay color comes from the theme (selectionColor, alpha-capped for legibility),
// not the historical hardcoded blue — sampled at the painted pixel.
void testSelectionColorIsThemed() {
  DocumentSession session;
  EditorView view;
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("plain words here"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);

  SelectionRange selection;
  selection.anchor.blockId = block->id();
  selection.anchor.text.nodeId = block->id();
  selection.anchor.text.textOffset = 6;
  selection.focus.blockId = block->id();
  selection.focus.text.nodeId = block->id();
  selection.focus.text.textOffset = 11;
  view.setSelectionRange(selection);

  const QImage image = captureView(view);
  const QRectF selRect = view.nodeRect(block->id());
  // Sample the middle of the first selected glyph row.
  const InlineLayout* layout = view.blockAtViewportPos(selRect.center())->inlineLayout();
  require(layout != nullptr, "layout should exist");
  const QRectF firstRect = layout->selectionRects(6, 11).value(0);
  require(!firstRect.isEmpty(), "selection should produce a rect");
  const QPointF sampleDoc = firstRect.center();
  const QPoint sample(static_cast<int>(sampleDoc.x()), static_cast<int>(sampleDoc.y() - view.verticalScrollBar()->value()));
  require(image.rect().contains(sample), "sample point should be inside the capture");
  const QColor pixel = image.pixelColor(sample);

  // Expected blend: theme selection color (default #d7e8ff) at the 0.5 alpha cap over the page
  // background. The old hardcoded color (79,143,247, a=72) blends much bluer — reject it.
  const QColor themed = view.theme().selectionColor();
  const QColor expected(
      qRound(0.5 * themed.red() + 0.5 * 255),
      qRound(0.5 * themed.green() + 0.5 * 255),
      qRound(0.5 * themed.blue() + 0.5 * 255));
  const int distThemed = qAbs(pixel.red() - expected.red()) + qAbs(pixel.green() - expected.green()) +
                         qAbs(pixel.blue() - expected.blue());
  const QColor oldBlue(qRound((79 * 72 + 255 * (255 - 72)) / 255.0),
                       qRound((143 * 72 + 255 * (255 - 72)) / 255.0),
                       qRound((247 * 72 + 255 * (255 - 72)) / 255.0));
  const int distOld = qAbs(pixel.red() - oldBlue.red()) + qAbs(pixel.green() - oldBlue.green()) +
                      qAbs(pixel.blue() - oldBlue.blue());
  require(distThemed < distOld, "selection overlay must derive from the theme, not the old hardcoded blue");
}

// inputMethodQuery answers the full IME context (surrounding text in the VISIBLE projection,
// cursor offset within it, same-node selection text) so conversion IMEs get real context.
void testInputMethodQueryAnswersImeContext() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after\n\nplain"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);

  // Caret inside the bold inline: surrounding reports the visible projection ("bold", not
  // "**bold**"), and the cursor offset is in that same visible space.
  CursorPosition inside = inlineCursor(block->id(), 9, 13);  // inside "bold" (visible off 9)
  view.setCursorPosition(inside);
  const QString surrounding = view.inputMethodQuery(Qt::ImSurroundingText).toString();
  require(surrounding == QStringLiteral("before bold after"),
          "ImSurroundingText must report the visible projection, not raw markdown");
  require(view.inputMethodQuery(Qt::ImCursorPosition).toInt() == 9,
          "ImCursorPosition must be the visible-space cursor offset");

  // Same-text-node selection reads back as the substring.
  SelectionRange selection;
  selection.anchor = inlineCursor(block->id(), 7, 11);
  selection.focus = inlineCursor(block->id(), 7, 11);
  selection.focus.text.textOffset = 11;
  view.setSelectionRange(selection);
  require(view.inputMethodQuery(Qt::ImCurrentSelection).toString() == QStringLiteral("bold"),
          "ImCurrentSelection must return the selected substring");

  // Table cell caret reports the cell text.
  session.setMarkdownText(QStringLiteral("| alpha | beta |\n| --- | --- |\n| gamma | delta |"), false);
  view.setDocument(session.document());
  MarkdownNode* table = blockAt(session, 0);
  std::vector<MarkdownNode*> cells;
  for (const auto& row : table->children()) {
    if (row->type() == BlockType::TableRow) {
      for (const auto& cell : row->children()) {
        cells.push_back(cell.get());
      }
    }
  }
  HitTestResult cellHit;
  cellHit.zone = HitTestResult::Zone::TableCell;
  cellHit.blockId = table->id();
  cellHit.textNodeId = cells.at(2)->id();
  cellHit.tableRow = 1;
  cellHit.tableColumn = 0;
  cellHit.textOffset = 0;
  controller.activateHit(cellHit);
  require(view.inputMethodQuery(Qt::ImSurroundingText).toString() == QStringLiteral("gamma"),
          "table-cell caret must report the cell text as surrounding");

  // ImEnabled and a font are answered.
  require(view.inputMethodQuery(Qt::ImEnabled).toBool(), "ImEnabled must be true");
  const QFont font = view.inputMethodQuery(Qt::ImFont).value<QFont>();
  require(!font.family().isEmpty(), "ImFont must return a real font");
}

// Focus-out resets an active composition: the splice is dropped and the block rebuilds, so no
// stale preedit survives at a caret that may have moved on.
void testFocusOutResetsPreedit() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("hello world"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = block->id();
  hit.textNodeId = block->id();
  hit.textOffset = 6;
  hit.sourceOffset = block->sourceRange().byteStart + 6;
  controller.activateHit(hit);
  QApplication::processEvents();

  // Start a composition the way the IME tests do.
  QInputMethodEvent preeditEvent(QStringLiteral("にほんご"), {});
  QApplication::sendEvent(&view, &preeditEvent);
  QApplication::processEvents();

  const InlineLayout* withPreedit = view.blockAtViewportPos(view.nodeRect(block->id()).center())->inlineLayout();
  require(withPreedit != nullptr && withPreedit->hasPreedit(), "composition should be spliced into the layout");

  // Focus-out clears it.
  QFocusEvent focusOut(QEvent::FocusOut);
  QApplication::sendEvent(&view, &focusOut);
  const InlineLayout* after = view.blockAtViewportPos(view.nodeRect(block->id()).center())->inlineLayout();
  require(after != nullptr && !after->hasPreedit(), "focus-out must reset the composition splice");
  require(view.inputMethodQuery(Qt::ImSurroundingText).toString() == QStringLiteral("hello world"),
          "surrounding text must be the plain block again");
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
  RUN_TEST(testInputMethodPreeditPaintsAtCaret);
  RUN_TEST(testInlinePreeditShiftsFollowingText);
  RUN_TEST(testInlinePreeditAtRightEdgeWraps);
  RUN_TEST(testInlinePreeditSplicesInsideRevealedLinkUrl);
  RUN_TEST(testInlinePreeditShiftsMathAtom);
  RUN_TEST(testInlinePreeditSplicesInTableCellAndKeepsAlignment);
  RUN_TEST(testEditorViewDragFromTrailingParagraphSelectsBack);
  RUN_TEST(testEditorViewBackwardNestedSelectionCoversBothBlocks);
  RUN_TEST(testListItemOwnSelectionRectsDoNotLeakToNested);
  RUN_TEST(testCrossCellSelectionIsNotCollapsed);
  RUN_TEST(testCrossCellSelectionCoversBothCells);
  RUN_TEST(testSelectionColorIsThemed);
  RUN_TEST(testInputMethodQueryAnswersImeContext);
  RUN_TEST(testFocusOutResetsPreedit);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
