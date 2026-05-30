#include "model/BlockDocumentMerger.h"
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

const MarkdownNode* firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return &node;
        }
    }
    return nullptr;
}

QVector<MarkdownNodeId> rootChildren(const MarkdownDocument& document)
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    return root ? root->children : QVector<MarkdownNodeId>{};
}

} // namespace

class TestBlockDocumentMerger : public QObject
{
    Q_OBJECT

private slots:
    void mergesParagraphBlockAndRebasesSourceSpans()
    {
        const QString before = QStringLiteral("before\n\nhello world\n\nafter");
        const QString after = QStringLiteral("before\n\nhello World\n\nafter");
        const MarkdownDocument document = parseDocument(before);
        const QVector<MarkdownNodeId> beforeChildren = rootChildren(document);
        const int start = before.indexOf(QStringLiteral("world"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("W"));
        QVERIFY(range.canReparseLocally());
        const MarkdownNodeId previousParagraphId = range.affectedBlockIds.first();
        const BlockReparseResult reparse = BlockReparser::reparse(document, range, after);
        QVERIFY2(reparse.ok, qPrintable(reparse.errors.join(QStringLiteral("; "))));

        const BlockMergeResult merge = BlockDocumentMerger::mergeReparsedBlock(document, reparse);

        QVERIFY2(merge.ok, qPrintable(merge.errors.join(QStringLiteral("; "))));
        QCOMPARE(merge.replacedNodeId, previousParagraphId);
        QCOMPARE(merge.document.source(), after);
        const MarkdownNode* mergedParagraph = merge.document.nodeById(previousParagraphId);
        QVERIFY(mergedParagraph);
        QCOMPARE(mergedParagraph->type, MarkdownNodeType::Paragraph);
        QCOMPARE(mergedParagraph->source.start, after.indexOf(QStringLiteral("hello")));
        QCOMPARE(mergedParagraph->source.end, after.indexOf(QStringLiteral("\n\nafter")));
        QVERIFY(!mergedParagraph->children.isEmpty());
        const MarkdownNode* text = merge.document.nodeById(mergedParagraph->children.first());
        QVERIFY(text);
        QCOMPARE(text->literal, QStringLiteral("hello World"));
        QCOMPARE(text->source.start, mergedParagraph->source.start);
        QCOMPARE(text->source.end, mergedParagraph->source.end);
        QCOMPARE(rootChildren(merge.document), beforeChildren);
    }

    void mergesHeadingBlockAndKeepsHeadingNodeId()
    {
        const QString before = QStringLiteral("# Hello\n\nbody");
        const QString after = QStringLiteral("# hello\n\nbody");
        const MarkdownDocument document = parseDocument(before);
        const MarkdownNode* previousHeading = firstNodeOfType(document, MarkdownNodeType::Heading);
        QVERIFY(previousHeading);
        const MarkdownNodeId previousHeadingId = previousHeading->id;
        const int start = before.indexOf(QStringLiteral("Hello"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("h"));
        const BlockReparseResult reparse = BlockReparser::reparse(document, range, after);
        QVERIFY2(reparse.ok, qPrintable(reparse.errors.join(QStringLiteral("; "))));

        const BlockMergeResult merge = BlockDocumentMerger::mergeReparsedBlock(document, reparse);

        QVERIFY2(merge.ok, qPrintable(merge.errors.join(QStringLiteral("; "))));
        const MarkdownNode* heading = merge.document.nodeById(previousHeadingId);
        QVERIFY(heading);
        QCOMPARE(heading->type, MarkdownNodeType::Heading);
        QCOMPARE(heading->headingLevel, 1);
        QCOMPARE(heading->source.start, 0);
        QCOMPARE(heading->source.end, after.indexOf(QStringLiteral("\n\nbody")));
        const MarkdownNode* text = firstNodeOfType(merge.document, MarkdownNodeType::Text);
        QVERIFY(text);
        QCOMPARE(text->literal, QStringLiteral("hello"));
        QCOMPARE(text->source.start, after.indexOf(QStringLiteral("hello")));
        QCOMPARE(text->source.end, after.indexOf(QStringLiteral("\n\nbody")));
    }

    void mergesCodeBlockAndKeepsCodeBlockNodeId()
    {
        const QString before = QStringLiteral("before\n\n```cpp\nint x;\n```\n\nafter");
        const QString after = QStringLiteral("before\n\n```cpp\nint y;\n```\n\nafter");
        const MarkdownDocument document = parseDocument(before);
        const int start = before.indexOf(QStringLiteral("x"));
        const BlockParseRange range = BlockReparsePlanner::planForEdit(document, {start, start + 1}, QStringLiteral("y"));
        QVERIFY2(range.canReparseLocally(), qPrintable(range.reason));
        const MarkdownNodeId codeId = range.affectedBlockIds.first();
        const BlockReparseResult reparse = BlockReparser::reparse(document, range, after);
        QVERIFY2(reparse.ok, qPrintable(reparse.errors.join(QStringLiteral("; "))));

        const BlockMergeResult merge = BlockDocumentMerger::mergeReparsedBlock(document, reparse);

        QVERIFY2(merge.ok, qPrintable(merge.errors.join(QStringLiteral("; "))));
        QCOMPARE(merge.replacedNodeId, codeId);
        const MarkdownNode* code = merge.document.nodeById(codeId);
        QVERIFY(code);
        QCOMPARE(code->type, MarkdownNodeType::CodeBlock);
        QCOMPARE(code->fenceInfo, QStringLiteral("cpp"));
        QCOMPARE(code->literal, QStringLiteral("int y;\n"));
        QCOMPARE(code->source.start, after.indexOf(QStringLiteral("```cpp")));
        QCOMPARE(code->source.end, after.indexOf(QStringLiteral("\n\nafter")));
    }

    void rejectsFailedReparseResult()
    {
        const QString markdown = QStringLiteral("Hello");
        const MarkdownDocument document = parseDocument(markdown);
        BlockReparseResult reparse;

        const BlockMergeResult merge = BlockDocumentMerger::mergeReparsedBlock(document, reparse);

        QVERIFY(!merge.ok);
        QVERIFY(merge.errors.contains(QStringLiteral("Block reparse result is not ok.")));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestBlockDocumentMerger test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_block_document_merger.moc"
