#include <QTest>

class TestCmarkParser : public QObject
{
    Q_OBJECT

private slots:
    void testInit()
    {
        QVERIFY(true);
    }
};

QTEST_MAIN(TestCmarkParser)
#include "test_cmark_parser.moc"
