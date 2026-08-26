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
#include <QKeyEvent>
#include <QScrollBar>

#include <cstdio>

using namespace muffin;

namespace {

// A document tall enough that its height clearly exceeds the 480px test viewport.
QString tallDocument() {
  QString text;
  for (int i = 0; i < 120; ++i) {
    text += QStringLiteral("paragraph %1 with some longer content to give each block height\n\n").arg(i);
  }
  return text;
}

void testPageDownMovesCaretAndScrolls() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(800, 480);

  session.setMarkdownText(tallDocument(), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);

  const int scrollBefore = view.verticalScrollBar()->value();
  require(pressKey(controller.inputController(), &view, Qt::Key_PageDown), "page down should be handled");
  const NodeId after = controller.selection().cursorPosition().blockId;
  require(after != blockAt(session, 0)->id(), "page down should move the caret off the first block");
  require(view.verticalScrollBar()->value() > scrollBefore, "page down should scroll the viewport");

  // The caret keeps its screen Y: its document position sits inside the post-scroll viewport.
  const QRectF caret = view.effectiveCursorRect();
  const qreal scroll = static_cast<qreal>(view.verticalScrollBar()->value());
  require(caret.center().y() >= scroll && caret.center().y() <= scroll + static_cast<qreal>(view.viewport()->height()),
          "caret should stay on screen after page down");
}

void testPageUpFromLaterBlock() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(800, 480);

  session.setMarkdownText(tallDocument(), false);
  view.setDocument(session.document());
  // Caret near the document end.
  const qsizetype last = 119;
  setCursor(controller.selection(), blockAt(session, last), 0);
  view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());

  const int scrollBefore = view.verticalScrollBar()->value();
  require(pressKey(controller.inputController(), &view, Qt::Key_PageUp), "page up should be handled");
  require(controller.selection().cursorPosition().blockId != blockAt(session, last)->id(),
          "page up should move the caret off the last block");
  require(view.verticalScrollBar()->value() < scrollBefore, "page up should scroll the viewport up");

  const QRectF caret = view.effectiveCursorRect();
  const qreal scroll = static_cast<qreal>(view.verticalScrollBar()->value());
  require(caret.center().y() >= scroll && caret.center().y() <= scroll + static_cast<qreal>(view.viewport()->height()),
          "caret should stay on screen after page up");
}

void testPageDownClampsAtDocumentEnd() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(800, 480);

  session.setMarkdownText(tallDocument(), false);
  view.setDocument(session.document());
  const qsizetype last = 119;
  setCursor(controller.selection(), blockAt(session, last), 0);
  view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());

  require(pressKey(controller.inputController(), &view, Qt::Key_PageDown), "page down at the end is still ours");
  require(view.verticalScrollBar()->value() == view.verticalScrollBar()->maximum(),
          "scroll must clamp at the document end");
  const QRectF caret = view.effectiveCursorRect();
  const qreal scroll = static_cast<qreal>(view.verticalScrollBar()->value());
  require(caret.center().y() <= scroll + static_cast<qreal>(view.viewport()->height()),
          "clamped page down must not strand the caret below the viewport");
}

void testShiftPageDownExtendsSelection() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(800, 480);

  session.setMarkdownText(tallDocument(), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);

  require(pressKey(controller.inputController(), &view, Qt::Key_PageDown, Qt::ShiftModifier),
          "shift+page down should be handled");
  const SelectionRange range = controller.selection().selection();
  require(!range.isCollapsed(), "shift+page down should extend the selection");
  require(range.anchor.blockId == blockAt(session, 0)->id(), "anchor should stay at the origin block");
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testPageDownMovesCaretAndScrolls);
  RUN_TEST(testPageUpFromLaterBlock);
  RUN_TEST(testPageDownClampsAtDocumentEnd);
  RUN_TEST(testShiftPageDownExtendsSelection);
#undef RUN_TEST
  qInfo("All page-navigation tests passed.");
  return 0;
}
