#include "commands/ParagraphController.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"

#include "ParagraphTestUtils.h"

#include <QApplication>
#include <QSettings>

using namespace muffin;

// markdown/* insertion defaults driven through ParagraphController: heading style (setext),
// unordered-list marker, and code-fence default language. Each maps a combo INDEX to an emitted
// value (mirroring editor/indentSize).

void testHeadingSetext() {
  SettingsOverride style("markdown/headingStyle", 1);  // 1 = setext (===/---)
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Title"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.setHeadingLevel(1), "setext H1 should succeed");
  require(session.markdownText().toString() == QStringLiteral("Title\n====="), "setext H1 should be content + === underline");
  require(blockAt(session, 0)->type() == BlockType::Heading, "setext should still parse as a heading");
  require(blockAt(session, 0)->headingLevel() == 1, "setext H1 level mismatch");
}

void testUnorderedListMarker() {
  SettingsOverride marker("markdown/unorderedList", 1);  // 1 = '*'
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("item"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.convertToUnorderedList(), "convert to unordered list should succeed");
  require(session.markdownText().toString() == QStringLiteral("* item"), "unordered list should use the '*' marker");
}

void testDefaultCodeLang() {
  SettingsOverride lang("markdown/defaultCodeLang", 1);  // 1 = cpp
  // markdown/autoCodeLang defaults to 0 (emit when inserting via markdown) -> fence carries the lang.
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QString(), false);
  require(paragraph.insertCodeBlock(), "insert code block should succeed");
  require(session.markdownText().toString() == QStringLiteral("```cpp\n\n```"), "code fence should carry the default language");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("ParagraphMarkdownDefaultsTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testHeadingSetext);
  RUN_TEST(testUnorderedListMarker);
  RUN_TEST(testDefaultCodeLang);
#undef RUN_TEST
  return 0;
}
