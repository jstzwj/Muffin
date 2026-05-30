#include "parser/BlockReparsePlanner.h"
#include "parser/CmarkParser.h"

#include <QTest>

using namespace Muffin;

namespace {

MarkdownDocument parseDocument(const QString& markdown)
{
    CmarkParser parser;
    return parser.parseDocument(markdown).document;
}

} // namespace

class TestBlockReparsePlanner : public QObject
{
    Q_OBJECT

private slots:
    void plansParagraphLocalReparseForInlineEdit()
    {
        const QString markdown = QStringLiteral("before\n\nhello world\n\nafter");
        const MarkdownDocument document = parseDocument(markdown);
        const int start = markdown.indexOf(QStringLiteral("world"));

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("W"));

        QVERIFY(range.canReparseLocally());
        QCOMPARE(range.requiresFullReparse, false);
        QCOMPARE(range.expandedSource.start, markdown.indexOf(QStringLiteral("hello")));
        QCOMPARE(range.expandedSource.end, markdown.indexOf(QStringLiteral("\n\nafter")));
        QCOMPARE(range.affectedBlockIds.size(), 1);
        QVERIFY(range.reason.isEmpty());
    }

    void plansSingleParagraphLocalReparseForInlineEdit()
    {
        const QString markdown = QStringLiteral("Hello");
        const MarkdownDocument document = parseDocument(markdown);

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {1, 2}, QStringLiteral("a"));

        QVERIFY2(range.canReparseLocally(), qPrintable(range.reason));
        QCOMPARE(range.expandedSource.start, 0);
        QCOMPARE(range.expandedSource.end, markdown.size());
        QCOMPARE(range.affectedBlockIds.size(), 1);
    }

    void plansHeadingLocalReparseForInlineEdit()
    {
        const QString markdown = QStringLiteral("# Hello\n\nbody");
        const MarkdownDocument document = parseDocument(markdown);
        const int start = markdown.indexOf(QStringLiteral("Hello"));

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("h"));

        QVERIFY(range.canReparseLocally());
        QCOMPARE(range.expandedSource.start, 0);
        QCOMPARE(range.expandedSource.end, markdown.indexOf(QStringLiteral("\n\nbody")));
    }

    void plansCodeBlockLocalReparseForContentEdit()
    {
        const QString markdown = QStringLiteral("```cpp\nint x;\n```\n");
        const MarkdownDocument document = parseDocument(markdown);
        const int start = markdown.indexOf(QStringLiteral("x"));

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("y"));

        QVERIFY2(range.canReparseLocally(), qPrintable(range.reason));
        QCOMPARE(range.expandedSource.start, 0);
        QCOMPARE(range.expandedSource.end, markdown.lastIndexOf(QChar('\n')));
    }

    void requiresFullReparseForCodeBlockFenceEdit()
    {
        const QString markdown = QStringLiteral("```cpp\nint x;\n```\n");
        const MarkdownDocument document = parseDocument(markdown);

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {1, 2}, QStringLiteral("~"));

        QVERIFY(range.requiresFullReparse);
        QCOMPARE(range.reason, QStringLiteral("Edit touches code block fence."));
    }

    void requiresFullReparseWhenReplacementContainsNewline()
    {
        const QString markdown = QStringLiteral("hello world");
        const MarkdownDocument document = parseDocument(markdown);

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {1, 2}, QStringLiteral("\n"));

        QVERIFY(range.requiresFullReparse);
        QCOMPARE(range.reason, QStringLiteral("Replacement changes block shape."));
    }

    void requiresFullReparseForListItemEdit()
    {
        const QString markdown = QStringLiteral("- item\n");
        const MarkdownDocument document = parseDocument(markdown);
        const int start = markdown.indexOf(QStringLiteral("item"));

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("I"));

        QVERIFY2(range.canReparseLocally(), qPrintable(range.reason));
        QCOMPARE(range.targetBlockType, MarkdownNodeType::ListItem);
    }

    void requiresFullReparseForTableEdit()
    {
        const QString markdown = QStringLiteral("| a |\n| - |\n| b |\n");
        const MarkdownDocument document = parseDocument(markdown);
        const int start = markdown.indexOf(QStringLiteral("b"));

        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("c"));

        QVERIFY(range.requiresFullReparse);
        QCOMPARE(range.reason, QStringLiteral("Block type requires full reparse."));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestBlockReparsePlanner test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_block_reparse_planner.moc"
