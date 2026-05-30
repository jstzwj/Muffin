#include "editor/QuoteCommandHandler.h"
#include "command_handler_test_utils.h"

#include <QTest>

using namespace Muffin;
using TestUtils::CommandHandlerResult;

namespace {

CommandHandlerResult applyQuote(const QString& markdown, SourceSelection selection)
{
    const ParseResult parsed = TestUtils::parseDocument(markdown);
    return TestUtils::serializeCommandResult(
        QuoteCommandHandler::applyQuote(parsed.document, selection));
}

QString applyQuoteMarkdown(const QString& markdown, SourceSelection selection)
{
    return applyQuote(markdown, selection).markdown;
}

} // namespace

class TestQuoteCommandHandler : public QObject
{
    Q_OBJECT

private slots:
    void wrapsParagraphs()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("A\n\nB"), {0, 4});

        QCOMPARE(result.markdown, QStringLiteral("> A\n> \n> B"));
        QCOMPARE(result.cursor, 10);
    }

    void wrapsHeading()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("## A"), {3, 4});

        QCOMPARE(result.markdown, QStringLiteral("> ## A"));
        QCOMPARE(result.cursor, 6);
    }

    void wrapsUnorderedList()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("- A"), {0, 3});

        QCOMPARE(result.markdown, QStringLiteral("> - A"));
        QCOMPARE(result.cursor, 5);
    }

    void wrapsOrderedList()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("1. A"), {0, 4});

        QCOMPARE(result.markdown, QStringLiteral("> 1. A"));
        QCOMPARE(result.cursor, 6);
    }

    void wrapsTaskList()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("- [ ] A"), {0, 7});

        QCOMPARE(result.markdown, QStringLiteral("> - [ ] A"));
        QCOMPARE(result.cursor, 9);
    }

    void wrapsPartialListItem()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("- A\n\n- A"), {5, 6});

        QCOMPARE(result.markdown, QStringLiteral("- A\n\n> - A"));
        QCOMPARE(result.cursor, 10);
    }

    void unwrapsParagraph()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("> A"), {2, 3});

        QCOMPARE(result.markdown, QStringLiteral("A"));
        QCOMPARE(result.cursor, 1);
    }

    void unwrapsHeading()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("> ## A"), {5, 6});

        QCOMPARE(result.markdown, QStringLiteral("## A"));
        QCOMPARE(result.cursor, 4);
    }

    void unwrapsList()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("> - A"), {4, 5});

        QCOMPARE(result.markdown, QStringLiteral("- A"));
        QCOMPARE(result.cursor, 3);
    }

    void unwrapsPartialBlockQuote()
    {
        const CommandHandlerResult result = applyQuote(QStringLiteral("> A\n> \n> B"), {9, 10});

        QCOMPARE(result.markdown, QStringLiteral("> A\n\nB"));
        QCOMPARE(result.cursor, 6);
    }

    void rejectsUnsupportedBlock()
    {
        QCOMPARE(applyQuoteMarkdown(QStringLiteral("```cpp\nx\n```"), {7, 8}), QString());
    }
};

QTEST_MAIN(TestQuoteCommandHandler)
#include "test_quote_command_handler.moc"
