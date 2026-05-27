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
        map.addSpan({0, 11, {0, 11}, {1, 1, 1, 11}, RenderSpan::Kind::Text, true});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello world"), map, {6, 11, QStringLiteral("Qt")});

        QVERIFY(result.ok);
        QCOMPARE(result.text, QStringLiteral("Hello Qt"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void preservesHeadingMarker()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {2, 7}, {1, 1, 1, 7}, RenderSpan::Kind::Text, true});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("# Hello"), map, {0, 5, QStringLiteral("Title")});

        QVERIFY(result.ok);
        QCOMPARE(result.text, QStringLiteral("# Title"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void rejectsMultilineReplacement()
    {
        RenderSourceMap map;
        map.addSpan({0, 5, {0, 5}, {1, 1, 1, 5}, RenderSpan::Kind::Text, true});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("Hello"), map, {0, 5, QStringLiteral("Hi\nthere")});

        QVERIFY(!result.ok);
        QCOMPARE(result.text, QStringLiteral("Hello"));
    }

    void rejectsUnsupportedSpan()
    {
        RenderSourceMap map;
        map.addSpan({0, 4, {2, 6}, {1, 1, 1, 8}, RenderSpan::Kind::Strong, false});

        PatchResult result = MarkdownPatch::applyRenderedEdit(QStringLiteral("**hi**"), map, {0, 2, QStringLiteral("yo")});

        QVERIFY(!result.ok);
        QCOMPARE(result.text, QStringLiteral("**hi**"));
    }
};

QTEST_MAIN(TestMarkdownPatch)
#include "test_markdown_patch.moc"
