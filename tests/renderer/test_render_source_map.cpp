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
};

QTEST_MAIN(TestRenderSourceMap)
#include "test_render_source_map.moc"
