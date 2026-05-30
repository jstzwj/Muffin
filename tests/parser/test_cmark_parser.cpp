#include <QTest>
#include "parser/CmarkParser.h"
#include "model/MarkdownDocument.h"
#include "model/MarkdownSerializer.h"

class TestCmarkParser : public QObject
{
    Q_OBJECT

private slots:
    void testInit()
    {
        QVERIFY(true);
    }

    void buildsMarkdownDocumentModel()
    {
        Muffin::CmarkParser parser;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("# Title\n\nParagraph with **bold**.\n\n- item"));

        QVERIFY(!result.ast.isNull());
        QVERIFY(!result.document.isEmpty());
        QVERIFY(result.document.rootId() != 0);

        const Muffin::MarkdownNode* root = result.document.nodeById(result.document.rootId());
        QVERIFY(root != nullptr);
        QCOMPARE(root->type, Muffin::MarkdownNodeType::Document);
        QVERIFY(root->children.size() >= 3);

        bool foundHeading = false;
        bool foundStrong = false;
        bool foundList = false;
        for (const Muffin::MarkdownNode& node : result.document.nodes()) {
            foundHeading = foundHeading || node.type == Muffin::MarkdownNodeType::Heading;
            foundStrong = foundStrong || node.type == Muffin::MarkdownNodeType::Strong;
            foundList = foundList || node.type == Muffin::MarkdownNodeType::List;
        }

        QVERIFY(foundHeading);
        QVERIFY(foundStrong);
        QVERIFY(foundList);
    }

    void indexesNodesBySourceOffset()
    {
        Muffin::CmarkParser parser;
        const QString markdown = QStringLiteral("# Title\n\nParagraph with **bold**.");
        Muffin::ParseResult result = parser.parseDocument(markdown);

        const int boldOffset = markdown.indexOf(QStringLiteral("bold"));
        const Muffin::MarkdownNode* inlineNode = result.document.nodeAtSourceOffset(boldOffset);
        QVERIFY(inlineNode != nullptr);
        QCOMPARE(inlineNode->type, Muffin::MarkdownNodeType::Text);

        const Muffin::MarkdownNode* blockNode = result.document.blockAtSourceOffset(boldOffset);
        QVERIFY(blockNode != nullptr);
        QCOMPARE(blockNode->type, Muffin::MarkdownNodeType::Paragraph);

        QVector<Muffin::MarkdownNodeId> ids = result.document.nodeIdsInSourceSpan({boldOffset, boldOffset + 4});
        QVERIFY(!ids.isEmpty());
    }

    void reusesNodeIdsForUnchangedNodes()
    {
        Muffin::CmarkParser parser;
        Muffin::ParseResult first = parser.parseDocument(QStringLiteral("# Title\n\nParagraph one\n\nParagraph two"));

        Muffin::MarkdownNodeId firstHeadingId = 0;
        for (const Muffin::MarkdownNode& node : first.document.nodes()) {
            if (node.type == Muffin::MarkdownNodeType::Heading) {
                firstHeadingId = node.id;
                break;
            }
        }
        QVERIFY(firstHeadingId != 0);

        Muffin::ParseResult second = parser.parseDocument(QStringLiteral("# Title\n\nParagraph changed\n\nParagraph two"), first.document);

        bool reusedHeading = false;
        for (const Muffin::MarkdownNode& node : second.document.nodes()) {
            if (node.type == Muffin::MarkdownNodeType::Heading && node.id == firstHeadingId) {
                reusedHeading = true;
                break;
            }
        }

        QVERIFY(reusedHeading);
    }

    void serializesBasicMarkdown()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("# Title\n\nParagraph with **bold** and `code`."));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("# Title\n\nParagraph with **bold** and `code`."));
    }

    void serializesListsAndCodeBlocks()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("- item\n- **bold**\n\n```cpp\nint x;\n```"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("- item\n- **bold**\n\n```cpp\nint x;\n```"));
    }

    void serializesEmptyCodeBlock()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        const QString markdown = QStringLiteral("```\n```");
        Muffin::ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(serializer.serializeDocument(result.document), markdown);
    }

    void mapsFencedCodeBlockContentSpan()
    {
        Muffin::CmarkParser parser;
        const QString markdown = QStringLiteral("before\n\n```cpp\nint x;\n```\n\nafter");
        Muffin::ParseResult result = parser.parseDocument(markdown);

        const Muffin::MarkdownNode* codeBlock = nullptr;
        for (const Muffin::MarkdownNode& node : result.document.nodes()) {
            if (node.type == Muffin::MarkdownNodeType::CodeBlock) {
                codeBlock = &node;
                break;
            }
        }

        QVERIFY(codeBlock != nullptr);
        const int contentStart = markdown.indexOf(QStringLiteral("int x;"));
        const int contentEnd = markdown.indexOf(QStringLiteral("\n```"), contentStart);
        QCOMPARE(codeBlock->source.start, markdown.indexOf(QStringLiteral("```cpp")));
        QCOMPARE(codeBlock->source.end, markdown.indexOf(QStringLiteral("\n\nafter")));
        QCOMPARE(codeBlock->content.start, contentStart);
        QCOMPARE(codeBlock->content.end, contentEnd);
        QCOMPARE(markdown.mid(codeBlock->content.start, codeBlock->content.end - codeBlock->content.start),
                 QStringLiteral("int x;"));
    }

    void mapsEmptyFencedCodeBlockContentSpan()
    {
        Muffin::CmarkParser parser;
        const QString markdown = QStringLiteral("```\n```");
        Muffin::ParseResult result = parser.parseDocument(markdown);

        const Muffin::MarkdownNode* codeBlock = nullptr;
        for (const Muffin::MarkdownNode& node : result.document.nodes()) {
            if (node.type == Muffin::MarkdownNodeType::CodeBlock) {
                codeBlock = &node;
                break;
            }
        }

        QVERIFY(codeBlock != nullptr);
        const int emptyContentOffset = markdown.indexOf(QChar('\n')) + 1;
        QCOMPARE(codeBlock->content.start, emptyContentOffset);
        QCOMPARE(codeBlock->content.end, emptyContentOffset);
    }

    void serializesTaskLists()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("- [ ] todo\n- [x] done"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("- [ ] todo\n- [x] done"));
    }

    void serializesNestedLists()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("- A\n  - B"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("- A\n  - B"));
    }

    void serializesLooseListItemParagraphs()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("- A\n\n  B"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("- A\n\n  B"));
    }

    void serializesBlockquoteAndTable()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("> quoted\n\n| A | B |\n| - | - |\n| 1 | 2 |"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("> quoted\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"));
    }

    void serializesBlockquoteMultipleParagraphs()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("> Hello\n>\n> world"));

        QCOMPARE(serializer.serializeDocument(result.document),
                 QStringLiteral("> Hello\n> \n> world"));
    }

    void buildsFormulaNodesInMarkdownDocumentModel()
    {
        Muffin::CmarkParser parser;
        Muffin::ParseResult result = parser.parseDocument(QStringLiteral("a $x$ b $y$\n\n$$\nz\n$$"));

        int inlineFormulaCount = 0;
        int blockFormulaCount = 0;
        for (const Muffin::MarkdownNode& node : result.document.nodes()) {
            if (node.type == Muffin::MarkdownNodeType::FormulaInline) {
                ++inlineFormulaCount;
                QVERIFY(node.id != 0);
                QVERIFY(node.source.isValid());
                QVERIFY(node.content.isValid());
                QVERIFY(!node.literal.isEmpty());
            }
            if (node.type == Muffin::MarkdownNodeType::FormulaBlock) {
                ++blockFormulaCount;
                QVERIFY(node.id != 0);
                QVERIFY(node.source.isValid());
                QVERIFY(node.content.isValid());
                QCOMPARE(node.literal, QStringLiteral("z"));
            }
        }

        QCOMPARE(inlineFormulaCount, 2);
        QCOMPARE(blockFormulaCount, 1);
    }

    void serializesFormulaNodes()
    {
        Muffin::CmarkParser parser;
        Muffin::MarkdownSerializer serializer;
        const QString markdown = QStringLiteral("a $x$ b\n\n$$\nz\n$$");
        Muffin::ParseResult result = parser.parseDocument(markdown);

        QCOMPARE(serializer.serializeDocument(result.document), markdown);
    }
};

QTEST_MAIN(TestCmarkParser)
#include "test_cmark_parser.moc"
