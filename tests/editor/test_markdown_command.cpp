#include "editor/MarkdownCommand.h"

#include <QTest>

using namespace Muffin;

class TestMarkdownCommand : public QObject
{
    Q_OBJECT

private slots:
    void wrapsSelectionWithBold()
    {
        MarkdownCommandResult result = MarkdownCommand::toggleBold(QStringLiteral("Hello"), {0, 5});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("**Hello**"));
        QCOMPARE(result.selection.start, 9);
        QCOMPARE(result.selection.end, 9);
    }

    void insertsPlaceholderWhenNoSelection()
    {
        MarkdownCommandResult result = MarkdownCommand::toggleItalic(QStringLiteral("Hi "), {3, 3});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hi *text*"));
    }

    void appliesHeadingToCurrentLine()
    {
        MarkdownCommandResult result = MarkdownCommand::applyHeading(QStringLiteral("One\nTwo"), {4, 4}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("One\n## Two"));
    }

    void appliesOrderedListAcrossSelectionLines()
    {
        MarkdownCommandResult result = MarkdownCommand::applyList(QStringLiteral("A\nB"), {0, 3}, MarkdownCommand::ListType::Ordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("1. A\n2. B"));
    }
};

QTEST_MAIN(TestMarkdownCommand)
#include "test_markdown_command.moc"
