#include "editor/ListCommandHandler.h"
#include "command_handler_test_utils.h"

#include <QTest>

using namespace Muffin;
using TestUtils::CommandHandlerResult;

namespace {

CommandHandlerResult applyList(const QString& markdown, SourceSelection selection, MarkdownCommand::ListType type)
{
    const ParseResult parsed = TestUtils::parseDocument(markdown);
    return TestUtils::serializeCommandResult(
        ListCommandHandler::applyList(parsed.document, selection, type));
}

QString applyListMarkdown(const QString& markdown, SourceSelection selection, MarkdownCommand::ListType type)
{
    return applyList(markdown, selection, type).markdown;
}

} // namespace

class TestListCommandHandler : public QObject
{
    Q_OBJECT

private slots:
    void wrapsParagraphsAsUnorderedList()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("A\n\nB"), {0, 4}, MarkdownCommand::ListType::Unordered);

        QCOMPARE(result.markdown, QStringLiteral("- A\n- B"));
        QCOMPARE(result.cursor, 7);
    }

    void wrapsParagraphsAsOrderedList()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("A\n\nB"), {0, 4}, MarkdownCommand::ListType::Ordered);

        QCOMPARE(result.markdown, QStringLiteral("1. A\n2. B"));
        QCOMPARE(result.cursor, 9);
    }

    void wrapsParagraphsAsTaskList()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("A\n\nB"), {0, 4}, MarkdownCommand::ListType::Task);

        QCOMPARE(result.markdown, QStringLiteral("- [ ] A\n- [ ] B"));
        QCOMPARE(result.cursor, 15);
    }

    void convertsHeadingToListParagraph()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("## A"), {3, 4}, MarkdownCommand::ListType::Unordered);

        QCOMPARE(result.markdown, QStringLiteral("- A"));
        QCOMPARE(result.cursor, 3);
    }

    void convertsUnorderedListToOrdered()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("- A\n- B"), {0, 7}, MarkdownCommand::ListType::Ordered);

        QCOMPARE(result.markdown, QStringLiteral("1. A\n2. B"));
        QCOMPARE(result.cursor, 9);
    }

    void convertsOrderedListToUnordered()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("1. A\n2. B"), {0, 9}, MarkdownCommand::ListType::Unordered);

        QCOMPARE(result.markdown, QStringLiteral("- A\n- B"));
        QCOMPARE(result.cursor, 7);
    }

    void convertsUnorderedListToTask()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("- A\n- B"), {0, 7}, MarkdownCommand::ListType::Task);

        QCOMPARE(result.markdown, QStringLiteral("- [ ] A\n- [ ] B"));
        QCOMPARE(result.cursor, 15);
    }

    void convertsTaskListToUnordered()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("- [ ] A\n- [ ] B"), {0, 15}, MarkdownCommand::ListType::Unordered);

        QCOMPARE(result.markdown, QStringLiteral("- A\n- B"));
        QCOMPARE(result.cursor, 7);
    }

    void appliesToRepeatedParagraphBySourceSelection()
    {
        const CommandHandlerResult result = applyList(QStringLiteral("Title\n\nTitle"), {7, 12}, MarkdownCommand::ListType::Unordered);

        QCOMPARE(result.markdown, QStringLiteral("Title\n\n- Title"));
        QCOMPARE(result.cursor, 14);
    }

    void rejectsUnsupportedBlock()
    {
        QCOMPARE(applyListMarkdown(QStringLiteral("> A"), {2, 3}, MarkdownCommand::ListType::Unordered), QString());
        QCOMPARE(applyListMarkdown(QStringLiteral("```cpp\nx\n```"), {7, 8}, MarkdownCommand::ListType::Unordered), QString());
    }
};

QTEST_MAIN(TestListCommandHandler)
#include "test_list_command_handler.moc"
