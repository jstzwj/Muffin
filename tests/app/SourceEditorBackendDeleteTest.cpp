#include "app/SourceEditorBackend.h"
#include "document/DocumentSession.h"
#include "editor/SourceEditorWidget.h"
#include "editor/VirtualSourceEdit.h"

#include "../TestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QScrollBar>
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
    editor.setCursorPosition(position);
  }
  // Select the closed interval [start, end] (end exclusive).
  void select(int start, int end) {
    editor.setSelection(start, end);
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

void testSessionBackedSourceEditsAndUndo() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\nbeta"), false);
  SourceEditorWidget editor;
  editor.bindSession(&session);
  SourceEditorBackend backend(&editor);

  editor.setCursorPosition(QStringLiteral("alpha").size());
  editor.insertText(QStringLiteral("!"));
  require(session.markdownText().toString() == QStringLiteral("alpha!\nbeta"),
          "source edit should update the session PieceTable directly");
  require(backend.canUndo(), "session-backed source edit should be undoable");
  backend.undo();
  require(session.markdownText().toString() == QStringLiteral("alpha\nbeta"),
          "source undo should apply the inverse PieceTable delta");
  backend.redo();
  require(session.markdownText().toString() == QStringLiteral("alpha!\nbeta"),
          "source redo should reapply the PieceTable delta");

  editor.setSelection(0, 5);
  editor.replaceSelection(QStringLiteral("A"));
  require(session.markdownText().toString() == QStringLiteral("A!\nbeta"),
          "source selection replacement should update the session");
}

void testReadOnlySourceRejectsMutationWithoutDamagingUndo() {
  Harness h;
  h.load(QStringLiteral("alpha"));
  h.placeAt(5);
  h.editor.insertText(QStringLiteral("!"));
  require(h.backend.canUndo(), "fixture edit should be undoable");

  h.editor.setReadOnly(true);
  h.editor.insertText(QStringLiteral(" ignored"));
  h.backend.undo();
  require(h.text() == QStringLiteral("alpha!"),
          "read-only input and undo must leave text and undo history unchanged");

  h.editor.setReadOnly(false);
  h.backend.undo();
  require(h.text() == QStringLiteral("alpha"),
          "undo history should remain usable after read-only mode ends");
}

void testVirtualSourceViewportAffordances() {
  SourceEditorWidget editor;
  editor.resize(720, 300);
  editor.setText(QStringLiteral("source line\n").repeated(1000));
  editor.show();
  QApplication::processEvents();

  VirtualSourceEdit* view = editor.findChild<VirtualSourceEdit*>();
  require(view != nullptr, "source widget should own a virtual source view");
  require(view->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
          "large source documents should expose a vertical scrollbar");
  require(view->verticalScrollBar()->maximum() > 0 && view->verticalScrollBar()->isVisible(),
          "vertical scrollbar should be visible for overflowing source text");
  require(view->viewport()->cursor().shape() == Qt::IBeamCursor,
          "source text viewport should use an I-beam cursor");

  const QImage viewportImage = view->viewport()->grab().toImage();
  bool foundDarkBodyPixel = false;
  for (int y = 0; y < qMin(50, viewportImage.height()) && !foundDarkBodyPixel; ++y) {
    for (int x = 72; x < qMin(220, viewportImage.width()); ++x) {
      if (qGray(viewportImage.pixel(x, y)) < 130) {
        foundDarkBodyPixel = true;
        break;
      }
    }
  }
  require(foundDarkBodyPixel,
          "plain source text should use the theme body color, not the pale line-number color");

  editor.setWordWrapEnabled(false);
  require(view->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
          "unwrapped source text should expose a horizontal scrollbar when needed");
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
  testSessionBackedSourceEditsAndUndo();
  testReadOnlySourceRejectsMutationWithoutDamagingUndo();
  testVirtualSourceViewportAffordances();
  QApplication::clipboard()->clear();
  return 0;
}
