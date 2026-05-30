#include "parser/BlockReparsePlanner.h"
#include "parser/BlockReparser.h"
#include "parser/CmarkParser.h"

#include <QTest>

using namespace Muffin;

namespace {

MarkdownDocument parseDocument(const QString& markdown)
{
    CmarkParser parser;
    return parser.parseDocument(markdown).document;
}

const MarkdownNode* firstTopLevelBlock(const MarkdownDocument& document)
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    if (!root || root->children.isEmpty()) {
        return nullptr;
    }
    return document.nodeById(root->children.first());
}

} // namespace

class TestBlockReparser : public QObject
{
    Q_OBJECT

private slots:
    void reparsesParagraphShadowBlock()
    {
        const QString before = QStringLiteral("before\n\nhello world\n\nafter");
        const QString after = QStringLiteral("before\n\nhello World\n\nafter");
        const MarkdownDocument document = parseDocument(before);
        const int start = before.indexOf(QStringLiteral("world"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("W"));

        const BlockReparseResult result = BlockReparser::reparse(document, range, after);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.attempted);
        QCOMPARE(result.localMarkdown, QStringLiteral("hello World"));
        const MarkdownNode* block = firstTopLevelBlock(result.localDocument);
        QVERIFY(block);
        QCOMPARE(block->type, MarkdownNodeType::Paragraph);
        QCOMPARE(block->source.start, 0);
        QCOMPARE(block->source.end, result.localMarkdown.size());
    }

    void reparsesHeadingShadowBlock()
    {
        const QString before = QStringLiteral("# Hello\n\nbody");
        const QString after = QStringLiteral("# hello\n\nbody");
        const MarkdownDocument document = parseDocument(before);
        const int start = before.indexOf(QStringLiteral("Hello"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("h"));

        const BlockReparseResult result = BlockReparser::reparse(document, range, after);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.localMarkdown, QStringLiteral("# hello"));
        const MarkdownNode* block = firstTopLevelBlock(result.localDocument);
        QVERIFY(block);
        QCOMPARE(block->type, MarkdownNodeType::Heading);
        QCOMPARE(block->headingLevel, 1);
    }

    void reparsesCodeBlockShadowBlock()
    {
        const QString before = QStringLiteral("```cpp\nint x;\n```");
        const QString after = QStringLiteral("```cpp\nint y;\n```");
        const MarkdownDocument document = parseDocument(before);
        const int start = before.indexOf(QStringLiteral("x"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("y"));

        const BlockReparseResult result = BlockReparser::reparse(document, range, after);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.localMarkdown, after);
        const MarkdownNode* block = firstTopLevelBlock(result.localDocument);
        QVERIFY(block);
        QCOMPARE(block->type, MarkdownNodeType::CodeBlock);
        QCOMPARE(block->fenceInfo, QStringLiteral("cpp"));
        QCOMPARE(block->literal, QStringLiteral("int y;\n"));
    }

    void reparsesSimpleListItem()
    {
        const QString before = QStringLiteral("- item\n");
        const QString after = QStringLiteral("- Item\n");
        const MarkdownDocument document = parseDocument(before);
        const int start = before.indexOf(QStringLiteral("item"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("I"));

        const BlockReparseResult result = BlockReparser::reparse(document, range, after);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.attempted);
    }

    void rejectsRangePastMarkdownEndWithoutAttemptingParse()
    {
        const QString markdown = QStringLiteral("Hello");
        const MarkdownDocument document = parseDocument(markdown);
        BlockParseRange range = BlockReparsePlanner::planForEdit(document, {1, 2}, QStringLiteral("a"));
        QVERIFY(range.canReparseLocally());
        range.expandedSource.end = markdown.size() + 1;

        const BlockReparseResult result = BlockReparser::reparse(document, range, QStringLiteral("Hallo"));

        QVERIFY(!result.ok);
        QVERIFY(!result.attempted);
        QVERIFY(result.errors.contains(QStringLiteral("Expanded source range exceeds markdown length.")));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestBlockReparser test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_block_reparser.moc"
