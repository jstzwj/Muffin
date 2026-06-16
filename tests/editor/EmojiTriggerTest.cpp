#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/EmojiCompleter.h"
#include "editor/EmojiProvider.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QSettings>

using namespace muffin;

namespace {

// A tiny in-memory provider so the trigger/popup/accept logic is exercised without the bundled
// 1900-entry dataset (which is only linked into the app executable, not the test).
class FakeEmojiProvider : public EmojiProvider {
public:
  QVector<EmojiEntry> matches(const QString& prefix, int max) const override {
    static const EmojiEntry all[] = {
        {QStringLiteral("smile"), QStringLiteral("😄")},
        {QStringLiteral("star"), QStringLiteral("⭐")},
        {QStringLiteral("+1"), QStringLiteral("👍")},
    };
    QVector<EmojiEntry> hits;
    for (const EmojiEntry& e : all) {
      if (e.shortcode.contains(prefix, Qt::CaseInsensitive)) {
        hits.append(e);
      }
    }
    if (hits.size() > max) {
      hits.resize(max);
    }
    return hits;
  }
};

struct Harness {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  FakeEmojiProvider provider;
  Harness() {
    wireInput(input, session, selection, undoStack, brushQueue, &view);
    input.setEmojiProvider(&provider);
    view.resize(640, 360);
  }
};

// Type `:smi`, assert the popup opens, then accept via Tab and assert the shortcode is replaced.
void typeShortcode(Harness& h, const QString& doc) {
  h.session.setMarkdownText(doc, false);
  const qsizetype end = h.session.markdownText().size();
  setSourceCursor(h.selection, blockAt(h.session, 0), end, end);
  for (QChar c : QStringLiteral(":smi")) {
    h.input.insertText(QString(c));
  }
}

void testEmojiPopupOpensAndAccepts() {
  SettingsOverride enabled("editor/emojiAutocomplete", true);
  Harness h;
  typeShortcode(h, QStringLiteral("Hello"));
  require(h.input.emojiCompleter() != nullptr, "emoji completer should be created after typing a trigger");
  require(h.input.emojiCompleter()->isVisible(), "popup should be visible after typing ':smi'");
  pressKey(h.input, &h.view, Qt::Key_Tab);  // accept the (single) candidate
  require(h.session.markdownText() == QStringLiteral("Hello😄"), "Tab should replace ':smi' with the emoji");
}

void testEmojiAcceptOnEnter() {
  SettingsOverride enabled("editor/emojiAutocomplete", true);
  Harness h;
  typeShortcode(h, QStringLiteral("word "));
  require(h.input.emojiCompleter()->isVisible(), "popup should be visible mid-word");
  pressKey(h.input, &h.view, Qt::Key_Return);
  require(h.session.markdownText() == QStringLiteral("word 😄"), "Enter should accept the candidate");
}

void testEmojiEscapeCancels() {
  SettingsOverride enabled("editor/emojiAutocomplete", true);
  Harness h;
  typeShortcode(h, QStringLiteral("x"));
  require(h.input.emojiCompleter()->isVisible(), "popup should be visible");
  pressKey(h.input, &h.view, Qt::Key_Escape);
  require(!h.input.emojiCompleter()->isVisible(), "Escape should hide the popup");
  require(h.session.markdownText() == QStringLiteral("x:smi"), "Escape should leave the shortcode text intact");
}

void testEmojiHidesOnNonShortcodeChar() {
  SettingsOverride enabled("editor/emojiAutocomplete", true);
  Harness h;
  typeShortcode(h, QStringLiteral("x"));
  require(h.input.emojiCompleter()->isVisible(), "popup should be visible");
  h.input.insertText(QStringLiteral(" "));  // a space breaks the shortcode
  require(!h.input.emojiCompleter()->isVisible(), "a non-shortcode character should hide the popup");
}

void testEmojiSettingDisabled() {
  SettingsOverride enabled("editor/emojiAutocomplete", false);
  Harness h;
  typeShortcode(h, QStringLiteral("Hello"));
  require(h.input.emojiCompleter() == nullptr || !h.input.emojiCompleter()->isVisible(),
          "popup must not open when the emojiAutocomplete preference is off");
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("EmojiTriggerTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testEmojiPopupOpensAndAccepts);
  RUN_TEST(testEmojiAcceptOnEnter);
  RUN_TEST(testEmojiEscapeCancels);
  RUN_TEST(testEmojiHidesOnNonShortcodeChar);
  RUN_TEST(testEmojiSettingDisabled);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
