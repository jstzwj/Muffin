#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorAccessibility.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/VirtualSourceEdit.h"

#include "EditorTestUtils.h"

#include <QAccessible>
#include <QAccessibleTextInterface>
#include <QApplication>
#include <QPointer>

using namespace muffin;

namespace {

QAccessibleTextInterface* textInterfaceFor(QWidget* widget) {
  QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget);
  if (!iface) {
    return nullptr;
  }
  return static_cast<QAccessibleTextInterface*>(iface->interface_cast(QAccessible::TextInterface));
}

// Prerequisite for the whole offset model: accessible offsets are UTF-16 source offsets. A
// document with a non-BMP emoji pins that emoji occupies two offsets and text(0, n) matches the
// markdown exactly.
void testSourceOffsetsAreUtf16Units() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);

  const QString text = QStringLiteral("a😀b\n\nplain tail");
  session.setMarkdownText(text, false);
  view.setDocument(session.document());

  QAccessibleTextInterface* iface = textInterfaceFor(&view);
  require(iface != nullptr, "editor view should expose a text interface");
  require(iface->characterCount() == text.size(), "character count must be UTF-16 code units");
  require(iface->text(0, text.size()) == text, "accessible text must equal the markdown source");

  // Cursor placed after the emoji (offset 3) must report 3.
  controller.setCursorForSourceOffset(3);
  require(iface->cursorPosition() == 3, "cursor position must be the UTF-16 source offset");
}

void testRenderedAdapterBasics() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("# Title\n\nalpha beta gamma\n\ndelta"), false);
  view.setDocument(session.document());

  QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&view);
  require(iface != nullptr, "editor view should have an accessible interface");
  require(iface->role() == QAccessible::EditableText, "role should be editable text");
  require(iface->state().editable, "state should be editable");
  require(iface->state().multiLine, "state should be multiline");

  QAccessibleTextInterface* text = static_cast<QAccessibleTextInterface*>(iface->interface_cast(QAccessible::TextInterface));
  require(text != nullptr, "text interface should be exposed");

  // Boundary queries: line and word.
  int start = 0;
  int end = 0;
  QString line = text->textAtOffset(3, QAccessible::LineBoundary, &start, &end);
  require(line == QStringLiteral("# Title") && start == 0 && end == 7,
          "line boundary should resolve the containing line");
  // Offset 8 is the empty separator line — an empty piece, which is a valid line.
  const QString emptyLine = text->textAtOffset(QStringLiteral("# Title\n").size(), QAccessible::LineBoundary, &start, &end);
  require(emptyLine.isEmpty() && start == 8 && end == 8, "empty separator line should resolve empty");

  // Word boundary inside "alpha beta gamma": the source starts after "# Title\n\n" (9 units).
  const int alphaStart = QStringLiteral("# Title\n\n").size();
  QString word = text->textAtOffset(alphaStart + 1, QAccessible::WordBoundary, &start, &end);
  require(word == QStringLiteral("alpha"), "word boundary should return the containing word");
  require(start == alphaStart && end == alphaStart + 5, "word offsets must be source offsets");

  // setCursorPosition round-trips through the controller.
  const int gammaStart = QStringLiteral("# Title\n\nalpha beta ").size();
  text->setCursorPosition(gammaStart);
  require(controller.selection().cursorPosition().text.sourceOffset == gammaStart,
          "setCursorPosition must map back to the same source offset");

  // Selection exposure.
  text->addSelection(alphaStart, alphaStart + 5);
  require(text->selectionCount() == 1, "selection should be exposed");
  int selStart = 0;
  int selEnd = 0;
  text->selection(0, &selStart, &selEnd);
  require(selStart == alphaStart && selEnd == alphaStart + 5, "selection offsets must match");
  require(text->text(selStart, selEnd) == QStringLiteral("alpha"), "selected text should read back");

  // Cache invalidation: editing the document changes characterCount.
  const int before = text->characterCount();
  session.setMarkdownText(QStringLiteral("# Title\n\nalpha beta gamma\n\ndelta plus"), false);
  view.setDocument(session.document());
  require(text->characterCount() > before, "text cache must invalidate on revision change");
}

void testSourceModeAdapter() {
  VirtualSourceEdit edit;
  edit.setStandaloneText(QStringLiteral("alpha beta\ngamma"));

  QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&edit);
  require(iface != nullptr, "source editor should have an accessible interface");
  require(iface->role() == QAccessible::EditableText, "source role should be editable text");

  QAccessibleTextInterface* text = static_cast<QAccessibleTextInterface*>(iface->interface_cast(QAccessible::TextInterface));
  require(text != nullptr, "source text interface should be exposed");
  require(text->characterCount() == QStringLiteral("alpha beta\ngamma").size(),
          "source character count must match the text");
  require(text->text(0, 5) == QStringLiteral("alpha"), "source text slice should read back");

  text->setCursorPosition(6);
  require(text->cursorPosition() == 6, "source cursor set/read should round-trip");

  text->addSelection(0, 5);
  int selStart = 0;
  int selEnd = 0;
  require(text->selectionCount() == 1, "source selection should be exposed");
  text->selection(0, &selStart, &selEnd);
  require(selStart == 0 && selEnd == 5, "source selection offsets should match");

  int start = 0;
  int end = 0;
  const QString word = text->textAtOffset(7, QAccessible::WordBoundary, &start, &end);
  require(word == QStringLiteral("beta"), "source word boundary should work");
}

// Detach must clear the controller registry (no dangling access).
void testDetachClearsRegistry() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("alpha"), false);
  view.setDocument(session.document());

  QAccessibleTextInterface* iface = textInterfaceFor(&view);
  require(iface != nullptr && iface->characterCount() == 5, "attached adapter should serve text");

  controller.detach();
  require(a11y::controllerFor(&view) == nullptr, "detach must clear the controller registry");
  // After detach the adapter degrades gracefully instead of dangling.
  require(iface->characterCount() == 0, "detached adapter should report empty text");
  controller.attach(&session, &view);
  require(textInterfaceFor(&view)->characterCount() == 5, "re-attach should serve text again");
}

// Event emission: caret/selection events fire through QAccessible::updateAccessibility when a
// bridge is active. Offscreen usually has no bridge, so the assertions are gated — the wiring
// compiles and runs either way, and environments with an active bridge get the full check.
int g_caretEvents = 0;
int g_selectionEvents = 0;

void countingUpdateHandler(QAccessibleEvent* event) {
  if (!event) {
    return;
  }
  if (event->type() == QAccessible::TextCaretMoved) {
    ++g_caretEvents;
  } else if (event->type() == QAccessible::TextSelectionChanged) {
    ++g_selectionEvents;
  }
}

void testAccessibleEvents() {
  if (!QAccessible::isActive()) {
    qInfo("skip: accessibility bridge inactive in this environment");
    return;
  }
  QAccessible::UpdateHandler previous = QAccessible::installUpdateHandler(&countingUpdateHandler);
  g_caretEvents = 0;
  g_selectionEvents = 0;

  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("alpha beta"), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 0);

  controller.setCursorForSourceOffset(6);
  QApplication::processEvents();
  require(g_caretEvents > 0, "caret move should fire a text caret event");

  textInterfaceFor(&view)->addSelection(0, 5);
  QApplication::processEvents();
  require(g_selectionEvents > 0, "selection change should fire a selection event");

  QAccessible::installUpdateHandler(previous);
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  muffin::installEditorAccessibility();
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSourceOffsetsAreUtf16Units);
  RUN_TEST(testRenderedAdapterBasics);
  RUN_TEST(testSourceModeAdapter);
  RUN_TEST(testDetachClearsRegistry);
  RUN_TEST(testAccessibleEvents);
#undef RUN_TEST
  qInfo("All accessibility adapter tests passed.");
  return 0;
}
