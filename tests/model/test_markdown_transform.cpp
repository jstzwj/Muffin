#include "model/MarkdownSerializer.h"
#include "model/MarkdownTransform.h"
#include "parser/CmarkParser.h"

#include <QTest>

using namespace Muffin;

namespace {

MarkdownNodeId firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return node.id;
        }
    }
    return 0;
}

MarkdownNodeId nthNodeOfType(const MarkdownDocument& document, MarkdownNodeType type, int index)
{
    int seen = 0;
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type != type) {
            continue;
        }
        if (seen == index) {
            return node.id;
        }
        ++seen;
    }
    return 0;
}

MarkdownNodeId textNodeWithLiteral(const MarkdownDocument& document, const QString& literal)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::Text && node.literal == literal) {
            return node.id;
        }
    }
    return 0;
}

MarkdownNodeId nthTextNodeWithLiteral(const MarkdownDocument& document, const QString& literal, int index)
{
    int seen = 0;
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type != MarkdownNodeType::Text || node.literal != literal) {
            continue;
        }
        if (seen == index) {
            return node.id;
        }
        ++seen;
    }
    return 0;
}

QString serialize(const MarkdownDocument& document)
{
    MarkdownSerializer serializer;
    return serializer.serializeDocument(document);
}

} // namespace

class TestMarkdownTransform : public QObject
{
    Q_OBJECT

private slots:
    void replacesNodeLiteral()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello world"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::replaceNodeLiteral(parsed.document, textId, QStringLiteral("Hello Qt"));

        QCOMPARE(serialize(transformed), QStringLiteral("Hello Qt"));
        QCOMPARE(serialize(parsed.document), QStringLiteral("Hello world"));
    }

    void replacesFormulaNode()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("a$x+y$b"));
        const MarkdownNodeId formulaId = firstNodeOfType(parsed.document, MarkdownNodeType::FormulaInline);
        QVERIFY(formulaId != 0);

        MarkdownDocument transformed = MarkdownTransform::replaceFormulaNode(parsed.document, formulaId, QStringLiteral("$z$"));

        QCOMPARE(serialize(transformed), QStringLiteral("a$z$b"));
        QCOMPARE(serialize(parsed.document), QStringLiteral("a$x+y$b"));
    }

    void splitsTextNodeIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello world"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoParagraphs(parsed.document, textId, 5);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello\n\n world"));
    }

    void splitsBlockquoteTextNodeIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("> Hello world"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoParagraphs(parsed.document, textId, 5);

        QCOMPARE(serialize(transformed), QStringLiteral("> Hello\n> \n>  world"));
    }

    void mergesAdjacentParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello\n\nworld"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("Helloworld"));
    }

    void mergesAdjacentBlockquoteParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("> Hello\n>\n> world"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("> Helloworld"));
    }

    void splitsListItemParagraphIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- Alpha\n\n  Beta"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("Alpha"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("- Al\n\n  pha\n\n  Beta"));
    }

    void mergesListItemParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- Alpha\n\n  Beta"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("- AlphaBeta"));
    }

    void splitsParagraphAtInlineChildBoundary()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **bold**"));
        const MarkdownNodeId paragraphId = firstNodeOfType(parsed.document, MarkdownNodeType::Paragraph);
        QVERIFY(paragraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitParagraphAtChildBoundary(parsed.document, paragraphId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello \n\n**bold**"));
    }

    void mergesParagraphsWithInlineChildBoundary()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello \n\n**bold**"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello **bold**"));
    }

    void splitsStrongTextIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("**bold**"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("bold"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitFormattedTextIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("**bo**\n\n**ld**"));
    }

    void splitsStrongTextAndKeepsParagraphSiblings()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **bold** tail"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("bold"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitFormattedTextIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello **bo**\n\n**ld** tail"));
    }

    void smartSplitsPlainTextInsideMultiInlineParagraph()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **bold** tail"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("Hello "));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitInlineNodeIntoParagraphs(parsed.document, textId, 3);

        QCOMPARE(serialize(transformed), QStringLiteral("Hel\n\nlo **bold** tail"));
    }

    void smartSplitsFormattedTextInsideMultiInlineParagraph()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **bold** tail"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("bold"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitInlineNodeIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello **bo**\n\n**ld** tail"));
    }

    void smartSplitsInlineCodeInsideMultiInlineParagraph()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello `code` tail"));
        const MarkdownNodeId codeId = firstNodeOfType(parsed.document, MarkdownNodeType::InlineCode);
        QVERIFY(codeId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitInlineNodeIntoParagraphs(parsed.document, codeId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello `co`\n\n`de` tail"));
    }

    void splitsEmphasisTextIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("*bold*"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("bold"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitFormattedTextIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("*bo*\n\n*ld*"));
    }

    void mergesAdjacentStrongParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("**bo**\n\n**ld**"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("**bold**"));
    }

    void mergesAdjacentEmphasisParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("*bo*\n\n*ld*"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("*bold*"));
    }

    void mergesAdjacentStrongParagraphsAndKeepsSiblings()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **bo**\n\n**ld** tail"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello **bold** tail"));
    }

    void splitsLinkTextIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("[bold](url)"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("bold"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitFormattedTextIntoParagraphs(parsed.document, textId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("[bo](url)\n\n[ld](url)"));
    }

    void mergesAdjacentLinkParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("[bo](url)\n\n[ld](url)"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("[bold](url)"));
    }

    void keepsAdjacentLinksWithDifferentTargetsSeparate()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("[bo](one)\n\n[ld](two)"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("[bo](one)[ld](two)"));
    }

    void splitsInlineCodeIntoParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("`bold`"));
        const MarkdownNodeId codeId = firstNodeOfType(parsed.document, MarkdownNodeType::InlineCode);
        QVERIFY(codeId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitInlineCodeIntoParagraphs(parsed.document, codeId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("`bo`\n\n`ld`"));
    }

    void mergesAdjacentInlineCodeParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("`bo`\n\n`ld`"));
        const MarkdownNodeId firstParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 0);
        const MarkdownNodeId secondParagraphId = nthNodeOfType(parsed.document, MarkdownNodeType::Paragraph, 1);
        QVERIFY(firstParagraphId != 0);
        QVERIFY(secondParagraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeParagraphs(parsed.document, firstParagraphId, secondParagraphId);

        QCOMPARE(serialize(transformed), QStringLiteral("`bold`"));
    }

    void leavesBacktickInlineCodeSplitUnchanged()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("``bo`ld``"));
        const MarkdownNodeId codeId = firstNodeOfType(parsed.document, MarkdownNodeType::InlineCode);
        QVERIFY(codeId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitInlineCodeIntoParagraphs(parsed.document, codeId, 2);

        QCOMPARE(serialize(transformed), QStringLiteral("`bo`ld`"));
    }

    void splitsTextNodeIntoListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- AB"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoListItems(parsed.document, textId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B"));
    }

    void splitsTextNodeIntoOrderedListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("1. AB"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoListItems(parsed.document, textId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("1. A\n2. B"));
    }

    void splitsTextNodeIntoTaskListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- [ ] AB"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoListItems(parsed.document, textId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("- [ ] A\n- [ ] B"));
    }

    void splitsCheckedTaskListItemIntoUncheckedNextItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- [x] AB"));
        const MarkdownNodeId textId = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoListItems(parsed.document, textId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("- [x] A\n- [ ] B"));
    }

    void splitsListItemAndMovesNestedListToNextItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- AB\n  - C"));
        const MarkdownNodeId textId = textNodeWithLiteral(parsed.document, QStringLiteral("AB"));
        QVERIFY(textId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoListItems(parsed.document, textId, 1);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B\n  - C"));
    }

    void mergesAdjacentListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeListItems(parsed.document, firstItemId, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- AB"));
    }

    void mergesListItemsAndKeepsNestedList()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n  - C"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeListItems(parsed.document, firstItemId, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- AB\n  - C"));
    }

    void mergesListItemsAndKeepsLooseParagraphs()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n\n  C"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeListItems(parsed.document, firstItemId, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- AB\n\n  C"));
    }

    void mergesAdjacentOrderedListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("1. A\n2. B"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeListItems(parsed.document, firstItemId, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("1. AB"));
    }

    void mergesAdjacentTaskListItems()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- [ ] A\n- [ ] B"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::mergeListItems(parsed.document, firstItemId, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- [ ] AB"));
    }

    void removesTrailingEmptyListItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- "));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::removeEmptyListItem(parsed.document, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A"));
    }

    void removesOnlyNestedEmptyListItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - "));
        const MarkdownNodeId nestedEmptyItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(nestedEmptyItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::removeEmptyListItem(parsed.document, nestedEmptyItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n  - B"));
    }

    void removesNestedListWhenLastNestedItemIsRemoved()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - "));
        const MarkdownNodeId nestedTextId = textNodeWithLiteral(parsed.document, QStringLiteral("B"));
        const MarkdownNodeId nestedItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        const MarkdownNodeId nestedEmptyItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(nestedTextId != 0);
        QVERIFY(nestedItemId != 0);
        QVERIFY(nestedEmptyItemId != 0);

        MarkdownDocument withoutEmptyItem = MarkdownTransform::removeEmptyListItem(parsed.document, nestedEmptyItemId);
        MarkdownDocument emptiedNestedItem = MarkdownTransform::replaceNodeLiteral(withoutEmptyItem, nestedTextId, QString());
        MarkdownDocument transformed = MarkdownTransform::removeEmptyListItem(emptiedNestedItem, nestedItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A"));
    }

    void removesOnlyEmptyListItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- "));
        const MarkdownNodeId itemId = firstNodeOfType(parsed.document, MarkdownNodeType::ListItem);
        QVERIFY(itemId != 0);

        MarkdownDocument transformed = MarkdownTransform::removeEmptyListItem(parsed.document, itemId);

        QCOMPARE(serialize(transformed), QString());
    }

    void removesTrailingEmptyOrderedListItem()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("1. A\n2. "));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::removeEmptyListItem(parsed.document, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("1. A"));
    }

    void demotesListItemUnderPreviousSibling()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItem(parsed.document, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n  - B"));
    }

    void leavesFirstListItemDemoteUnchanged()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        QVERIFY(firstItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItem(parsed.document, firstItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B"));
    }

    void demotesListItemIntoExistingNestedList()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n- C"));
        const MarkdownNodeId thirdItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(thirdItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItem(parsed.document, thirdItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n  - B\n  - C"));
    }

    void demotesListItemWithNestedChildren()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n  - C"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItem(parsed.document, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n  - B\n    - C"));
    }

    void demotesContiguousListItemsUnderPreviousSibling()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n- C"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        const MarkdownNodeId thirdItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(secondItemId != 0);
        QVERIFY(thirdItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItems(parsed.document, {secondItemId, thirdItemId});

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n  - B\n  - C"));
    }

    void leavesDemoteUnchangedWhenFirstSelectedListItemIsFirst()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNodeId firstItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 0);
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(firstItemId != 0);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::demoteListItems(parsed.document, {firstItemId, secondItemId});

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B"));
    }

    void promotesNestedListItemToParentList()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B"));
        const MarkdownNodeId nestedItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(nestedItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::promoteListItem(parsed.document, nestedItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B"));
    }

    void promotesNestedListItemAndAdoptsFollowingSiblings()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - C"));
        const MarkdownNodeId nestedItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(nestedItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::promoteListItem(parsed.document, nestedItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B\n  - C"));
    }

    void promotesContiguousNestedListItemsToParentList()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - C"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        const MarkdownNodeId thirdItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(secondItemId != 0);
        QVERIFY(thirdItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::promoteListItems(parsed.document, {secondItemId, thirdItemId});

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B\n- C"));
    }

    void promotesContiguousNestedListItemsAndAdoptsFollowingSiblings()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - C\n  - D"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        const MarkdownNodeId thirdItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 2);
        QVERIFY(secondItemId != 0);
        QVERIFY(thirdItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::promoteListItems(parsed.document, {secondItemId, thirdItemId});

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B\n- C\n  - D"));
    }

    void leavesTopLevelListItemPromoteUnchanged()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNodeId secondItemId = nthNodeOfType(parsed.document, MarkdownNodeType::ListItem, 1);
        QVERIFY(secondItemId != 0);

        MarkdownDocument transformed = MarkdownTransform::promoteListItem(parsed.document, secondItemId);

        QCOMPARE(serialize(transformed), QStringLiteral("- A\n- B"));
    }

    void leavesUnsupportedSplitUnchanged()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **world**"));
        const MarkdownNodeId paragraphId = firstNodeOfType(parsed.document, MarkdownNodeType::Paragraph);
        QVERIFY(paragraphId != 0);

        MarkdownDocument transformed = MarkdownTransform::splitTextNodeIntoParagraphs(parsed.document, paragraphId, 5);

        QCOMPARE(serialize(transformed), QStringLiteral("Hello **world**"));
    }

    void serializerReportsInlineNodeContentSpans()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello **world** and `code`"));
        const MarkdownNodeId strongTextId = textNodeWithLiteral(parsed.document, QStringLiteral("world"));
        const MarkdownNodeId codeId = firstNodeOfType(parsed.document, MarkdownNodeType::InlineCode);
        QVERIFY(strongTextId != 0);
        QVERIFY(codeId != 0);

        MarkdownSerializer serializer;
        MarkdownSerializationResult result = serializer.serializeDocumentWithSourceMap(parsed.document);

        QCOMPARE(result.markdown, QStringLiteral("Hello **world** and `code`"));
        QCOMPARE(result.sourceOffsetForNodeOffset(strongTextId, 5).value_or(-1), 13);
        QCOMPARE(result.sourceOffsetForNodeOffset(codeId, 4).value_or(-1), 25);
    }

    void serializerReportsListItemContentSpans()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- Alpha\n- [ ] Beta\n1. Gamma"));
        const MarkdownNodeId alphaId = textNodeWithLiteral(parsed.document, QStringLiteral("Alpha"));
        const MarkdownNodeId betaId = textNodeWithLiteral(parsed.document, QStringLiteral("Beta"));
        const MarkdownNodeId gammaId = textNodeWithLiteral(parsed.document, QStringLiteral("Gamma"));
        QVERIFY(alphaId != 0);
        QVERIFY(betaId != 0);
        QVERIFY(gammaId != 0);

        MarkdownSerializer serializer;
        MarkdownSerializationResult result = serializer.serializeDocumentWithSourceMap(parsed.document);

        QCOMPARE(result.markdown, QStringLiteral("- Alpha\n- [ ] Beta\n\n1. Gamma"));
        QCOMPARE(result.sourceOffsetForNodeOffset(alphaId, 5).value_or(-1), 7);
        QCOMPARE(result.sourceOffsetForNodeOffset(betaId, 4).value_or(-1), 18);
        QCOMPARE(result.sourceOffsetForNodeOffset(gammaId, 5).value_or(-1), 28);
    }

    void serializerReportsNestedListContentSpans()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- Alpha\n  - Beta"));
        const MarkdownNodeId betaId = textNodeWithLiteral(parsed.document, QStringLiteral("Beta"));
        QVERIFY(betaId != 0);

        MarkdownSerializer serializer;
        MarkdownSerializationResult result = serializer.serializeDocumentWithSourceMap(parsed.document);

        QCOMPARE(result.markdown, QStringLiteral("- Alpha\n  - Beta"));
        QCOMPARE(result.sourceOffsetForNodeOffset(betaId, 4).value_or(-1), 16);
    }

    void serializerReportsBlockQuoteParagraphContentSpans()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("> Alpha\n>\n> Beta"));
        const MarkdownNodeId alphaId = textNodeWithLiteral(parsed.document, QStringLiteral("Alpha"));
        const MarkdownNodeId betaId = textNodeWithLiteral(parsed.document, QStringLiteral("Beta"));
        QVERIFY(alphaId != 0);
        QVERIFY(betaId != 0);

        MarkdownSerializer serializer;
        MarkdownSerializationResult result = serializer.serializeDocumentWithSourceMap(parsed.document);

        QCOMPARE(result.markdown, QStringLiteral("> Alpha\n> \n> Beta"));
        QCOMPARE(result.sourceOffsetForNodeOffset(alphaId, 5).value_or(-1), 7);
        QCOMPARE(result.sourceOffsetForNodeOffset(betaId, 4).value_or(-1), 17);
    }
};

QTEST_MAIN(TestMarkdownTransform)
#include "test_markdown_transform.moc"
