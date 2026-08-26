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
#include <QLineEdit>

using namespace muffin;

namespace {

// Ctrl+Tab must move focus out of the editor canvas (keyboard trap escape). Plain Tab stays
// editor content.
void testCtrlTabEscapesEditorFocus() {
  QWidget host;
  auto* before = new QLineEdit(&host);
  auto* view = new EditorView(&host);
  auto* after = new QLineEdit(&host);
  host.resize(800, 600);
  host.show();

  DocumentSession session;
  EditorController controller;
  controller.attach(&session, view);
  session.setMarkdownText(QStringLiteral("alpha beta"), false);
  view->setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);

  view->setFocus();
  QApplication::processEvents();
  require(QApplication::focusWidget() == view || QApplication::focusWidget() == view->viewport(),
          "editor should hold focus before ctrl+tab");

  sendKey(view, Qt::Key_Tab, Qt::ControlModifier);
  QApplication::processEvents();
  QWidget* focused = QApplication::focusWidget();
  require(focused != view && focused != view->viewport(),
          "ctrl+tab must move focus out of the editor canvas");

  // Focus moved somewhere within the host (either sibling qualifies; tab order decides which).
  require(focused != nullptr && (focused == before || focused == after ||
          (focused->parentWidget() != nullptr && focused->parentWidget()->isAncestorOf(&host))),
          "focus should land on another widget in the window");

  // Plain Tab stays editor content: focus unchanged, no traversal.
  view->setFocus();
  QApplication::processEvents();
  sendKey(view, Qt::Key_Tab, Qt::NoModifier);
  QApplication::processEvents();
  require(QApplication::focusWidget() == view || QApplication::focusWidget() == view->viewport(),
          "plain tab must keep focus in the editor");
}

// Ctrl+Tab in the source-mode widget escapes too (VirtualSourceEdit shares the policy through
// the same handling contract — verified indirectly: the source editor's focus proxy chain keeps
// the event from being swallowed is a MainWindow-level concern; here we lock the render mode,
// which was the actual trap).
void testFocusReturnsAfterEscape() {
  QWidget host;
  auto* line = new QLineEdit(&host);
  auto* view = new EditorView(&host);
  host.resize(800, 600);
  host.show();

  DocumentSession session;
  EditorController controller;
  controller.attach(&session, view);
  session.setMarkdownText(QStringLiteral("alpha"), false);
  view->setDocument(session.document());

  view->setFocus();
  QApplication::processEvents();
  sendKey(view, Qt::Key_Tab, Qt::ControlModifier);
  QApplication::processEvents();
  QWidget* escaped = QApplication::focusWidget();
  require(escaped != view && escaped != view->viewport(), "focus should have left the editor");

  // And back in by mouse-free means: focus policy still works after an escape round-trip.
  view->setFocus();
  QApplication::processEvents();
  require(QApplication::focusWidget() == view || QApplication::focusWidget() == view->viewport(),
          "focus should return to the editor on setFocus");
  Q_UNUSED(line);
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCtrlTabEscapesEditorFocus);
  RUN_TEST(testFocusReturnsAfterEscape);
#undef RUN_TEST
  qInfo("All focus-escape tests passed.");
  return 0;
}
