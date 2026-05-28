#include "parser/CmarkParser.h"
#include <QTest>

using namespace Muffin;

class TestMathDelimiterScanner : public QObject
{
    Q_OBJECT

private slots:
    void detectsMultilineDisplayMath()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("before\n\n$$\nE = mc^2\n$$\n\nafter");

        ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(result.mathSpans.size(), 1);
        const MathSpan span = result.mathSpans.first();
        QVERIFY(span.display);
        QCOMPARE(span.tex, QStringLiteral("E = mc^2"));
        QCOMPARE(markdown.mid(span.source.start, span.source.end - span.source.start), QStringLiteral("$$\nE = mc^2\n$$"));
        QCOMPARE(markdown.mid(span.content.start, span.content.end - span.content.start), QStringLiteral("E = mc^2"));
    }

    void detectsStandaloneSingleLineDisplayMath()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("  $$E = mc^2$$  ");

        ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(result.mathSpans.size(), 1);
        QVERIFY(result.mathSpans.first().display);
        QCOMPARE(result.mathSpans.first().tex, QStringLiteral("E = mc^2"));
    }

    void ignoresUnmatchedDisplayMath()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("$$\nE = mc^2");

        ParseResult result = parser.parseDocument(markdown);

        QVERIFY(result.mathSpans.isEmpty());
    }

    void ignoresDisplayMathInsideFencedCode()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("```cpp\n$$\nnot math\n$$\n```");

        ParseResult result = parser.parseDocument(markdown);

        QVERIFY(result.mathSpans.isEmpty());
    }

    void detectsInlineMath()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("hello $x+y$ world");

        ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(result.mathSpans.size(), 1);
        const MathSpan span = result.mathSpans.first();
        QVERIFY(!span.display);
        QCOMPARE(span.tex, QStringLiteral("x+y"));
        QCOMPARE(markdown.mid(span.source.start, span.source.end - span.source.start), QStringLiteral("$x+y$"));
    }

    void ignoresEscapedDollar()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("price \\$x$ text");

        ParseResult result = parser.parseDocument(markdown);

        QVERIFY(result.mathSpans.isEmpty());
    }

    void ignoresDoubleDollarAsInlineMath()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("a $$x$$ b");

        ParseResult result = parser.parseDocument(markdown);

        QVERIFY(result.mathSpans.isEmpty());
    }

    void avoidsCurrencyFalsePositive()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("Costs are $5 and $6 today.");

        ParseResult result = parser.parseDocument(markdown);

        QVERIFY(result.mathSpans.isEmpty());
    }

    void detectsMultipleInlineFormulas()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("$a$ plus $b$");

        ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(result.mathSpans.size(), 2);
        QCOMPARE(result.mathSpans.at(0).tex, QStringLiteral("a"));
        QCOMPARE(result.mathSpans.at(1).tex, QStringLiteral("b"));
    }
};

QTEST_MAIN(TestMathDelimiterScanner)
#include "test_math_delimiter_scanner.moc"
