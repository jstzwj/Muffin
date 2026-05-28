#include "editor/MarkdownEditEngine.h"

#include <QTest>

using namespace Muffin;

class TestMarkdownEditEngine : public QObject
{
    Q_OBJECT

private slots:
    void appliesRenderedEditThroughPatch()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(QStringLiteral("Hello"), map,
            {RenderedEditOperation::ReplaceSelection, 0, 5, QStringLiteral("Hi")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hi"));
    }

    void appliesInlineCommandToRenderedTarget()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyInlineCommandToRenderedTarget(
            QStringLiteral("Hello world"), {{1, 1, 1, 11}, QStringLiteral("world")}, &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello **world**"));
    }

    void appliesListCommandToRenderedTarget()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyListCommandToRenderedTarget(
            QStringLiteral("A\nB"), {{1, 1, 2, 1}, {}}, MarkdownCommand::ListType::Unordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("- A\n- B"));
    }

    void appliesHeadingCommandToRenderedTargetUsingBlockContent()
    {
        QVector<MarkdownBlock> blocks{{{0, 7}, {2, 7}, {1, 1, 1, 7}, 0, 5, RenderSpan::Kind::Heading, true}};

        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommandToRenderedTarget(
            QStringLiteral("# Title"), blocks, {{1, 1, 1, 7}, {}}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("## Title"));
    }

    void appliesHeadingCommandToSourceSelection()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommand(
            QStringLiteral("Title"), {0, 5}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("## Title"));
    }
};

QTEST_MAIN(TestMarkdownEditEngine)
#include "test_markdown_edit_engine.moc"
