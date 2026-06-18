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

// markdown/* smart punctuation, driven through InputController::insertText (the prose typing path).
// Quotes become curly and "--"/"---" collapse to en/em dashes, gated by markdown/convertOnInput.

void testSmartQuotes() {
  SettingsOverride mode("markdown/convertOnInput", 1);   // convert single chars while typing
  SettingsOverride quotes("markdown/smartQuotes", true);
  SettingsOverride doubleStyle("markdown/doubleQuoteStyle", 0);  // 0 = curly
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("word"), false);
  setCursor(selection, blockAt(session, 0), 4);  // end of "word"
  require(input.insertText(QStringLiteral("\"")), "typing a quote should succeed");
  // Preceding char is a letter -> closing curly quote (U+201D), and it must NOT auto-pair.
  require(session.markdownText() == QStringLiteral("word") + QStringLiteral("\xe2\x80\x9d"),
          "smart quote after a word should be a closing curly quote");
}

void testSmartDashes() {
  SettingsOverride mode("markdown/convertOnInput", 1);
  SettingsOverride dashes("markdown/smartDashes", true);
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("a-"), false);
  setCursor(selection, blockAt(session, 0), 2);  // after the single '-'
  require(input.insertText(QStringLiteral("-")), "typing a dash should succeed");
  // "--" -> en-dash (U+2013): the existing '-' + typed '-' collapse.
  require(session.markdownText() == QStringLiteral("a") + QStringLiteral("\xe2\x80\x93"),
          "'--' should collapse to an en-dash");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("InputMarkdownDefaultsTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSmartQuotes);
  RUN_TEST(testSmartDashes);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
