#include "editor/MarkdownPatch.h"

#include <QTest>

using namespace Muffin;

class TestMarkdownPatch : public QObject
{
    Q_OBJECT

private slots:
    void replacesPlainText()
    {
        RenderSourceMap map;
        map.addSpan({0, 11, {0, 11}, {1, 1, 1, 11}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello world"), map,
            {RenderedEditOperation::ReplaceSelection, 6, 11, QStringLiteral("Qt")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello Qt"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void preservesHeadingMarker()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {2, 7}, {1, 1, 1, 7}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("# Hello"), map,
            {RenderedEditOperation::ReplaceSelection, 0, 5, QStringLiteral("Title")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("# Title"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void enterSplitsParagraph()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("AB"), map,
            {RenderedEditOperation::Enter, 1, 1, QStringLiteral("\n")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("A\n\nB"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void enterSplitsHeadingWithSameLevel()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Heading, false, true});
        map.addSpan({0, 2, {3, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("## AB"), map,
            {RenderedEditOperation::Enter, 1, 1, QStringLiteral("\n")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("## A\n## B"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void enterSplitsParagraphUsingBlockIndex()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Text, true, false});
        QVector<MarkdownBlock> blocks{{{0, 2}, {0, 2}, {1, 1, 1, 2}, 0, 2, RenderSpan::Kind::Paragraph, true}};

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("AB"), map, blocks,
            {RenderedEditOperation::Enter, 1, 1, QStringLiteral("\n")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("A\n\nB"));
    }

    void deleteMergesNextParagraph()
    {
        RenderSourceMap map;
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({2, 3, {3, 4}, {3, 1, 3, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("A\n\nB"), map,
            {RenderedEditOperation::Delete, 1, 1, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("AB"));
        QCOMPARE(result.cursorSourceOffset, 1);
    }

    void deleteMergesNextHeadingWithoutMarker()
    {
        RenderSourceMap map;
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({2, 3, {3, 7}, {3, 1, 3, 4}, RenderSpan::Kind::Heading, false, true});
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("A\n\n## B"), map,
            {RenderedEditOperation::Delete, 1, 1, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("AB"));
    }

    void backspaceMergesCurrentParagraphIntoPrevious()
    {
        RenderSourceMap map;
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({2, 3, {3, 4}, {3, 1, 3, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({2, 3, {3, 4}, {3, 1, 3, 1}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("A\n\nB"), map,
            {RenderedEditOperation::Backspace, 2, 2, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("AB"));
    }

    void enterSplitsListItemWithMarker()
    {
        RenderSourceMap map;
        map.addSpan({2, 4, {2, 4}, {1, 3, 1, 4}, RenderSpan::Kind::Text, true, false, {2, 4}, RenderSpan::EditPolicy::LinearText});
        QVector<MarkdownBlock> blocks{{{0, 4}, {2, 4}, {1, 1, 1, 4}, 2, 4, RenderSpan::Kind::List, true}};

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("- AB"), map, blocks,
            {RenderedEditOperation::Enter, 3, 3, QStringLiteral("\n")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B"));
    }

    void backspaceMergesListItemsWithoutNextMarker()
    {
        RenderSourceMap map;
        map.addSpan({2, 3, {2, 3}, {1, 3, 1, 3}, RenderSpan::Kind::Text, true, false, {2, 3}, RenderSpan::EditPolicy::LinearText});
        map.addSpan({4, 5, {6, 7}, {2, 3, 2, 3}, RenderSpan::Kind::Text, true, false, {6, 7}, RenderSpan::EditPolicy::LinearText});
        QVector<MarkdownBlock> blocks{
            {{0, 3}, {2, 3}, {1, 1, 1, 3}, 2, 3, RenderSpan::Kind::List, true},
            {{4, 7}, {6, 7}, {2, 1, 2, 3}, 4, 5, RenderSpan::Kind::List, true}
        };

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("- A\n- B"), map, blocks,
            {RenderedEditOperation::Backspace, 4, 4, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB"));
    }

    void deleteBeforeTableDoesNothing()
    {
        RenderSourceMap map;
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Paragraph, false, true});
        map.addSpan({2, 10, {3, 16}, {3, 1, 4, 5}, RenderSpan::Kind::Table, false, true});
        map.addSpan({0, 1, {0, 1}, {1, 1, 1, 1}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("A\n\n| x |\n| - |"), map,
            {RenderedEditOperation::Delete, 1, 1, {}});

        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(result.text, QStringLiteral("A\n\n| x |\n| - |"));
    }

    void acceptsMultilineReplacement()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello"), map,
            {RenderedEditOperation::ReplaceSelection, 0, 5, QStringLiteral("Hi\r\nthere")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hi\nthere"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void replacesAcrossAdjacentPlainSpans()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {0, 2}, {1, 1, 1, 2}, RenderSpan::Kind::Text, true, false, {0, 2}, RenderSpan::EditPolicy::LinearText});
        map.addSpan({2, 5, {2, 5}, {1, 3, 1, 5}, RenderSpan::Kind::Text, true, false, {2, 5}, RenderSpan::EditPolicy::LinearText});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello"), map,
            {RenderedEditOperation::ReplaceSelection, 1, 4, QStringLiteral("i")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hio"));
        QCOMPARE(result.cursorSourceOffset, 2);
    }

    void enterWithSelectionSplitsAtSelectionStart()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Paragraph, false, true, {0, 5}, RenderSpan::EditPolicy::BlockContent});
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello"), map,
            {RenderedEditOperation::Enter, 1, 4, QStringLiteral("\n")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("H\n\no"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void editsFormattedInlineText()
    {
        RenderSourceMap map;
        map.addSpan({0, 2, {2, 4}, {1, 1, 1, 6}, RenderSpan::Kind::Text, true, false, {2, 4}, RenderSpan::EditPolicy::LinearText});
        map.addSpan({0, 2, {0, 6}, {1, 1, 1, 6}, RenderSpan::Kind::Strong, false, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("**hi**"), map,
            {RenderedEditOperation::ReplaceSelection, 0, 2, QStringLiteral("yo")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("**yo**"));
    }

    void editsInlineCodeContent()
    {
        RenderSourceMap map;
        map.addSpan({0, 4, {1, 5}, {1, 1, 1, 6}, RenderSpan::Kind::InlineCode, true, false, {1, 5}, RenderSpan::EditPolicy::LinearText});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("`code`"), map,
            {RenderedEditOperation::ReplaceSelection, 1, 3, QStringLiteral("ut")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("`cute`"));
    }

    void replacesFormulaAtom()
    {
        RenderSourceMap map;
        map.addSpan({1, 2, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::FormulaInline, true, false, {1, 6}, RenderSpan::EditPolicy::Atomic});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("a$x+y$b"), map,
            {RenderedEditOperation::ReplaceSelection, 1, 2, QStringLiteral("$z$")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("a$z$b"));
    }

    void rejectsUnsupportedSpan()
    {
        RenderSourceMap map;
        map.addSpan({0, 4, {2, 6}, {1, 1, 1, 8}, RenderSpan::Kind::Strong, false, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("**hi**"), map,
            {RenderedEditOperation::ReplaceSelection, 0, 2, QStringLiteral("yo")});

        QVERIFY(!result.ok);
        QCOMPARE(result.text, QStringLiteral("**hi**"));
    }
};

QTEST_MAIN(TestMarkdownPatch)
#include "test_markdown_patch.moc"
