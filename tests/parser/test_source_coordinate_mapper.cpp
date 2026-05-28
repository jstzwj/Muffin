#include "parser/SourceCoordinateMapper.h"

#include <QTest>

using namespace Muffin;

class TestSourceCoordinateMapper : public QObject
{
    Q_OBJECT

private slots:
    void mapsAsciiLineColumnToUtf16Offset()
    {
        SourceCoordinateMapper mapper(QStringLiteral("abc\ndef"));

        QCOMPARE(mapper.offsetForLineColumn(1, 1), 0);
        QCOMPARE(mapper.offsetForLineColumn(1, 4), 3);
        QCOMPARE(mapper.offsetForLineColumn(2, 1), 4);
        QCOMPARE(mapper.offsetForLineColumn(2, 4), 7);
    }

    void mapsUtf8ByteColumnsToUtf16Offsets()
    {
        SourceCoordinateMapper mapper(QStringLiteral("a你😀b"));

        QCOMPARE(mapper.offsetForLineColumn(1, 1), 0);
        QCOMPARE(mapper.offsetForLineColumn(1, 2), 1);
        QCOMPARE(mapper.offsetForLineColumn(1, 5), 2);
        QCOMPARE(mapper.offsetForLineColumn(1, 9), 4);
        QCOMPARE(mapper.offsetForLineColumn(1, 10), 5);
    }

    void mapsSourceRangeAcrossUtf8Characters()
    {
        SourceCoordinateMapper mapper(QStringLiteral("标题\n你好 world"));
        const SourceSpan span = mapper.spanForRange({2, 1, 2, 6});

        QCOMPARE(span.start, 3);
        QCOMPARE(span.end, 5);
    }

    void mapsHeadingContentAfterMarker()
    {
        SourceCoordinateMapper mapper(QStringLiteral("### 你好"));
        const SourceSpan span = mapper.headingContentSpan({1, 1, 1, 10}, 3);

        QCOMPARE(span.start, 4);
        QCOMPARE(span.end, 6);
    }

    void mapsLineAndColumnForOffset()
    {
        SourceCoordinateMapper mapper(QStringLiteral("a你\n😀b"));

        QCOMPARE(mapper.lineForOffset(0), 1);
        QCOMPARE(mapper.lineForOffset(2), 1);
        QCOMPARE(mapper.lineForOffset(3), 2);
        QCOMPARE(mapper.columnForOffset(0), 1);
        QCOMPARE(mapper.columnForOffset(2), 5);
        QCOMPARE(mapper.columnForOffset(3), 1);
        QCOMPARE(mapper.columnForOffset(5), 5);
    }
};

QTEST_MAIN(TestSourceCoordinateMapper)
#include "test_source_coordinate_mapper.moc"
