#include "EditorViewTestUtils.h"

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
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "view hit inline insert mismatch");
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
  require(selectedLayout->cursorRectForSourceOffset(9).left() != collapsedCursor.left(), "selection touching inline should expand marker layout");
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
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
