#include "renderer/RenderSourceMap.h"

#include <QTest>

using namespace Muffin;

class TestRenderSourceMap : public QObject
{
    Q_OBJECT

private slots:
    void mapsParagraphSpan()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true});

        QCOMPARE(map.sourceOffsetForRenderedPosition(3).value(), 3);
        QCOMPARE(map.sourceSpanForRenderedRange(1, 4).value().start, 1);
        QCOMPARE(map.sourceSpanForRenderedRange(1, 4).value().end, 4);
        QCOMPARE(map.renderedPositionForSourceOffset(5).value(), 5);
        QVERIFY(map.isEditableRenderedRange(0, 5));
    }

    void rejectsUnsupportedSpan()
    {
        RenderSourceMap map;
        map.addSpan({0, 4, {2, 6}, {1, 1, 1, 8}, RenderSpan::Kind::Strong, false});

        QVERIFY(!map.sourceSpanForRenderedRange(0, 4).has_value());
        QVERIFY(!map.isEditableRenderedRange(0, 4));
    }

    void mapsHeadingContentAfterMarker()
    {
        SourceCoordinateMapper mapper(QStringLiteral("## Hello"));
        SourceSpan span = mapper.headingContentSpan({1, 1, 1, 8}, 2);

        QCOMPARE(span.start, 3);
        QCOMPARE(span.end, 8);
    }

    void mapsChineseUtf8ColumnsToUtf16Offsets()
    {
        SourceCoordinateMapper mapper(QStringLiteral("a你b"));

        QCOMPARE(mapper.offsetForLineColumn(1, 1), 0);
        QCOMPARE(mapper.offsetForLineColumn(1, 2), 1);
        QCOMPARE(mapper.offsetForLineColumn(1, 5), 2);
        QCOMPARE(mapper.offsetForLineColumn(1, 6), 3);
    }

    void mapsEmojiUtf8ColumnsToUtf16Offsets()
    {
        SourceCoordinateMapper mapper(QStringLiteral("a😀b"));

        QCOMPARE(mapper.offsetForLineColumn(1, 1), 0);
        QCOMPARE(mapper.offsetForLineColumn(1, 2), 1);
        QCOMPARE(mapper.offsetForLineColumn(1, 6), 3);
        QCOMPARE(mapper.offsetForLineColumn(1, 7), 4);
    }

    void mapsHeadingContentWithChineseText()
    {
        SourceCoordinateMapper mapper(QStringLiteral("## 你好"));
        SourceSpan span = mapper.headingContentSpan({1, 1, 1, 9}, 2);

        QCOMPARE(span.start, 3);
        QCOMPARE(span.end, 5);
    }

    void mapsRenderedPositionForUtf8SourceOffset()
    {
        RenderSourceMap map;
        map.addSpan({0, 4, {3, 5}, {1, 4, 1, 9}, RenderSpan::Kind::Text, true, false, {3, 5}, RenderSpan::EditPolicy::LinearText});

        QCOMPARE(map.renderedPositionForSourceOffset(3).value(), 0);
        QCOMPARE(map.renderedPositionForSourceOffset(4).value(), 1);
        QCOMPARE(map.renderedPositionForSourceOffset(5).value(), 2);
    }

    void mapsSelectionAcrossAdjacentEditableSpans()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Text, true, false, {0, 2}, RenderSpan::EditPolicy::LinearText});
        map.addSpan({2, 5, {2, 5}, {1, 3, 1, 5}, RenderSpan::Kind::Text, true, false, {2, 5}, RenderSpan::EditPolicy::LinearText});

        SourceSpan span = map.editableSourceSpanForRenderedRange(1, 4).value();

        QCOMPARE(span.start, 1);
        QCOMPARE(span.end, 4);
    }

    void rejectsSelectionAcrossNonEditableGap()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Text, true, false, {0, 2}, RenderSpan::EditPolicy::LinearText});
        map.addSpan({2, 4, {2, 8}, {1, 3, 1, 8}, RenderSpan::Kind::Strong, false, false, {2, 8}, RenderSpan::EditPolicy::None});
        map.addSpan({4, 6, {8, 10}, {1, 9, 1, 10}, RenderSpan::Kind::Text, true, false, {8, 10}, RenderSpan::EditPolicy::LinearText});

        QVERIFY(!map.editableSourceSpanForRenderedRange(1, 5).has_value());
    }

    void rejectsInteriorMappingForNonLinearText()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true, false, {0, 5}, RenderSpan::EditPolicy::NonLinearText});

        QCOMPARE(map.sourceOffsetForRenderedPosition(0).value(), 0);
        QVERIFY(!map.sourceOffsetForRenderedPosition(1).has_value());
        QCOMPARE(map.sourceOffsetForRenderedPosition(2).value(), 5);
        QCOMPARE(map.sourceSpanForRenderedRange(0, 2).value().end, 5);
        QVERIFY(!map.sourceSpanForRenderedRange(0, 1).has_value());
    }

    void mapsWholeAtomicReplacementOnly()
    {
        RenderSourceMap map;
        map.addSpan({0, 1, {1, 6}, {1, 1, 1, 6}, RenderSpan::Kind::FormulaInline, true, false, {1, 6}, RenderSpan::EditPolicy::Atomic});

        SourceSpan span = map.editableSourceSpanForRenderedRange(0, 1).value();

        QCOMPARE(span.start, 1);
        QCOMPARE(span.end, 6);
        QCOMPARE(map.editableSourceInsertionPoint(0).value(), 1);
        QCOMPARE(map.editableSourceInsertionPoint(1).value(), 6);
    }
};

QTEST_MAIN(TestRenderSourceMap)
#include "test_render_source_map.moc"
