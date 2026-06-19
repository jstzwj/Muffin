#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>
#include <QSettings>

using namespace muffin;

// Inside a code fence, Shift+Tab dedents by one codeIndent unit (default 4 spaces): every selected
// line when there is a selection, otherwise just the caret's line. min-rule: never strip more
// spaces than a line has; an unindented line is a no-op (no tab is inserted).

namespace {
// Builds a one-fence document, enters code-fence edit mode, and selects [anchorOff, focusOff] of
// the fence literal (anchor/focus on the same fence). Returns the fence node.
MarkdownNode* setupFenceSelection(EditorController& controller, DocumentSession& session, EditorView& view,
                                  const QString& code, qsizetype anchorOff, qsizetype focusOff) {
  session.setMarkdownText(QStringLiteral("```cpp\n%1\n```").arg(code), false);
  view.setDocument(session.document());
  MarkdownNode* fence = blockAt(session, 0);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = fence->id();
  hit.textNodeId = fence->id();
  hit.textOffset = 0;
  controller.activateHit(hit);
  require(controller.codeFenceController().enterEditMode(), "code fence edit mode should activate");

  SelectionRange range;
  range.anchor.blockId = fence->id();
  range.anchor.text.nodeId = fence->id();
  range.anchor.text.textOffset = anchorOff;
  range.focus.blockId = fence->id();
  range.focus.text.nodeId = fence->id();
  range.focus.text.textOffset = focusOff;
  controller.selection().setSelection(range);
  return fence;
}

void sendBackTab(EditorController& controller, EditorView& view) {
  QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  controller.inputController().eventFilter(&view, &backtab);
}
}  // namespace

void testShiftTabDedentsSelectedLines() {
  SettingsOverride indent("markdown/codeIndent", 1);  // 4 spaces
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString code = QStringLiteral("    line one\n    line two");
  setupFenceSelection(controller, session, view, code, 0, code.size());
  sendBackTab(controller, view);

  require(session.markdownText() == QStringLiteral("```cpp\nline one\nline two\n```"),
          "Shift+Tab should strip one indent unit from every selected line");
}

void testShiftTabMinRuleStripsOnlyPresentSpaces() {
  SettingsOverride indent("markdown/codeIndent", 1);  // 4 spaces
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  // First line has only 2 leading spaces (< unit 4); min-rule strips 2, not 4.
  const QString code = QStringLiteral("  short\n    long");
  setupFenceSelection(controller, session, view, code, 0, code.size());
  sendBackTab(controller, view);

  require(session.markdownText() == QStringLiteral("```cpp\nshort\nlong\n```"),
          "Shift+Tab min-rule should strip no more spaces than a line has");
}

void testShiftTabCollapsedCaretDedentsLine() {
  SettingsOverride indent("markdown/codeIndent", 1);  // 4 spaces
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  // Collapsed caret at the start of the first line: Shift+Tab dedents the caret's line only,
  // leaving the second line untouched.
  const QString code = QStringLiteral("    line one\n    line two");
  MarkdownNode* fence = setupFenceSelection(controller, session, view, code, 0, 0);
  CursorPosition caret;
  caret.blockId = fence->id();
  caret.text.nodeId = fence->id();
  caret.text.textOffset = 0;
  controller.selection().setCursorPosition(caret);
  sendBackTab(controller, view);

  require(session.markdownText() == QStringLiteral("```cpp\nline one\n    line two\n```"),
          "Shift+Tab with a collapsed caret should dedent only the caret's line");
}

void testShiftTabUnindentedLineIsNoop() {
  SettingsOverride indent("markdown/codeIndent", 1);  // 4 spaces
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  // Collapsed caret on a line with no leading spaces: nothing to strip, so Shift+Tab is a no-op
  // and must not insert an indent unit.
  const QString code = QStringLiteral("line one");
  MarkdownNode* fence = setupFenceSelection(controller, session, view, code, 0, 0);
  CursorPosition caret;
  caret.blockId = fence->id();
  caret.text.nodeId = fence->id();
  caret.text.textOffset = 0;
  controller.selection().setCursorPosition(caret);
  sendBackTab(controller, view);

  require(session.markdownText() == QStringLiteral("```cpp\nline one\n```"),
          "Shift+Tab on an unindented line should be a no-op (no tab inserted)");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("InputShiftTabDedentTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testShiftTabDedentsSelectedLines);
  RUN_TEST(testShiftTabMinRuleStripsOnlyPresentSpaces);
  RUN_TEST(testShiftTabCollapsedCaretDedentsLine);
  RUN_TEST(testShiftTabUnindentedLineIsNoop);
#undef RUN_TEST
  return 0;
}
