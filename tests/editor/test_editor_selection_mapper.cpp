#include "editor/EditorSelectionMapper.h"

#include <QApplication>
#include <QPlainTextEdit>
#include <QTest>

using namespace Muffin;

class TestEditorSelectionMapper : public QObject
{
    Q_OBJECT

private slots:
    void mapsRenderedBlockRangeToSourceSelection()
    {
        RenderedCommandTarget target{{2, 1, 2, 3}, {}};
        SourceSelection selection = EditorSelectionMapper::sourceSelectionForRenderedTarget(
            QStringLiteral("One\nTwo three"), target, false);

        QCOMPARE(selection.start, 4);
        QCOMPARE(selection.end, 7);
    }

    void mapsRenderedInlineTextToSourceSelection()
    {
        RenderedCommandTarget target{{1, 1, 1, 15}, QStringLiteral("middle")};
        SourceSelection selection = EditorSelectionMapper::sourceSelectionForRenderedTarget(
            QStringLiteral("start middle end"), target, true);

        QCOMPARE(selection.start, 6);
        QCOMPARE(selection.end, 12);
    }

    void readsEditorSelection()
    {
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("Hello world"));
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(6);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);

        SourceSelection selection = EditorSelectionMapper::sourceSelectionForEditor(&editor);

        QCOMPARE(selection.start, 6);
        QCOMPARE(selection.end, 11);
    }

    void movesCursorToRange()
    {
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("One\nTwo\nThree"));

        QVERIFY(EditorSelectionMapper::moveSourceCursorToRange(&editor, {2, 1, 2, 3}, true));

        QTextCursor cursor = editor.textCursor();
        QCOMPARE(cursor.selectionStart(), 4);
        QCOMPARE(cursor.selectionEnd(), 7);
    }

    void movesCursorToInlineText()
    {
        QPlainTextEdit editor;
        editor.setPlainText(QStringLiteral("One\nTwo middle\nThree"));

        QVERIFY(EditorSelectionMapper::moveSourceCursorToInlineText(&editor, {2, 1, 2, 10}, QStringLiteral("middle")));

        QTextCursor cursor = editor.textCursor();
        QCOMPARE(cursor.selectionStart(), 8);
        QCOMPARE(cursor.selectionEnd(), 14);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestEditorSelectionMapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_editor_selection_mapper.moc"
