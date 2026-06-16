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

// editor/matchBrackets (default on) and editor/matchMarkdown (default off) drive auto-pairing,
// wrap-selection and skip-over in InputController::insertText. These cover the three behaviours
// (empty pair + caret between, wrap a selection, step over an existing closer) plus the apostrophe
// contraction guard and the settings gate.

void testBracketAutoPair() {
  SettingsOverride brackets("editor/matchBrackets", true);
  SettingsOverride markdown("editor/matchMarkdown", false);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(input.insertText(QStringLiteral("(")), "bracket opener should auto-pair");
  require(session.markdownText() == QStringLiteral("al()pha"), "bracket pair should be inserted");
  require(selection.cursorPosition().text.textOffset == 3, "caret should land between the bracket pair");

  // Skip-over: typing the closer while the caret is already before it steps over, no duplicate.
  session.setMarkdownText(QStringLiteral("()"), false);
  setCursor(selection, blockAt(session, 0), 1);
  require(input.insertText(QStringLiteral(")")), "closer before a closer should skip over");
  require(session.markdownText() == QStringLiteral("()"), "skip-over should not insert a duplicate closer");
  require(selection.cursorPosition().text.textOffset == 2, "skip-over should advance the caret past the closer");
}

void testMarkdownAutoPairAndWrap() {
  SettingsOverride brackets("editor/matchBrackets", true);
  SettingsOverride markdown("editor/matchMarkdown", true);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // With markdown pairing on, '*' pairs to '**' with the caret between.
  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("*")), "markdown opener should auto-pair");
  require(session.markdownText() == QStringLiteral("**alpha"), "markdown pair should be inserted");
  require(selection.cursorPosition().text.textOffset == 1, "caret should land between the markdown pair");

  // Wrapping a selection types the opener around the selected text.
  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 3);  // "lp"
  require(input.insertText(QStringLiteral("*")), "wrap should consume the selection");
  require(session.markdownText() == QStringLiteral("a*lp*ha"), "wrap should surround the selection with the pair");
}

void testMarkdownPairingOffInsertsLone() {
  SettingsOverride brackets("editor/matchBrackets", true);
  SettingsOverride markdown("editor/matchMarkdown", false);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("*")), "lone '*' should insert when markdown pairing is off");
  require(session.markdownText() == QStringLiteral("*alpha"), "no markdown pair should be inserted when the setting is off");
}

void testApostropheContractionGuard() {
  SettingsOverride brackets("editor/matchBrackets", true);
  SettingsOverride markdown("editor/matchMarkdown", false);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // A single quote between two letters is a contraction, not an opener.
  session.setMarkdownText(QStringLiteral("dont"), false);
  setCursor(selection, blockAt(session, 0), 3);  // between "don" and "t"
  require(input.insertText(QStringLiteral("'")), "apostrophe between letters should insert normally");
  require(session.markdownText() == QStringLiteral("don't"), "contraction should not become a paired quote");

  // At the start (not between letters) the quote still pairs.
  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("'")), "quote at the start should pair");
  require(session.markdownText() == QStringLiteral("''alpha"), "leading quote should produce a pair");
}

void testBracketPairingDisabled() {
  SettingsOverride brackets("editor/matchBrackets", false);
  SettingsOverride markdown("editor/matchMarkdown", false);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("(")), "bracket should insert normally when pairing disabled");
  require(session.markdownText() == QStringLiteral("(alpha"), "no pair should be inserted when both settings are off");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("InputAutoPairTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testBracketAutoPair);
  RUN_TEST(testMarkdownAutoPairAndWrap);
  RUN_TEST(testMarkdownPairingOffInsertsLone);
  RUN_TEST(testApostropheContractionGuard);
  RUN_TEST(testBracketPairingDisabled);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
