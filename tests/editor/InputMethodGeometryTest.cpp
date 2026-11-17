#include "document/DocumentSession.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/VirtualSourceEdit.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "../TestUtils.h"

#include <QApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QVBoxLayout>

using namespace muffin;

namespace {
struct EditorFixture {
  DocumentSession session;
  QWidget window;
  QVBoxLayout layout{&window};
  EditorView rendered;
  VirtualSourceEdit source;
  EditorController controller;
  bool sourceMode;

  explicit EditorFixture(bool sourceMode) : sourceMode(sourceMode) {
    layout.setContentsMargins(23, 31, 17, 19);
    layout.addWidget(editor());
    controller.attach(&session, &rendered);
    source.bindSession(&session);
    QString text;
    for (int n = 0; n < 30; ++n)
      text += QStringLiteral("Paragraph %1 with some words and 中文 text.\n\n").arg(n);
    setText(text);
    window.resize(640, 400);
    window.show();
    window.activateWindow();
    editor()->setFocus();
    settle();
  }

  QAbstractScrollArea* editor() { return sourceMode ? static_cast<QAbstractScrollArea*>(&source) : &rendered; }
  void setText(const QString& text) {
    session.setMarkdownText(text, false);
    rendered.setDocument(session.document());
  }
  void move(qsizetype offset) {
    if (sourceMode)
      source.setCursorPosition(offset);
    else
      controller.setCursorForSourceOffset(offset);
  }
  QRectF cursorRect() { return editor()->inputMethodQuery(Qt::ImCursorRectangle).toRectF(); }
  static void settle() {
    for (int n = 0; n < 6; ++n)
      QCoreApplication::processEvents();
  }
};
} // namespace

class InputMethodGeometryTest : public QObject {
  Q_OBJECT
private slots:
  void caretAndScrollNotifyInputMethod_data() {
    QTest::addColumn<bool>("sourceMode");
    QTest::newRow("rendered") << false;
    QTest::newRow("source") << true;
  }
  void caretAndScrollNotifyInputMethod() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    QTRY_COMPARE(QApplication::focusWidget(), f.editor());
    f.move(3);
    EditorFixture::settle();
    QSignalSpy changes(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged);
    QRectF published;
    const auto connection = connect(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged, this,
                                    [&] { published = qApp->inputMethod()->cursorRectangle(); });
    const auto initial = f.cursorRect();
    f.move(f.session.markdownText().toString().indexOf(QStringLiteral("Paragraph 2")) + 5);
    QVERIFY(f.cursorRect().top() > initial.top());
    QTRY_VERIFY(!changes.isEmpty());
    QCOMPARE(published, qApp->inputMethod()->cursorRectangle());
    EditorFixture::settle();
    changes.clear();
    const auto beforeScroll = f.cursorRect();
    f.editor()->verticalScrollBar()->setValue(20);
    QTRY_VERIFY(!changes.isEmpty());
    QCOMPARE(f.cursorRect().top(), beforeScroll.top() - 20);
    QCOMPARE(published, qApp->inputMethod()->cursorRectangle());
    disconnect(connection);
  }

  void updatesCoalesceAndIgnoreInactiveEditor_data() { caretAndScrollNotifyInputMethod_data(); }
  void updatesCoalesceAndIgnoreInactiveEditor() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    f.move(0);
    EditorFixture::settle();
    QSignalSpy changes(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged);
    f.move(1);
    f.move(2);
    f.move(3);
    QCOMPARE(changes.size(), 0); // Never query half-rebuilt layout synchronously.
    QTRY_COMPARE(changes.size(), 1);
    EditorFixture::settle();
    changes.clear();
    for (int n = 0; n < 3; ++n) {
      f.editor()->viewport()->update(); // Caret blink/decoration repaint is not an IME state change.
      EditorFixture::settle();
    }
    QCOMPARE(changes.size(), 0);
    f.move(4);
    f.editor()->clearFocus(); // A queued update must not publish into the next editor's context.
    EditorFixture::settle();
    QCOMPARE(changes.size(), 0);
  }

  void viewportCoordinatesAreMappedToEditor_data() { caretAndScrollNotifyInputMethod_data(); }
  void viewportCoordinatesAreMappedToEditor() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    f.move(4);
    EditorFixture::settle();
    const auto before = f.cursorRect();
    f.editor()->setFrameStyle(QFrame::Box | QFrame::Plain);
    f.editor()->setLineWidth(7);
    EditorFixture::settle();
    const auto viewportOffset = f.editor()->viewport()->pos();
    QVERIFY(viewportOffset.x() >= 7 && viewportOffset.y() >= 7);
    const auto after = f.cursorRect();
    QCOMPARE(after.left() - before.left(), qreal(viewportOffset.x()));
    QCOMPARE(after.top() - before.top(), qreal(viewportOffset.y()));
    QCOMPARE(f.editor()->inputMethodQuery(Qt::ImInputItemClipRectangle).toRectF(),
             QRectF(f.editor()->viewport()->geometry()));
  }

  void preeditCursorFollowsComposition_data() { caretAndScrollNotifyInputMethod_data(); }
  void preeditCursorFollowsComposition() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    f.setText(QStringLiteral("Some text before the insertion point and after."));
    f.move(8);
    f.editor()->setFocus();
    EditorFixture::settle();
    const auto insertion = f.cursorRect();
    QSignalSpy changes(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged);
    QList<QInputMethodEvent::Attribute> attributes{{QInputMethodEvent::Cursor, 4, 1, {}}};
    QInputMethodEvent composition(QStringLiteral("nihao"), attributes);
    QApplication::sendEvent(f.editor(), &composition);
    EditorFixture::settle();
    QVERIFY(f.cursorRect().left() > insertion.left() + 5);
    QVERIFY(!changes.isEmpty());
    QInputMethodEvent cancel;
    QApplication::sendEvent(f.editor(), &cancel);
    EditorFixture::settle();
    QCOMPARE(f.cursorRect(), insertion);
  }

  void windowMoveNotifiesEvenWithSameLocalCursor_data() { caretAndScrollNotifyInputMethod_data(); }
  void windowMoveNotifiesEvenWithSameLocalCursor() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    f.move(4);
    EditorFixture::settle();
    const auto before = f.cursorRect();
    QSignalSpy changes(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged);
    f.window.move(f.window.pos() + QPoint(40, 30));
    QTRY_VERIFY(!changes.isEmpty());
    QCOMPARE(f.cursorRect(), before);
  }

  void renderedCompositionWrapsAndTracksTableCell() {
    EditorFixture f(false);
    f.rendered.setFixedWidth(180);
    f.setText(QStringLiteral("abc"));
    f.move(3);
    EditorFixture::settle();
    const auto before = f.cursorRect();
    const QString text(100, QLatin1Char('a'));
    QList<QInputMethodEvent::Attribute> attrs{{QInputMethodEvent::Cursor, int(text.size()), 1, {}}};
    QInputMethodEvent composition(text, attrs);
    QApplication::sendEvent(&f.rendered, &composition);
    EditorFixture::settle();
    QVERIFY(f.cursorRect().top() > before.top());
    QInputMethodEvent cancel;
    QApplication::sendEvent(&f.rendered, &cancel);
    f.rendered.setFixedWidth(550);
    const QString table = QStringLiteral("| alpha | beta |\n| --- | --- |\n| gamma | delta |\n");
    f.setText(table);
    f.move(table.indexOf(QStringLiteral("gamma")) + 2);
    EditorFixture::settle();
    QCOMPARE(f.rendered.cursorHit().zone, HitTestResult::Zone::TableCell);
    const auto insertion = f.cursorRect();
    attrs = {{QInputMethodEvent::Cursor, 3, 1, {}}};
    QInputMethodEvent cellComposition(QStringLiteral("nihao"), attrs);
    QApplication::sendEvent(&f.rendered, &cellComposition);
    EditorFixture::settle();
    QVERIFY(f.cursorRect().left() > insertion.left() + 5);
    QCOMPARE(f.rendered.inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("gamma"));
  }

  void horizontalScrollTracksPaintedCursor_data() { caretAndScrollNotifyInputMethod_data(); }
  void horizontalScrollTracksPaintedCursor() {
    QFETCH(bool, sourceMode);
    EditorFixture f(sourceMode);
    const QString line(200, QLatin1Char('w'));
    SettingsOverride wrap("markdown/codeBlockWrap", false);
    f.source.setWordWrapEnabled(false);
    f.setText(sourceMode ? line : QStringLiteral("```cpp\n") + line + QStringLiteral("\n```"));
    f.move(sourceMode ? 90 : 97);
    EditorFixture::settle();
    const auto before = f.cursorRect();
    QSignalSpy changes(qApp->inputMethod(), &QInputMethod::cursorRectangleChanged);
    if (sourceMode) {
      auto* bar = f.source.horizontalScrollBar();
      QVERIFY(bar->maximum() > bar->value() + 15);
      bar->setValue(bar->value() + 15);
    } else {
      const auto blockId = f.rendered.cursorPosition().blockId;
      auto& scroll = f.controller.codeFenceScroll();
      scroll.setOffset(blockId, scroll.offsetFor(blockId) + 15);
      f.rendered.viewport()->update();
    }
    QTRY_VERIFY(!changes.isEmpty());
    QCOMPARE(f.cursorRect().left(), before.left() - 15);
  }

  void sourceContextUsesOffsetsWithinSurroundingText() {
    EditorFixture f(true);
    f.setText(QStringLiteral("first line\nsecond 中文 line\nlast line"));
    f.source.setSelection(13, 18);
    QCOMPARE(f.source.inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("second 中文 line"));
    QCOMPARE(f.source.inputMethodQuery(Qt::ImCursorPosition).toInt(), 7);
    QCOMPARE(f.source.inputMethodQuery(Qt::ImAnchorPosition).toInt(), 2);
    QCOMPARE(f.source.inputMethodQuery(Qt::ImAbsolutePosition).toLongLong(), qint64(18));
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  InputMethodGeometryTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "InputMethodGeometryTest.moc"
