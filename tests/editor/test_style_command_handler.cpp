#include "editor/StyleCommandHandler.h"
#include "command_handler_test_utils.h"

#include <QTest>

using namespace Muffin;
using TestUtils::CommandHandlerResult;

namespace {

CommandHandlerResult applyStyle(const QString& markdown, SourceSelection selection, StyleCommandHandler::InlineStyle style)
{
    const ParseResult parsed = TestUtils::parseDocument(markdown);
    return TestUtils::serializeCommandResult(
        StyleCommandHandler::toggleInlineStyleWithSelection(parsed.document, selection, style));
}

} // namespace

class TestStyleCommandHandler : public QObject
{
    Q_OBJECT

private slots:
    void wrapsPlainTextSelectionAsStrong()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello world"), {6, 11}, StyleCommandHandler::InlineStyle::Strong);

        QCOMPARE(result.markdown, QStringLiteral("Hello **world**"));
        QCOMPARE(result.cursor, 13);
    }

    void wrapsPlainTextSelectionAsEmphasis()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello world"), {6, 11}, StyleCommandHandler::InlineStyle::Emphasis);

        QCOMPARE(result.markdown, QStringLiteral("Hello *world*"));
        QCOMPARE(result.cursor, 12);
    }

    void wrapsPlainTextSelectionAsInlineCode()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello world"), {6, 11}, StyleCommandHandler::InlineStyle::InlineCode);

        QCOMPARE(result.markdown, QStringLiteral("Hello `world`"));
        QCOMPARE(result.cursor, 12);
    }

    void wrapsPlainTextSelectionAsStrikethrough()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello world"), {6, 11}, StyleCommandHandler::InlineStyle::Strikethrough);

        QCOMPARE(result.markdown, QStringLiteral("Hello ~~world~~"));
        QCOMPARE(result.cursor, 13);
    }

    void unwrapsStrongWhenSelectionCoversStyledText()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello **world**"), {8, 13}, StyleCommandHandler::InlineStyle::Strong);

        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
        QCOMPARE(result.cursor, 11);
    }

    void unwrapsEmphasisWhenSelectionCoversStyledText()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello *world*"), {7, 12}, StyleCommandHandler::InlineStyle::Emphasis);

        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
        QCOMPARE(result.cursor, 11);
    }

    void unwrapsInlineCodeWhenSelectionCoversCodeText()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello `world`"), {7, 12}, StyleCommandHandler::InlineStyle::InlineCode);

        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
        QCOMPARE(result.cursor, 11);
    }

    void unwrapsStrikethroughWhenSelectionCoversStyledText()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello ~~world~~"), {8, 13}, StyleCommandHandler::InlineStyle::Strikethrough);

        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
        QCOMPARE(result.cursor, 11);
    }

    void unwrapsPartialStrongSelectionInMiddle()
    {
        const CommandHandlerResult result = applyStyle(QStringLiteral("Hello **world**"), {9, 11}, StyleCommandHandler::InlineStyle::Strong);

        QCOMPARE(result.markdown, QStringLiteral("Hello **w**or**ld**"));
        QCOMPARE(result.cursor, 13);
    }

    void unwrapsPartialStrongSelectionAtStart()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello **world**"), {8, 10}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("Hello wo**rld**"));
    }

    void unwrapsPartialStrongSelectionAtEnd()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello **world**"), {11, 13}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("Hello **wor**ld"));
    }

    void unwrapsPartialEmphasisSelection()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello *world*"), {8, 10}, StyleCommandHandler::InlineStyle::Emphasis).markdown,
                 QStringLiteral("Hello *w*or*ld*"));
    }

    void unwrapsPartialStrikethroughSelection()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello ~~world~~"), {9, 11}, StyleCommandHandler::InlineStyle::Strikethrough).markdown,
                 QStringLiteral("Hello ~~w~~or~~ld~~"));
    }

    void doesNotUnwrapDifferentStyle()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello **world**"), {8, 13}, StyleCommandHandler::InlineStyle::Emphasis).markdown,
                 QString());
    }

    void rejectsEmptySelection()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello world"), {6, 6}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QString());
    }

    void wrapsSelectionAcrossInlineSiblings()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello *world* again"), {3, 15}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("Hel**lo *world* a**gain"));
    }

    void wrapsSelectionAcrossFormattedInlineSiblings()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello *beautiful* world"), {3, 20}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("Hel**lo *beautiful* wo**rld"));
    }

    void extendsStrongAcrossSelectionEnteringStrong()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello **world**"), {3, 10}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("Hel**lo world**"));
    }

    void extendsStrongAcrossSelectionLeavingStrong()
    {
        QCOMPARE(applyStyle(QStringLiteral("**Hello** world"), {5, 12}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("**Hello wo**rld"));
    }

    void extendsStrongAcrossCompleteStrongWithTextOnBothSides()
    {
        QCOMPARE(applyStyle(QStringLiteral("A **bold** C"), {0, 12}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QStringLiteral("**A bold C**"));
    }

    void extendsStrikethroughAcrossSelectionEnteringStrikethrough()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello ~~world~~"), {3, 10}, StyleCommandHandler::InlineStyle::Strikethrough).markdown,
                 QStringLiteral("Hel~~lo world~~"));
    }

    void rejectsSelectionAcrossBlocks()
    {
        QCOMPARE(applyStyle(QStringLiteral("Hello\n\nworld"), {3, 10}, StyleCommandHandler::InlineStyle::Strong).markdown,
                 QString());
    }
};

QTEST_MAIN(TestStyleCommandHandler)
#include "test_style_command_handler.moc"
