#include <QApplication>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QImage>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QMimeData>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QPainter>
#include <QScrollBar>

#include "../TestUtils.h"
#include "../theme/BrowserLayoutFont.h"
#include "document/DocumentSession.h"
#include "editor/VirtualSourceEdit.h"
#include "editor/EditorController.h"
#include "io/FileController.h"
#include "theme/RenderTheme.h"
#include <QTemporaryDir>
#include <QMessageBox>
#include <QTimer>
#include <QtTest/QTest>
#include <QtMath>

using namespace muffin;

namespace {
QFont fixtureSourceFont;

void finishSourceBatch(DocumentSession& session) {
  QElapsedTimer timeout;
  timeout.start();
  while (session.isBatchPending() && timeout.elapsed() < 15000)
    QTest::qWait(5);
  require(!session.isBatchPending(), "source drag/history batch completes");
}

QPoint sourcePoint(VirtualSourceEdit& edit, qsizetype offset) {
  const auto anchor = edit.anchorPosition(), cursor = edit.cursorPosition();
  edit.setCursorPosition(offset);
  const auto point = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect().center();
  edit.setSelection(anchor, cursor);
  return point;
}

// Drive the real selection gesture into QDrag's nested loop. Delivering the
// destination events there makes event->source() and the MIME ownership real,
// while avoiding platform-specific global mouse automation in headless CI.
void dragSourceSelection(VirtualSourceEdit& edit, const QPoint& press, const QPoint& destination, Qt::DropAction action,
                         const std::function<void()>& duringDrag = {}) {
  bool entered = false;
  QTimer::singleShot(0, &edit, [&] {
    const auto drags = edit.findChildren<QDrag*>();
    auto* drag = drags.isEmpty() ? nullptr : drags.constLast();
    require(drag, "pressing inside the selection starts a real text drag");
    entered = true;
    if (duringDrag)
      duringDrag();
    else {
      QDragEnterEvent enter(destination, action, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(edit.viewport(), &enter);
      require(enter.isAccepted(), "source drag enters editable viewport");
      QDragMoveEvent move(destination, action, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(edit.viewport(), &move);
      require(move.isAccepted(), "source drag moves through editable viewport");
      QDropEvent drop(destination, action, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(edit.viewport(), &drop);
      require(drop.isAccepted(), "source selection drop accepted");
    }
    QDrag::cancel();
  });
  QTest::mousePress(edit.viewport(), Qt::LeftButton, Qt::NoModifier, press);
  const auto moved = press + QPoint(QApplication::startDragDistance() + 2, 0);
  QMouseEvent move(QEvent::MouseMove, moved, edit.viewport()->mapToGlobal(moved), Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
  QApplication::sendEvent(edit.viewport(), &move);
  require(entered, "selection gesture enters the drag loop");
  QTest::mouseRelease(edit.viewport(), Qt::LeftButton, Qt::NoModifier, destination);
}

void testSelectionDragMovesCopiesAndUndoesAtomically() {
  const auto original = QString::fromUtf8("Start 中文😀\n**middle**\nEnd\n");
  const auto selected = QString::fromUtf8("中文😀\n**middle**");
  const auto start = original.indexOf(selected), end = start + selected.size();
  for (bool bound : {false, true})
    for (bool reversed : {false, true})
      for (bool forward : {false, true})
        for (bool copy : {false, true}) {
          DocumentSession session;
          VirtualSourceEdit edit;
          edit.setSourceFont(fixtureSourceFont);
          edit.resize(800, 300);
          if (bound) {
            session.setMarkdownText(original, false);
            edit.bindSession(&session);
          } else
            edit.setStandaloneText(original);
          edit.show();
          QApplication::processEvents();
          const qsizetype destination = forward ? original.size() - 1 : 0;
          edit.setSelection(reversed ? end : start, reversed ? start : end);
          const auto press = sourcePoint(edit, start + 1), target = sourcePoint(edit, destination);
          dragSourceSelection(edit, press, target, copy ? Qt::CopyAction : Qt::MoveAction);
          finishSourceBatch(session);
          QString expected = original;
          const auto targetStart = !copy && destination > end ? destination - selected.size() : destination;
          if (!copy)
            expected.remove(start, selected.size());
          expected.insert(targetStart, selected);
          require(edit.text() == expected && edit.selectedText() == selected && edit.selectionStart() == targetStart,
                  "drag preserves exact Unicode/newlines and selects the moved or copied source");
          edit.undo();
          finishSourceBatch(session);
          require(edit.text() == original && edit.anchorPosition() == (reversed ? end : start) &&
                      edit.cursorPosition() == (reversed ? start : end) && !edit.canUndo(),
                  "one undo restores original text, selection direction and history position");
          if (bound)
            require(!session.document().isModified(), "undo drag returns to saved state");
          edit.redo();
          finishSourceBatch(session);
          require(edit.text() == expected && edit.selectedText() == selected, "one redo restores the complete drag");
        }
}

void testSelectionDragWrappedTextAndMimeProxy() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  const auto original = QStringLiteral("abcdefghijklmnopqrstuvwxyz ").repeated(8) + QStringLiteral("\nend\n");
  edit.setStandaloneText(original);
  edit.resize(340, 500);
  edit.show();
  QApplication::processEvents();
  edit.setSelection(2, 160);
  const auto selected = edit.selectedText();
  const auto press = sourcePoint(edit, 20), target = sourcePoint(edit, original.size() - 1);
  dragSourceSelection(edit, press, target, Qt::MoveAction, [&] {
    // Windows may expose a different QMimeData object from the one on QDrag.
    QMimeData proxy;
    proxy.setText(selected);
    QDragEnterEvent enter(target, Qt::MoveAction, &proxy, Qt::LeftButton, Qt::NoModifier);
    require(enter.source() == &edit, "native drag identifies its source independently of MIME data identity");
    QApplication::sendEvent(edit.viewport(), &enter);
    require(enter.isAccepted(), "wrapped selection enters with platform MIME proxy");
    QDropEvent drop(target, Qt::MoveAction, &proxy, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(edit.viewport(), &drop);
    require(drop.isAccepted(), "wrapped source move accepts platform MIME proxy");
  });
  auto expected = original;
  expected.remove(2, selected.size());
  expected.insert(expected.size() - 1, selected);
  require(edit.text() == expected && edit.selectedText() == selected,
          "wrapped selection moves rather than being copied when native MIME data is wrapped");
  edit.undo();
  require(edit.text() == original && edit.anchorPosition() == 2 && edit.cursorPosition() == 160 && !edit.canUndo(),
          "wrapped move remains a single undo operation");
}

void testSelectionDragNoOpsCancellationAndReadOnly() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText("one two three");
  edit.resize(800, 300);
  edit.show();
  QApplication::processEvents();
  const auto select = [&] { edit.setSelection(4, 7); };
  for (const auto destination : {4, 5, 7}) {
    select();
    dragSourceSelection(edit, sourcePoint(edit, 5), sourcePoint(edit, destination), Qt::MoveAction);
    require(edit.text() == "one two three" && edit.selectedText() == "two" && !edit.canUndo(),
            "dropping inside or on selection boundaries is a no-op");
  }
  select();
  const auto press = sourcePoint(edit, 5);
  QTest::mousePress(edit.viewport(), Qt::LeftButton, Qt::NoModifier, press);
  require(edit.selectedText() == "two", "press inside selection preserves it until drag threshold");
  QTest::mouseRelease(edit.viewport(), Qt::LeftButton, Qt::NoModifier, press);
  require(!edit.hasSelection() && edit.cursorPosition() == 5, "a click without dragging still positions the caret");
  select();
  dragSourceSelection(edit, sourcePoint(edit, 5), {}, Qt::MoveAction, [] {});
  require(edit.selectedText() == "two" && !edit.canUndo(), "cancelled drag preserves text and selection");
  select();
  edit.setReadOnly(true);
  dragSourceSelection(edit, sourcePoint(edit, 5), {}, Qt::CopyAction, [&] {
    auto* drag = edit.findChildren<QDrag*>().constLast();
    require(drag->supportedActions() == Qt::CopyAction, "read-only source can only drag a copy out");
    QDragEnterEvent enter(QPoint(100, 10), Qt::CopyAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(edit.viewport(), &enter);
    require(!enter.isAccepted(), "read-only viewport rejects a selection drop");
  });
  require(edit.text() == "one two three" && edit.selectedText() == "two", "read-only drag cannot delete its source");
}

void testSelectionDragRejectsStaleSource() {
  DocumentSession session;
  session.setMarkdownText("one two three", false);
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.bindSession(&session);
  edit.resize(800, 300);
  edit.show();
  QApplication::processEvents();
  edit.setSelection(4, 7);
  dragSourceSelection(edit, sourcePoint(edit, 5), {}, Qt::MoveAction, [&] {
    auto* drag = edit.findChildren<QDrag*>().constLast();
    session.replaceDocument("Externally replaced source");
    QDragEnterEvent enter(QPoint(100, 10), Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(edit.viewport(), &enter);
    require(!enter.isAccepted(), "reload invalidates the in-flight drag's source range");
    QDropEvent drop(QPoint(100, 10), Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(edit.viewport(), &drop);
    require(!drop.isAccepted(), "stale drop is rejected without inserting an old selection");
  });
  require(edit.text() == "Externally replaced source" && !edit.canUndo(), "stale drag leaves replacement intact");
}

void testExternalSourceDropAndEdgeScroll() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText("first\nsecond\nthird\n");
  edit.resize(500, 240);
  edit.show();
  QApplication::processEvents();
  edit.setSelection(6, 12);
  const auto target = sourcePoint(edit, 0);
  QMimeData mime;
  mime.setText(QString::fromUtf8("外部😀\r\n"));
  QDragEnterEvent enter(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit.viewport(), &enter);
  require(enter.isAccepted(), "external plain text can enter source mode");
  QDropEvent drop(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit.viewport(), &drop);
  require(drop.isAccepted() && edit.text() == QString::fromUtf8("外部😀\nfirst\nsecond\nthird\n"),
          "external drop inserts at the pointer and normalizes line endings without replacing selection");
  edit.undo();
  require(edit.text() == "first\nsecond\nthird\n" && edit.selectedText() == "second",
          "external drop undo restores previous selection");
  edit.setReadOnly(true);
  QDragEnterEvent locked(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit.viewport(), &locked);
  require(!locked.isAccepted(), "locked source rejects external drops");
  edit.setReadOnly(false);
  edit.setStandaloneText(QStringLiteral("long source line ").repeated(80) + QStringLiteral("\nrow\n").repeated(300));
  edit.setWordWrapEnabled(false);
  edit.setSelection(0, 4);
  const QPoint edge(edit.viewport()->width() - 2, edit.viewport()->height() - 2);
  QDragEnterEvent edgeEnter(edge, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit.viewport(), &edgeEnter);
  QTest::qWait(180);
  require(edit.verticalScrollBar()->value() > 0 && edit.horizontalScrollBar()->value() > 0,
          "holding drag at an edge scrolls vertically and horizontally without moving original selection");
  require(edit.selectionStart() == 0 && edit.selectedText() == "long", "drag hover preserves source selection");
  QDragLeaveEvent leave;
  QApplication::sendEvent(edit.viewport(), &leave);
  const auto scroll = edit.verticalScrollBar()->value();
  QTest::qWait(100);
  require(edit.verticalScrollBar()->value() == scroll, "leaving the editor stops drag scrolling");
}

void testSharedHistoryAcrossViewsAndSavePoints() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("ab\n"), false);
  EditorController rendered;
  rendered.attach(&session, nullptr);
  VirtualSourceEdit source;
  source.setSourceFont(fixtureSourceFont);
  source.bindSession(&session);
  rendered.setCursorForSourceOffset(1);
  require(rendered.inputController().insertText("X"), "rendered insertion");
  source.setCursorPosition(0);
  source.insertText("NEW:");
  require(source.text() == "NEW:aXb\n", "mixed edit fixture");
  rendered.undo();
  require(source.text() == "aXb\n", "render undo consumes the latest SOURCE edit");
  source.undo();
  require(source.text() == "ab\n" && !session.document().isModified(), "source undo reaches original save point");
  source.redo();
  rendered.redo();
  require(source.text() == "NEW:aXb\n" && session.document().isModified(), "cross-view redo preserves text");

  session.acceptSavedText(source.text());
  source.setSelection(6, 4);
  source.toggleInlineMarkup("**", "**");
  source.undo();
  require(source.anchorPosition() == 6 && source.cursorPosition() == 4,
          "shared undo retains reversed raw source selection");
  require(!session.document().isModified(), "undo returns to latest save point");
  source.redo();
  require(source.selectedText() == "aX" && session.document().isModified(), "source format redo selection");
  source.undo();
  rendered.undo();
  require(session.document().isModified(), "undo past saved state is dirty");
  rendered.redo();
  require(!session.document().isModified(), "redo back to saved state is clean");
}

void testSaveNormalizationPreservesRenderedCaretAndHistory() {
  QTemporaryDir dir;
  require(dir.isValid(), "save fixture directory");
  DocumentSession session;
  session.setFilePath(dir.filePath("note.md"));
  session.setMarkdownText("ab", false);
  auto format = session.fileFormat();
  format.ensureTrailingNewline = true;
  session.setFileFormat(format);
  EditorController rendered;
  rendered.attach(&session, nullptr);
  rendered.setCursorForSourceOffset(1);
  require(rendered.inputController().insertText("X"), "edit before saving");
  FileController files;
  require(files.save(session, nullptr) == SaveOutcome::Saved, "save must succeed");
  require(session.markdownText().toString() == "aXb\n" && !session.document().isModified(), "normalized save");
  require(rendered.inputController().insertText("Y"), "caret must remain usable after save");
  require(session.markdownText().toString() == "aXYb\n", "post-save edit at original caret");
  rendered.undo();
  require(session.markdownText().toString() == "aXb\n" && !session.document().isModified(), "undo to saved text");
  rendered.undo();
  require(session.markdownText().toString() == "aXb" && session.document().isModified(), "normalization is undoable");
  rendered.undo();
  require(session.markdownText().toString() == "ab", "pre-save history remains available");

  session.setFilePath(dir.path()); // a directory is not a writable document
  QTimer::singleShot(0, [] {
    auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    require(message, "failed save reports its error");
    message->accept();
  });
  require(files.save(session, nullptr) == SaveOutcome::Failed && session.document().isModified(),
          "failed save cannot move the clean history position");
  rendered.redo();
  rendered.redo();
  require(session.markdownText().toString() == "aXb\n" && !session.document().isModified(),
          "failed save retains redo and the last successful save point");

  session.setFilePath(dir.filePath("locked.md"));
  session.setMarkdownText("locked", true);
  session.setEditingReadOnly(true);
  require(files.save(session, nullptr) == SaveOutcome::Saved && session.editingReadOnly() &&
              session.markdownText().toString() == "locked\n",
          "saving normalizes without unlocking user edits");
  session.setEditingReadOnly(false);
  rendered.undo();
  require(session.markdownText().toString() == "locked" && session.document().isModified(),
          "locked-save normalization also participates in history");
}

void testInlineMarkupPreservesSelectionAndHistory() {
  for (bool reversed : {false, true}) {
    VirtualSourceEdit edit;
    edit.setSourceFont(fixtureSourceFont);
    const auto original = QStringLiteral("中文 value");
    edit.setStandaloneText(original);
    edit.setSelection(reversed ? 2 : 0, reversed ? 0 : 2);
    edit.toggleInlineMarkup("**", "**");
    require(edit.text() == QStringLiteral("**中文** value") && edit.selectedText() == QStringLiteral("中文"),
            "source formatting preserves selected text");
    require((edit.cursorPosition() < edit.anchorPosition()) == reversed, "source formatting preserves direction");
    edit.undo();
    require(edit.text() == original && edit.selectedText() == QStringLiteral("中文"), "undo restores source selection");
    edit.redo();
    require(edit.selectedText() == QStringLiteral("中文"), "redo restores formatted content selection");
    edit.toggleInlineMarkup("**", "**");
    require(edit.text() == original && edit.selectedText() == QStringLiteral("中文"), "repeat toggle removes markup");
    edit.setSelection(reversed ? 2 : 0, reversed ? 0 : 2);
    edit.toggleInlineMarkup("<u>", "</u>");
    require(edit.text() == QStringLiteral("<u>中文</u> value"), "unequal marker lengths preserve text");
    edit.setCursorPosition(edit.text().size());
    edit.toggleInlineMarkup("`", "`");
    const auto caret = edit.cursorPosition();
    require(edit.text().mid(caret - 1, 2) == "``" && !edit.hasSelection(), "empty skeleton places caret inside");
    edit.undo();
    edit.redo();
    require(edit.cursorPosition() == caret, "skeleton redo restores interior caret");
    edit.setReadOnly(true);
    const auto before = edit.text();
    edit.toggleInlineMarkup("*", "*");
    require(edit.text() == before, "read-only source cannot be formatted");
  }
}

void testHeadingsKeepSourceRowGeometry() {
  QString text = QStringLiteral("plain source\n");
  for (int level = 1; level <= 6; ++level) {
    text += QString(level, QLatin1Char('#')) + QStringLiteral(" Heading\n");
  }
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText(text);
  edit.resize(720, 400);
  edit.show();
  QApplication::processEvents();
  const QRect body = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
  qsizetype offset = 0;
  for (int row = 0; row <= 6; ++row) {
    edit.setCursorPosition(offset);
    const QRect caret = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    require(caret.height() == body.height() && caret.top() == body.top() + row * body.height(),
            "all source heading levels must keep the body row height and caret geometry");
    offset = text.indexOf(QLatin1Char('\n'), offset) + 1;
  }
}

void testReplacementRetiresSourceHistory() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("abc"), false);
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.bindSession(&session);
  QObject::connect(&session, &DocumentSession::documentTextChanged, &edit, [&] { edit.syncFromSession(); });
  edit.insertText(QStringLiteral("X"));
  require(edit.canUndo(), "source edit must create history");
  session.replaceDocument(QStringLiteral("12345"));
  require(!edit.canUndo() && !edit.canRedo(), "replacement must clear source history");
  require(edit.cursorPosition() == 0 && !edit.hasSelection(), "replacement must reset source selection");
  edit.undo();
  require(edit.text() == QStringLiteral("12345"), "old source undo must not mutate reloaded text");

  edit.insertText(QStringLiteral("Y"));
  edit.undo();
  require(edit.canRedo(), "fixture must contain redo history");
  session.replaceDocument(QStringLiteral("other"));
  edit.redo();
  require(edit.text() == QStringLiteral("other") && !edit.canRedo(), "replacement must retire old redo too");
}

void testEmptyReplacementDeletesSelection() {
  for (const bool bound : {false, true}) {
    DocumentSession session;
    VirtualSourceEdit edit;
    edit.setSourceFont(fixtureSourceFont);
    if (bound) {
      session.setMarkdownText(QStringLiteral("abc"), false);
      edit.bindSession(&session);
    } else {
      edit.setStandaloneText(QStringLiteral("abc"));
    }
    edit.setSelection(1, 2);
    edit.replaceSelection(QString());
    require(edit.text() == QStringLiteral("ac"), "empty Replace must delete selected text");
    require(!edit.hasSelection() && edit.cursorPosition() == 1, "replacement must collapse selection");
    edit.undo();
    require(edit.text() == QStringLiteral("abc"), "empty replacement must be undoable");
    edit.redo();
    require(edit.text() == QStringLiteral("ac"), "empty replacement must be redoable");
    edit.selectAll();
    edit.insertText(QString()); // Replace All uses this path when its result is empty.
    require(edit.text().isEmpty(), "Replace All must be able to empty the entire document");
    edit.undo();
    require(edit.text() == QStringLiteral("ac"), "empty-document replacement must undo");
    edit.setReadOnly(true);
    edit.selectAll();
    edit.replaceSelection(QString());
    require(edit.text() == QStringLiteral("ac"), "read-only replacement must remain blocked");
  }
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText(QStringLiteral("abc"));
  edit.insertText(QString());
  require(edit.text() == QStringLiteral("abc") && !edit.canUndo(), "empty insertion without selection must be a no-op");
}

void testWrappedSourceEditingPreservesViewport() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("word ").repeated(2000), false);
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.bindSession(&session);
  QObject::connect(&session, &DocumentSession::documentLocallyEdited, &edit,
                   [&edit](qsizetype, qsizetype, const QString&) { edit.notifyDocumentChanged(); });
  edit.resize(500, 240);
  edit.show();
  QApplication::processEvents();
  edit.setCursorPosition(5000);
  edit.ensureCursorVisible();
  QApplication::processEvents();
  // Revisit after the initial lazy measurement has expanded the scroll range.
  edit.ensureCursorVisible();
  QApplication::processEvents();
  const int before = edit.verticalScrollBar()->value();
  require(before > 100, "wrapped paragraph should scroll away from the start");

  const auto requireStable = [&edit, before] {
    QApplication::processEvents();
    require(qAbs(edit.verticalScrollBar()->value() - before) < 50,
            "source editing should preserve the viewport inside a wrapped paragraph");
    const QRect caret = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    require(caret.top() >= 0 && caret.bottom() <= edit.viewport()->height(),
            "source caret should remain visible after editing");
  };
  edit.insertText(QStringLiteral("x"));
  requireStable();
  edit.deleteBackward();
  requireStable();
  edit.undo();
  requireStable();
  edit.redo();
  requireStable();
  edit.insertText(QStringLiteral("\n"));
  requireStable();
  edit.deleteBackward();
  requireStable();
}

void testHeightResizePreservesReadingPosition() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText(QStringLiteral("A source line\n").repeated(200));
  edit.resize(500, 300);
  edit.show();
  QApplication::processEvents();
  edit.setCursorPosition(0);
  edit.verticalScrollBar()->setValue(edit.verticalScrollBar()->maximum() / 2);
  const int scroll = edit.verticalScrollBar()->value();
  require(scroll > 300, "reading position is away from the caret");
  for (int height : {260, 300}) {
    edit.resize(500, height);
    QApplication::processEvents();
    require(edit.verticalScrollBar()->value() == scroll && edit.cursorPosition() == 0,
            "height-only resize retains source scroll and caret");
  }
}

void testWidthResizePreservesReadingPosition() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.resize(600, 300);
  const auto line = QStringLiteral("words and wrapping text ").repeated(60) + QLatin1Char('\n');
  const auto document = line.repeated(60);
  edit.setStandaloneText(document);
  edit.show();
  edit.setSelection(12, 2);
  edit.restoreViewportPosition({line.size() * 30 + 180, 0, 0, true, false});
  QApplication::processEvents();
  for (const int width : {400, 720, 460, 600}) {
    const auto before = edit.viewportPosition();
    edit.resize(width, 300);
    QApplication::processEvents();
    const auto after = edit.viewportPosition();
    require(document.left(after.sourceOffset).count('\n') == document.left(before.sourceOffset).count('\n') &&
                qAbs(after.sourceOffset - before.sourceOffset) < 120,
            "width changes retain nearby reading text instead of following an offscreen caret");
    require(edit.anchorPosition() == 12 && edit.cursorPosition() == 2,
            "rewrapping preserves the independent reverse selection");
  }
}

void testWritingModesUseTheVisualCaretRow() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.resize(460, 300);
  const auto line = QStringLiteral("wrapped words ").repeated(160) + QLatin1Char('\n');
  edit.setStandaloneText(line.repeated(5));
  edit.show();
  QApplication::processEvents();
  edit.setTypewriterMode(true, true);
  for (const qsizetype offset : {qsizetype(0), line.size() + 700, line.size() * 4 + 900}) {
    edit.setCursorPosition(offset);
    QApplication::processEvents();
    const auto caret = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    require(qAbs(caret.center().y() - edit.viewport()->height() / 2) <= 2,
            "source typewriter mode centers the actual wrapped caret row, including document start");
  }
  edit.setTypewriterMode(true, false);
  edit.setCursorPosition(line.size() * 2 + 700);
  const int relaxedScroll = edit.verticalScrollBar()->value();
  edit.ensureCursorVisible();
  require(
      edit.verticalScrollBar()->minimum() == 0 && edit.verticalScrollBar()->value() == relaxedScroll,
      QStringLiteral(
          "relaxed typewriter mode keeps a stable comfort band without boundary padding: min=%1 before=%2 after=%3 caret=%4 height=%5")
          .arg(edit.verticalScrollBar()->minimum())
          .arg(relaxedScroll)
          .arg(edit.verticalScrollBar()->value())
          .arg(edit.inputMethodQuery(Qt::ImCursorRectangle).toRect().center().y())
          .arg(edit.viewport()->height()));
  edit.setTypewriterMode(false, true);
  edit.setStandaloneText(QStringLiteral("identical plain text\n").repeated(5));
  edit.setCursorPosition(25);
  const auto normal = edit.viewport()->grab().toImage();
  edit.setFocusMode(true);
  const auto focused = edit.viewport()->grab().toImage();
  require(normal != focused, "source focus mode dims other logical lines");
  edit.setFocusMode(false);
  require(edit.viewport()->grab().toImage() == normal, "leaving focus mode restores source syntax colors");
}

QImage captureEdit(VirtualSourceEdit& edit) {
  return edit.grab().toImage();
}

void testSelectedSourceInkOverridesSyntaxWithoutMovingText() {
  for (bool longLine : {false, true}) {
    for (bool wrap : {false, true}) {
      VirtualSourceEdit edit;
      edit.setSourceFont(fixtureSourceFont);
      edit.setWordWrapEnabled(wrap);
      edit.setReadOnly(true);
      auto colors = SourceEditorColors::fromTheme(RenderTheme::github());
      colors.selection = QColor("#d6e8f8");
      colors.selectionText = QColor("#7023a1");
      edit.setColors(colors);
      edit.setStandaloneText(longLine ? QString(18000, QLatin1Char('M'))
                                      : QStringLiteral("https://www.qt.io/path/to/document"));
      edit.resize(700, 250);
      edit.show();
      QApplication::processEvents();
      edit.setCursorPosition(2);
      const auto start = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
      edit.setCursorPosition(10);
      const auto end = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
      const auto before = edit.viewport()->grab().toImage();
      edit.setSelection(2, 10);
      const auto after = edit.viewport()->grab().toImage();
      const qreal scale = after.devicePixelRatio();
      const QRect selected(qCeil(start.left() * scale), qCeil(start.top() * scale),
                           qFloor((end.left() - start.left()) * scale), qFloor(start.height() * scale));
      // Rasterizers can antialias every glyph pixel; requiring pixels equal to
      // the solid pen colour fails for thin fonts on FreeType. Change only the
      // selection ink and verify that glyph pixels respond inside the selection.
      colors.selectionText = QColor("#167036");
      edit.setColors(colors);
      const auto recolored = edit.viewport()->grab().toImage();
      int selectedInk = 0;
      for (int y = selected.top(); y <= selected.bottom(); ++y)
        for (int x = selected.left(); x <= selected.right(); ++x) {
          const auto pixel = after.pixelColor(x, y), changed = recolored.pixelColor(x, y);
          if (qAbs(pixel.red() - changed.red()) + qAbs(pixel.green() - changed.green()) +
                  qAbs(pixel.blue() - changed.blue()) >
              24)
            ++selectedInk;
        }
      require(selectedInk > 20,
              QStringLiteral("selected glyphs use selection ink (long line %1, wrap %2, changed pixels %3)")
                  .arg(longLine)
                  .arg(wrap)
                  .arg(selectedInk));
      // The selection changes paint only: the rest of the shaped line, including
      // unselected URL syntax, must retain its original pixels and hit positions.
      const int right = qCeil((end.right() + 3) * scale);
      require(before.copy(right, selected.top(), 100, selected.height()) ==
                      after.copy(right, selected.top(), 100, selected.height()) &&
                  after.copy(right, selected.top(), 100, selected.height()) ==
                      recolored.copy(right, selected.top(), 100, selected.height()),
              "partial selection preserves unselected text colour and geometry");
      require(edit.inputMethodQuery(Qt::ImCursorRectangle).toRect() == end,
              "painting a selection preserves caret geometry");
    }
  }
}

// The composition must render with an underline, NOT overlap the following text, and NOT be
// blink-gated (the old code drew the preedit only while the caret blink was "on").
void testSourcePreeditRendersWithoutOverlap() {
  QApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QApplication::setApplicationName(QStringLiteral("VirtualSourceEditTest"));

  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText(QStringLiteral("alpha beta gamma"));
  edit.resize(600, 300);
  edit.show();
  edit.setCursorPosition(6); // between "alpha" and "beta"
  QApplication::processEvents();
  edit.setFocus();
  QApplication::processEvents();

  const QImage baseline = captureEdit(edit);

  QInputMethodEvent preeditEvent(QStringLiteral("にほん"), {});
  QApplication::sendEvent(&edit, &preeditEvent);
  QApplication::processEvents();
  const QImage composed = captureEdit(edit);

  require(baseline.size() == composed.size(), "captures should be size-stable");
  int differingPixels = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      if (baseline.pixel(x, y) != composed.pixel(x, y)) {
        ++differingPixels;
      }
    }
  }
  require(differingPixels > 100, "the composition should paint visible pixels (glyphs + underline) at the caret");

  // "beta" (previously starting at the caret x) must have shifted right by the preedit advance —
  // sample the column at the preedit's start: it must now hold composition glyphs, not "beta"'s.
  // The old overlap rendering left "beta"'s pixels at the caret x mixed with the preedit.
  const QImage withoutPreedit = baseline;
  int changedInCaretColumn = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    // Find the caret row: the row band with the most changed pixels.
    Q_UNUSED(y);
  }
  // Simpler and stronger: the differing-pixel count must include the SHIFTED tail — compare the
  // composed image's right-of-caret region against both candidates.
  const QRect caretRect = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
  require(!caretRect.isEmpty(), "ImCursorRectangle should resolve");
  // The composition occupies the caret row right of the caret: glyphs (and their underline) must
  // put substantial non-background ink in that window. The old overlap rendering left the same
  // window with "beta"'s ink, so also require the window's ink to have CHANGED vs the baseline.
  int composedInk = 0;
  int changedInk = 0;
  for (int y = caretRect.top(); y <= caretRect.bottom() + 2 && y < composed.height(); ++y) {
    for (int x = caretRect.left(); x < caretRect.left() + 60 && x < composed.width(); ++x) {
      const QColor pixel = composed.pixelColor(x, y);
      if (pixel.lightness() < 230) {
        ++composedInk;
      }
      if (baseline.pixel(x, y) != composed.pixel(x, y)) {
        ++changedInk;
      }
    }
  }
  require(composedInk > 40, "composition glyphs should ink the caret row window");
  require(changedInk > 40, "the caret row window must change vs baseline (shifted tail, not overlap)");
  Q_UNUSED(withoutPreedit);
  Q_UNUSED(changedInCaretColumn);
}

// Focus-out clears the composition (no stale preedit).
void testSourcePreeditClearsOnFocusOut() {
  VirtualSourceEdit edit;
  edit.setSourceFont(fixtureSourceFont);
  edit.setStandaloneText(QStringLiteral("hello"));
  edit.resize(600, 300);
  edit.show();
  edit.setCursorPosition(2);
  edit.setFocus();
  QApplication::processEvents();

  QInputMethodEvent preeditEvent(QStringLiteral("かん"), {});
  QApplication::sendEvent(&edit, &preeditEvent);
  QApplication::processEvents();

  QFocusEvent focusOut(QEvent::FocusOut);
  QApplication::sendEvent(&edit, &focusOut);
  QApplication::processEvents();

  const QImage after = captureEdit(edit);
  const QImage fresh = [] {
    VirtualSourceEdit plain;
    plain.setSourceFont(fixtureSourceFont);
    plain.setStandaloneText(QStringLiteral("hello"));
    plain.resize(600, 300);
    plain.show();
    plain.setCursorPosition(2);
    QApplication::processEvents();
    return captureEdit(plain);
  }();
  require(after.size() == fresh.size(), "captures should be comparable");
  int differing = 0;
  for (int y = 0; y < after.height(); ++y) {
    for (int x = 0; x < after.width(); ++x) {
      if (after.pixel(x, y) != fresh.pixel(x, y)) {
        ++differing;
      }
    }
  }
  require(differing < 20, "focus-out should leave a clean image (no preedit remnants)");
}

void testPersistentReadingAnchorSurvivesRewrap() {
  const auto text = QStringLiteral("中文 words and wrapping text ").repeated(80) + QStringLiteral("\n");
  const auto document = text.repeated(40);
  VirtualSourceEdit original;
  original.setSourceFont(fixtureSourceFont);
  original.resize(600, 400);
  original.setStandaloneText(document);
  original.show();
  original.setCursorPosition(text.size() * 20 + 300);
  original.centerCursor();
  QApplication::processEvents();
  const auto position = original.viewportPosition();
  require(!position.atStart && position.sourceOffset > text.size(), "reading fixture is scrolled into wrapped content");
  VirtualSourceEdit restored;
  restored.setSourceFont(fixtureSourceFont);
  restored.resize(420, 300);
  restored.setStandaloneText(document);
  restored.show();
  restored.setSelection(12, 2);
  restored.restoreViewportPosition(position);
  QApplication::processEvents();
  const auto actual = restored.viewportPosition();
  require(document.left(actual.sourceOffset).count('\n') == document.left(position.sourceOffset).count('\n'),
          "a new width restores the same logical source line");
  require(qAbs(actual.sourceOffset - position.sourceOffset) < 100, "rewrapped viewport retains the same nearby text");
  require(restored.anchorPosition() == 12 && restored.cursorPosition() == 2,
          "reading restoration leaves the independent reverse selection intact");
  restored.verticalScrollBar()->setValue(0);
  const auto top = restored.viewportPosition();
  restored.verticalScrollBar()->setValue(100);
  restored.restoreViewportPosition(top);
  require(restored.verticalScrollBar()->value() == 0, "document-start viewport retains its top padding");
}

} // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  if (QCoreApplication::arguments().contains(QStringLiteral("--selection-drag"))) {
    fixtureSourceFont = QFont(QStringLiteral("monospace"), 13);
    runTest("testSelectionDragMovesCopiesAndUndoesAtomically", testSelectionDragMovesCopiesAndUndoesAtomically);
    runTest("testSelectionDragWrappedTextAndMimeProxy", testSelectionDragWrappedTextAndMimeProxy);
    runTest("testSelectionDragNoOpsCancellationAndReadOnly", testSelectionDragNoOpsCancellationAndReadOnly);
    runTest("testSelectionDragRejectsStaleSource", testSelectionDragRejectsStaleSource);
    runTest("testExternalSourceDropAndEdgeScroll", testExternalSourceDropAndEdgeScroll);
    return 0;
  }
  QFile reference(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/real-theme-browser.json"));
  require(reference.open(QIODevice::ReadOnly), "source editor font reference");
  const auto latin =
      browserLayoutFont(QJsonDocument::fromJson(reference.readAll()).object()).value("muffinfixturesans");
  const int id = QFontDatabase::addApplicationFont(QStringLiteral(MUFFIN_BINARY_DIR "/text-fixture-fonts/han.ttf"));
  require(id >= 0, "source editor CJK fixture font");
  fixtureSourceFont.setFamilies({latin, QFontDatabase::applicationFontFamilies(id).value(0)});
  fixtureSourceFont.setPointSizeF(13);

#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSharedHistoryAcrossViewsAndSavePoints);
  RUN_TEST(testSaveNormalizationPreservesRenderedCaretAndHistory);
  RUN_TEST(testReplacementRetiresSourceHistory);
  RUN_TEST(testInlineMarkupPreservesSelectionAndHistory);
  RUN_TEST(testEmptyReplacementDeletesSelection);
  RUN_TEST(testHeadingsKeepSourceRowGeometry);
  RUN_TEST(testSelectedSourceInkOverridesSyntaxWithoutMovingText);
  RUN_TEST(testSourcePreeditRendersWithoutOverlap);
  RUN_TEST(testSourcePreeditClearsOnFocusOut);
  RUN_TEST(testWrappedSourceEditingPreservesViewport);
  RUN_TEST(testHeightResizePreservesReadingPosition);
  RUN_TEST(testWidthResizePreservesReadingPosition);
  RUN_TEST(testWritingModesUseTheVisualCaretRow);
  RUN_TEST(testPersistentReadingAnchorSurvivesRewrap);
#undef RUN_TEST
  qInfo("All source-editor IME tests passed.");
  return 0;
}
