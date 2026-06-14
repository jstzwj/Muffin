#include "app/SourceEditorBackend.h"
#include "editor/SourceEditorWidget.h"

#include "../TestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QTextCursor>

#include <iostream>

using namespace muffin;

namespace {

struct Harness {
  SourceEditorWidget editor;
  SourceEditorBackend backend;
  Harness() : backend(&editor) {
    editor.resize(720, 460);
  }
  void load(const QString& text) {
    editor.setText(text);
  }
  // Place the caret at a flat document position.
  void placeAt(int position) {
    QTextCursor c = editor.editor()->textCursor();
    c.setPosition(position);
    editor.editor()->setTextCursor(c);
  }
  // Select the closed interval [start, end] (end exclusive).
  void select(int start, int end) {
    QTextCursor c = editor.editor()->textCursor();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    editor.editor()->setTextCursor(c);
  }
  QString text() const {
    return editor.text();
  }
};

}  // namespace

void testSourceDeleteForwardChar() {
  Harness h;
  h.load(QStringLiteral("abc"));
  h.placeAt(0);
  h.backend.deleteRange(DeleteTarget::Forward);
  require(h.text() == QStringLiteral("bc"), "forward delete should remove one char");
}

void testSourceDeleteForwardSelection() {
  Harness h;
  h.load(QStringLiteral("abc"));
  h.select(0, 2);
  h.backend.deleteRange(DeleteTarget::Forward);
  require(h.text() == QStringLiteral("c"), "forward delete should remove the selection");
}

void testSourceDeleteBackwardChar() {
  Harness h;
  h.load(QStringLiteral("abc"));
  h.placeAt(2);
  h.backend.deleteRange(DeleteTarget::Backward);
  require(h.text() == QStringLiteral("ac"), "backward delete should remove the previous char");
}

void testSourceDeleteWord() {
  Harness h;
  h.load(QStringLiteral("hello world"));
  h.placeAt(7);  // inside "world"
  h.backend.deleteRange(DeleteTarget::Word);
  require(h.text() == QStringLiteral("hello "), "delete word should remove \"world\"");
}

// Source mode has no markdown-aware format notion; FormatSpan behaves as Word.
void testSourceDeleteFormatSpan() {
  Harness h;
  h.load(QStringLiteral("hello world"));
  h.placeAt(7);
  h.backend.deleteRange(DeleteTarget::FormatSpan);
  require(h.text() == QStringLiteral("hello "), "delete format span should match word in source mode");
}

// Line clears the current line's text but leaves the (empty) line in place.
void testSourceDeleteLine() {
  Harness h;
  h.load(QStringLiteral("line1\nline2\nline3"));
  h.placeAt(6);  // start of "line2"
  h.backend.deleteRange(DeleteTarget::Line);
  require(h.text() == QStringLiteral("line1\n\nline3"), "delete line should clear the line text, leaving an empty line");
}

// Block removes a middle line entirely (line text + its newline), joining neighbours.
void testSourceDeleteBlockMiddle() {
  Harness h;
  h.load(QStringLiteral("line1\nline2\nline3"));
  h.placeAt(6);  // inside "line2"
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.text() == QStringLiteral("line1\nline3"), "delete block on a middle line should remove the line and join");
}

// Block on the last line removes the line text (previous newline kept).
void testSourceDeleteBlockLast() {
  Harness h;
  h.load(QStringLiteral("line1\nline2"));
  h.placeAt(6);  // inside "line2"
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.text() == QStringLiteral("line1\n"), "delete block on the last line should leave the previous line");
}

// Block on the only line empties the document.
void testSourceDeleteBlockOnly() {
  Harness h;
  h.load(QStringLiteral("only"));
  h.placeAt(1);
  h.backend.deleteRange(DeleteTarget::Block);
  require(h.text().isEmpty(), "delete block on the only line should empty the document");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testSourceDeleteForwardChar();
  testSourceDeleteForwardSelection();
  testSourceDeleteBackwardChar();
  testSourceDeleteWord();
  testSourceDeleteFormatSpan();
  testSourceDeleteLine();
  testSourceDeleteBlockMiddle();
  testSourceDeleteBlockLast();
  testSourceDeleteBlockOnly();
  QApplication::clipboard()->clear();
  return 0;
}
