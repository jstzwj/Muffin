#include "editor/EditorSelectionMapper.h"
#include "parser/CmarkParser.h"

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

    void buildsBookmarkFromSourceOffset()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("# Title\n\nParagraph");
        ParseResult parsed = parser.parseDocument(markdown);

        SelectionBookmark bookmark = EditorSelectionMapper::bookmarkForSourceOffset(
            parsed.document, markdown.indexOf(QStringLiteral("Paragraph")));

        QVERIFY(bookmark.isValid());
        QVERIFY(bookmark.nodeId != 0);
        QVERIFY(bookmark.sourceOffset >= 0);
    }

    void restoresRenderedPositionFromBookmark()
    {
        RenderSourceMap map;
        RenderSpan span{0, 9, {9, 18}, {3, 1, 3, 9}, RenderSpan::Kind::Paragraph, true, true,
                        {9, 18}, RenderSpan::EditPolicy::BlockContent, 77};
        map.addSpan(span);

        SelectionBookmark bookmark{77, 12};
        std::optional<int> renderedPosition = EditorSelectionMapper::renderedPositionForBookmark(map, bookmark);

        QVERIFY(renderedPosition.has_value());
        QCOMPARE(*renderedPosition, 3);
    }

    void restoresRenderedPositionUsingNodeLocalOffsetFirst()
    {
        CmarkParser parser;
        const QString beforeMarkdown = QStringLiteral("# T\n\nAlpha bold omega");
        const QString afterMarkdown = QStringLiteral("# Title expanded\n\nAlpha bold omega");
        ParseResult before = parser.parseDocument(beforeMarkdown);
        ParseResult after = parser.parseDocument(afterMarkdown, before.document);

        const int beforeBoldOffset = static_cast<int>(beforeMarkdown.indexOf(QStringLiteral("bold")));
        const int afterBoldOffset = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("bold")));
        const int afterParagraphStart = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("Alpha")));
        SelectionBookmark bookmark =
            EditorSelectionMapper::bookmarkForSourceOffset(before.document, beforeBoldOffset);
        QVERIFY(bookmark.isValid());
        QCOMPARE(bookmark.sourceOffset, beforeBoldOffset);

        RenderSourceMap map;
        RenderSpan span{0,
                        static_cast<int>(afterMarkdown.size()) - afterParagraphStart,
                        {afterParagraphStart, static_cast<int>(afterMarkdown.size())},
                        {3, 1, 3, static_cast<int>(afterMarkdown.size()) - afterParagraphStart},
                        RenderSpan::Kind::Paragraph,
                        true,
                        true,
                        {afterParagraphStart, static_cast<int>(afterMarkdown.size())},
                        RenderSpan::EditPolicy::LinearText,
                        bookmark.nodeId};
        map.addSpan(span);

        std::optional<int> renderedPosition = EditorSelectionMapper::renderedPositionForBookmark(map, bookmark);

        QVERIFY(renderedPosition.has_value());
        QCOMPARE(*renderedPosition, afterBoldOffset - afterParagraphStart);
        QVERIFY(after.document.nodeById(bookmark.nodeId));
    }

    void buildsRangeBookmarkFromSourceSelection()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("- A\n- B\n- C");
        ParseResult parsed = parser.parseDocument(markdown);

        SelectionRangeBookmark bookmark = EditorSelectionMapper::bookmarkForSourceSelection(parsed.document, {6, 11});

        QVERIFY(bookmark.isValid());
        QCOMPARE(bookmark.anchor.sourceOffset, 6);
        QCOMPARE(bookmark.focus.sourceOffset, 11);
        QCOMPARE(bookmark.anchor.affinity, SelectionBookmark::Affinity::Forward);
        QCOMPARE(bookmark.focus.affinity, SelectionBookmark::Affinity::Backward);
    }

    void buildsRangeBookmarkPreservingDirection()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("- A\n- B\n- C");
        ParseResult parsed = parser.parseDocument(markdown);

        SelectionRangeBookmark bookmark = EditorSelectionMapper::bookmarkForSourceSelection(parsed.document, {11, 6});

        QVERIFY(bookmark.isValid());
        QCOMPARE(bookmark.anchor.sourceOffset, 11);
        QCOMPARE(bookmark.focus.sourceOffset, 6);

        std::optional<SourceSelection> sourceSelection = EditorSelectionMapper::sourceSelectionForBookmark(bookmark);
        QVERIFY(sourceSelection.has_value());
        QCOMPARE(sourceSelection->start, 11);
        QCOMPARE(sourceSelection->end, 6);

        std::optional<SourceSelection> orderedSelection = EditorSelectionMapper::orderedSourceSelectionForBookmark(bookmark);
        QVERIFY(orderedSelection.has_value());
        QCOMPARE(orderedSelection->start, 6);
        QCOMPARE(orderedSelection->end, 11);
    }

    void restoresRenderedSelectionFromRangeBookmark()
    {
        RenderSourceMap map;
        RenderSpan firstSpan{0, 1, {2, 3}, {1, 3, 1, 3}, RenderSpan::Kind::List, true, true,
                             {2, 3}, RenderSpan::EditPolicy::LinearText, 10};
        RenderSpan secondSpan{2, 3, {6, 7}, {2, 3, 2, 3}, RenderSpan::Kind::List, true, true,
                              {6, 7}, RenderSpan::EditPolicy::LinearText, 20};
        map.addSpan(firstSpan);
        map.addSpan(secondSpan);

        SelectionRangeBookmark bookmark{{10, 2, -1, SelectionBookmark::Affinity::Forward},
                                        {20, 7, -1, SelectionBookmark::Affinity::Forward}};
        std::optional<SourceSelection> selection = EditorSelectionMapper::renderedSelectionForBookmark(map, bookmark);

        QVERIFY(selection.has_value());
        QCOMPARE(selection->start, 0);
        QCOMPARE(selection->end, 3);
    }

    void restoresRenderedSelectionPreservingDirection()
    {
        RenderSourceMap map;
        RenderSpan firstSpan{0, 1, {2, 3}, {1, 3, 1, 3}, RenderSpan::Kind::List, true, true,
                             {2, 3}, RenderSpan::EditPolicy::LinearText, 10};
        RenderSpan secondSpan{2, 3, {6, 7}, {2, 3, 2, 3}, RenderSpan::Kind::List, true, true,
                              {6, 7}, RenderSpan::EditPolicy::LinearText, 20};
        map.addSpan(firstSpan);
        map.addSpan(secondSpan);

        SelectionRangeBookmark bookmark{{20, 7, -1, SelectionBookmark::Affinity::Forward},
                                        {10, 2, -1, SelectionBookmark::Affinity::Forward}};
        std::optional<SourceSelection> selection = EditorSelectionMapper::renderedSelectionForBookmark(map, bookmark);

        QVERIFY(selection.has_value());
        QCOMPARE(selection->start, 3);
        QCOMPARE(selection->end, 0);

        std::optional<SourceSelection> orderedSelection =
            EditorSelectionMapper::orderedRenderedSelectionForBookmark(map, bookmark);
        QVERIFY(orderedSelection.has_value());
        QCOMPARE(orderedSelection->start, 0);
        QCOMPARE(orderedSelection->end, 3);
    }

    void bookmarkAffinityOverridesRenderedPositionBias()
    {
        RenderSourceMap map;
        RenderSpan span{10, 15, {20, 25}, {1, 1, 1, 5}, RenderSpan::Kind::Paragraph, true, true,
                        {20, 25}, RenderSpan::EditPolicy::LinearText, 77};
        map.addSpan(span);

        SelectionBookmark forward{77, 100, -1, SelectionBookmark::Affinity::Forward};
        SelectionBookmark backward{77, 100, -1, SelectionBookmark::Affinity::Backward};

        std::optional<int> forwardPosition =
            EditorSelectionMapper::renderedPositionForBookmark(map, forward, RenderSourceMap::Bias::Backward);
        std::optional<int> backwardPosition =
            EditorSelectionMapper::renderedPositionForBookmark(map, backward, RenderSourceMap::Bias::Forward);

        QVERIFY(forwardPosition.has_value());
        QVERIFY(backwardPosition.has_value());
        QCOMPARE(*forwardPosition, 10);
        QCOMPARE(*backwardPosition, 15);
    }

    void restoreRenderedSelectionPrefersRangeBookmark()
    {
        RenderSourceMap map;
        RenderSpan span{0, 10, {10, 20}, {1, 1, 1, 10}, RenderSpan::Kind::Paragraph, true, true,
                        {10, 20}, RenderSpan::EditPolicy::LinearText, 77};
        map.addSpan(span);

        RenderedSelectionRestoreRequest request;
        request.rangeBookmark = SelectionRangeBookmark{{77, 12, -1, SelectionBookmark::Affinity::Forward},
                                                       {77, 15, -1, SelectionBookmark::Affinity::Forward}};
        request.caretBookmark = SelectionBookmark{77, 18, -1, SelectionBookmark::Affinity::Backward};
        request.fallbackSourceOffset = 19;
        const RenderedSelectionRestoreResult result =
            EditorSelectionMapper::restoreRenderedSelection(map, request);

        QVERIFY(result.selection.has_value());
        QVERIFY(!result.cursorPosition.has_value());
        QCOMPARE(result.method, RenderedSelectionRestoreMethod::RangeBookmark);
        QCOMPARE(result.selection->start, 2);
        QCOMPARE(result.selection->end, 5);
    }

    void restoreRenderedSelectionFallsBackToCaretBookmark()
    {
        RenderSourceMap map;
        RenderSpan span{0, 10, {10, 20}, {1, 1, 1, 10}, RenderSpan::Kind::Paragraph, true, true,
                        {10, 20}, RenderSpan::EditPolicy::LinearText, 77};
        map.addSpan(span);

        RenderedSelectionRestoreRequest request;
        request.rangeBookmark = SelectionRangeBookmark{{999, 100, -1, SelectionBookmark::Affinity::Forward},
                                                       {999, 101, -1, SelectionBookmark::Affinity::Forward}};
        request.caretBookmark = SelectionBookmark{77, 18, -1, SelectionBookmark::Affinity::Backward};
        request.fallbackSourceOffset = 19;
        const RenderedSelectionRestoreResult result =
            EditorSelectionMapper::restoreRenderedSelection(map, request);

        QVERIFY(!result.selection.has_value());
        QVERIFY(result.cursorPosition.has_value());
        QCOMPARE(result.method, RenderedSelectionRestoreMethod::CaretBookmark);
        QCOMPARE(*result.cursorPosition, 8);
    }

    void restoreRenderedSelectionFallsBackToSourceOffset()
    {
        RenderSourceMap map;
        RenderSpan span{0, 10, {10, 20}, {1, 1, 1, 10}, RenderSpan::Kind::Paragraph, true, true,
                        {10, 20}, RenderSpan::EditPolicy::LinearText, 77};
        map.addSpan(span);

        RenderedSelectionRestoreRequest request;
        request.rangeBookmark = SelectionRangeBookmark{{999, 100, -1, SelectionBookmark::Affinity::Forward},
                                                       {999, 101, -1, SelectionBookmark::Affinity::Forward}};
        request.caretBookmark = SelectionBookmark{};
        request.fallbackSourceOffset = 19;
        const RenderedSelectionRestoreResult result =
            EditorSelectionMapper::restoreRenderedSelection(map, request);

        QVERIFY(!result.selection.has_value());
        QVERIFY(result.cursorPosition.has_value());
        QCOMPARE(result.method, RenderedSelectionRestoreMethod::SourceOffset);
        QCOMPARE(*result.cursorPosition, 9);
    }

    void restoreRenderedSelectionReportsNoneWhenAllFallbacksFail()
    {
        RenderSourceMap map;
        RenderedSelectionRestoreRequest request;
        request.rangeBookmark = SelectionRangeBookmark{{999, 100, -1, SelectionBookmark::Affinity::Forward},
                                                       {999, 101, -1, SelectionBookmark::Affinity::Forward}};
        request.caretBookmark = SelectionBookmark{999, 200, -1, SelectionBookmark::Affinity::Backward};
        request.fallbackSourceOffset = 300;

        const RenderedSelectionRestoreResult result =
            EditorSelectionMapper::restoreRenderedSelection(map, request);

        QVERIFY(!result.hasValue());
        QCOMPARE(result.method, RenderedSelectionRestoreMethod::None);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestEditorSelectionMapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_editor_selection_mapper.moc"
