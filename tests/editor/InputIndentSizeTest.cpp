#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QSettings>

using namespace muffin;

// editor/indentSize maps a combo index {0,1,2} to space counts {2,4,8}. These tests pin the
// mapping end-to-end through the list indent/outdent/exit builders, which previously hardcoded 2.

void testIndentUnitDefaultIsTwoSpaces() {
  SettingsOverride settings("editor/indentSize", 0);  // index 0 -> 2 spaces
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "indent at unit 2 should succeed");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "indent at unit 2 should add 2 spaces");
}

void testIndentUnitFourSpaces() {
  SettingsOverride settings("editor/indentSize", 1);  // index 1 -> 4 spaces
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "indent at unit 4 should succeed");
  require(session.markdownText() == QStringLiteral("- alpha\n    - beta"), "indent at unit 4 should add 4 spaces");

  MarkdownNode* nestedList = maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  require(nestedList != nullptr, "4-space indent should nest the item");
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.outdentListItem(), "outdent at unit 4 should succeed");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "outdent at unit 4 should remove 4 spaces");

  // Graceful min-rule: with unit 4, outdenting a stray 2-indent removes only 2 (never more than present).
  session.setMarkdownText(QStringLiteral("- alpha\n  - beta"), false);
  nestedList = maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  require(nestedList != nullptr, "stray 2-indent item should parse as nested");
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.outdentListItem(), "outdent should succeed on stray 2-indent at unit 4");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "outdent at unit 4 on a 2-indent should remove min(4,2)=2 spaces");
}

void testIndentUnitEightSpaces() {
  SettingsOverride settings("editor/indentSize", 2);  // index 2 -> 8 spaces
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Assert the exact source text (8 leading spaces). The command inserts indentUnit() spaces at the
  // line start; the AST may re-shape at such depth, but markdownText() reflects the command verbatim.
  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "indent at unit 8 should succeed");
  require(session.markdownText() == QStringLiteral("- alpha\n        - beta"), "indent at unit 8 should add 8 spaces");
}

void testExitEmptyItemRemovesFullUnit() {
  SettingsOverride settings("editor/indentSize", 1);  // 4 spaces
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // A 4-space nested empty item: Enter outdents one full unit (4 spaces) back to top level.
  session.setMarkdownText(QStringLiteral("- alpha\n    - "), false);
  MarkdownNode* nestedList = maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  require(nestedList != nullptr, "4-space nested empty item should parse as a nested list");
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.insertParagraphBreak(), "enter on nested empty item at unit 4 should outdent");
  require(session.markdownText() == QStringLiteral("- alpha\n- "), "exit-on-empty at unit 4 should remove 4 spaces");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  // Throwaway org/app + INI format so the test's QSettings never touches the real Muffin store
  // (e.g. the Windows registry) and round-trips reliably within the process. Mirrors SpellCheckerTest.
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("InputIndentSizeTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testIndentUnitDefaultIsTwoSpaces);
  RUN_TEST(testIndentUnitFourSpaces);
  RUN_TEST(testIndentUnitEightSpaces);
  RUN_TEST(testExitEmptyItemRemovesFullUnit);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
