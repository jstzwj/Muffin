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

// editor/alignIndent (default off) makes a split list item's new marker line up under the parent
// item's *content* column instead of the flat historical indent. Off must be byte-identical to the
// pre-change behaviour so existing InputListTabTest assertions never regress.

void testAlignIndentOffMatchesDefault() {
  SettingsOverride settings("editor/alignIndent", false);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split unordered list item with align off");
  require(session.markdownText() == QStringLiteral("- al\n- pha\n- beta"),
          "align off: unordered split should stay flat (byte-identical to historical behaviour)");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item with align off");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n3. beta"),
          "align off: ordered split should stay flat");
}

void testAlignIndentOnAlignsUnderContent() {
  SettingsOverride settings("editor/alignIndent", true);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Unordered: marker width 2 -> continuation column 0+2=2 -> "\n  - ".
  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split unordered list item with align on");
  require(session.markdownText() == QStringLiteral("- al\n  - pha\n- beta"),
          "align on: unordered split should indent the new item under the parent content");

  // Ordered lists stay flat even with align on: nesting a numbered item would collide with the
  // auto-renumber pass (it only renumbers same-column siblings) and a nested ordered list restarts
  // numbering anyway, so aligning it is ill-defined.
  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item with align on");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n3. beta"),
          "align on: ordered split should stay flat (renumber + nesting would collide)");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  // Throwaway org/app + INI format so the test's QSettings never touches the real Muffin store
  // and round-trips reliably within the process. Mirrors SpellCheckerTest.
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("InputAlignIndentTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testAlignIndentOffMatchesDefault);
  RUN_TEST(testAlignIndentOnAlignsUnderContent);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
