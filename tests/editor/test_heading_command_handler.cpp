#include "editor/HeadingCommandHandler.h"
#include "command_handler_test_utils.h"

#include <QTest>

using namespace Muffin;
using TestUtils::CommandHandlerResult;

namespace {

CommandHandlerResult applyHeading(const QString& markdown, SourceSelection selection, int level)
{
    const ParseResult parsed = TestUtils::parseDocument(markdown);
    return TestUtils::serializeCommandResult(
        HeadingCommandHandler::applyHeading(parsed.document, selection, level));
}

CommandHandlerResult applyParagraph(const QString& markdown, SourceSelection selection)
{
    const ParseResult parsed = TestUtils::parseDocument(markdown);
    return TestUtils::serializeCommandResult(
        HeadingCommandHandler::applyParagraph(parsed.document, selection));
}

} // namespace

class TestHeadingCommandHandler : public QObject
{
    Q_OBJECT

private slots:
    void convertsParagraphToHeading()
    {
        const CommandHandlerResult result = applyHeading(QStringLiteral("Title"), {0, 5}, 2);

        QCOMPARE(result.markdown, QStringLiteral("## Title"));
        QCOMPARE(result.cursor, 8);
    }

    void convertsRepeatedParagraphBySourceSelection()
    {
        const CommandHandlerResult result = applyHeading(QStringLiteral("Title\n\nTitle"), {7, 12}, 2);

        QCOMPARE(result.markdown, QStringLiteral("Title\n\n## Title"));
        QCOMPARE(result.cursor, 15);
    }

    void convertsMultipleBlocksToHeading()
    {
        const CommandHandlerResult result = applyHeading(QStringLiteral("A\n\nB"), {0, 4}, 2);

        QCOMPARE(result.markdown, QStringLiteral("## A\n\n## B"));
        QCOMPARE(result.cursor, 10);
    }

    void convertsHeadingToParagraph()
    {
        const CommandHandlerResult result = applyParagraph(QStringLiteral("## Title"), {3, 8});

        QCOMPARE(result.markdown, QStringLiteral("Title"));
        QCOMPARE(result.cursor, 5);
    }

    void convertsRepeatedHeadingsToParagraph()
    {
        const CommandHandlerResult result = applyParagraph(QStringLiteral("## Title\n\n## Title"), {0, 18});

        QCOMPARE(result.markdown, QStringLiteral("Title\n\nTitle"));
        QCOMPARE(result.cursor, 12);
    }

    void clampsHeadingLevel()
    {
        QCOMPARE(applyHeading(QStringLiteral("Title"), {0, 5}, 9).markdown,
                 QStringLiteral("###### Title"));
    }

    void rejectsUnsupportedBlock()
    {
        QCOMPARE(applyHeading(QStringLiteral("```cpp\nx\n```"), {7, 8}, 2).markdown, QString());
        QCOMPARE(applyParagraph(QStringLiteral("```cpp\nx\n```"), {7, 8}).markdown, QString());
    }
};

QTEST_MAIN(TestHeadingCommandHandler)
#include "test_heading_command_handler.moc"
