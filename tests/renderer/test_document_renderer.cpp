#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/RenderConsistency.h"
#include "theme/Theme.h"
#include "theme/ThemeStylesheet.h"

#include <QTest>

using namespace Muffin;

namespace {

const MarkdownNode* firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace

class TestDocumentRenderer : public QObject
{
    Q_OBJECT

private slots:
    void headingDoesNotStartWithEmptyBlock()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("# Title"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QVERIFY(result.document);
        QCOMPARE(result.document->toPlainText(), QStringLiteral("Title"));
    }

    void paragraphDoesNotStartWithEmptyBlock()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QVERIFY(result.document);
        QCOMPARE(result.document->toPlainText(), QStringLiteral("Hello"));
    }

    void recordsBlockIndex()
    {
        const QString markdown = QStringLiteral("# Title\n\nParagraph");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 2);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::Heading);
        QCOMPARE(result.blocks.at(0).source.start, 0);
        QCOMPARE(result.blocks.at(0).content.start, 2);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(result.blocks.at(1).source.start, 9);
    }

    void recordsInlineSyntaxTokens()
    {
        const QString markdown = QStringLiteral("**bold** *em* `code`");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.syntaxTokens.size(), 6);
        QCOMPARE(result.syntaxTokens.at(0).kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(result.syntaxTokens.at(0).source.start, 0);
        QCOMPARE(result.syntaxTokens.at(0).source.end, 2);
        QCOMPARE(result.syntaxTokens.at(2).kind, SyntaxTokenSpan::Kind::EmphasisMarker);
        QCOMPARE(result.syntaxTokens.at(2).source.start, 9);
        QCOMPARE(result.syntaxTokens.at(4).kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
        QCOMPARE(result.syntaxTokens.at(4).source.start, 15);
    }

    void recordsHiddenInlineMarkerFragments()
    {
        const QString markdown = QStringLiteral("**bold** *em* `code`");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.document->toPlainText(), QStringLiteral("bold em code"));
        QCOMPARE(result.fragments.size(), 6);
        QCOMPARE(result.fragments.at(0).kind, RenderFragment::Kind::Marker);
        QCOMPARE(result.fragments.at(0).markerKind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(result.fragments.at(0).source.start, 0);
        QCOMPARE(result.fragments.at(0).source.end, 2);
        QVERIFY(!result.fragments.at(0).visible);
        QVERIFY(!result.fragments.at(0).editable);
        QCOMPARE(result.fragments.at(0).nodeId, result.syntaxTokens.at(0).nodeId);
        QCOMPARE(result.fragments.at(1).source.start, 6);
        QCOMPARE(result.fragments.at(1).source.end, 8);
    }

    void recordsListItemBlockIndex()
    {
        const QString markdown = QStringLiteral("- A\n- B");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 2);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::List);
        QCOMPARE(result.blocks.at(0).source.start, 0);
        QCOMPARE(result.blocks.at(0).content.start, 2);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::List);
        QCOMPARE(result.blocks.at(1).source.start, 4);
        QCOMPARE(result.blocks.at(1).content.start, 6);
    }

    void recordsLooseListItemParagraphBlocks()
    {
        const QString markdown = QStringLiteral("- Alpha\n\n  Beta");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 3);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(result.blocks.at(0).content.start, 2);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(result.blocks.at(1).content.start, 11);
        QCOMPARE(result.blocks.at(2).kind, RenderSpan::Kind::List);
        QCOMPARE(result.blocks.at(2).content.start, 2);
    }

    void partialRenderLooseListItemIncludesAggregateRange()
    {
        const QString markdown = QStringLiteral("- Alpha\n\n  Beta");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        const MarkdownNode* item = firstNodeOfType(parsed.document, MarkdownNodeType::ListItem);
        QVERIFY(item);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        PartialRenderResult partial = renderer.renderPartial(parsed.document, {item->id}, parsed.mathSpans);

        QCOMPARE(partial.renderedNodeIds, QVector<MarkdownNodeId>{item->id});
        QCOMPARE(partial.blocks.size(), 3);
        QCOMPARE(partial.blocks.at(0).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(partial.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(partial.blocks.at(2).kind, RenderSpan::Kind::List);
        QCOMPARE(partial.blocks.at(2).nodeId, item->id);
        QVERIFY(partial.blocks.at(2).renderedStart <= partial.blocks.at(0).renderedStart);
        QVERIFY(partial.blocks.at(2).renderedEnd >= partial.blocks.at(1).renderedEnd);
    }

    void keepsTableCellsNonEditable()
    {
        const QString markdown = QStringLiteral("| A |\n| - |");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        for (const RenderSpan& span : result.sourceMap.spans()) {
            QVERIFY(span.kind != RenderSpan::Kind::Text || !span.editable);
        }
    }

    void recordsModelNodeIds()
    {
        const QString markdown = QStringLiteral("# Title\n\nParagraph");
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 2);
        QVERIFY(result.blocks.at(0).nodeId != 0);
        QVERIFY(result.blocks.at(1).nodeId != 0);
        QVERIFY(result.blocks.at(0).nodeId != result.blocks.at(1).nodeId);

        std::optional<RenderSpan> headingSpan = result.sourceMap.spanForNode(result.blocks.at(0).nodeId);
        QVERIFY(headingSpan.has_value());
        QCOMPARE(headingSpan->kind, RenderSpan::Kind::Heading);
        QCOMPARE(headingSpan->nodeId, result.blocks.at(0).nodeId);
    }

    void paragraphReplacementRangeDefaultsToRenderedContentRange()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 1);
        const MarkdownBlock& block = result.blocks.first();
        QCOMPARE(block.kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(block.effectiveReplacementRenderedStart(), block.renderedStart);
        QCOMPARE(block.effectiveReplacementRenderedEnd(), block.renderedEnd);
    }

    void codeBlockReplacementRangeCoversFrameAndContent()
    {
        CmarkParser parser;
        const QString markdown = QStringLiteral("```\nold\n```");
        ParseResult parsed = parser.parseDocument(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 1);
        const MarkdownBlock& block = result.blocks.first();
        QCOMPARE(block.kind, RenderSpan::Kind::CodeBlock);
        QVERIFY(block.hasRenderedRange());
        QVERIFY(block.hasReplacementRenderedRange());
        QVERIFY(block.effectiveReplacementRenderedStart() <= block.renderedStart);
        QVERIFY(block.effectiveReplacementRenderedEnd() >= block.renderedEnd);
        QVERIFY(block.effectiveReplacementRenderedStart() < block.renderedStart
                || block.effectiveReplacementRenderedEnd() > block.renderedEnd);
        QCOMPARE(block.source.start, 0);
        QCOMPARE(block.source.end, markdown.size());
        QCOMPARE(block.content.start, markdown.indexOf(QStringLiteral("old")));
        QCOMPARE(block.content.end, markdown.indexOf(QStringLiteral("\n```")));

        std::optional<RenderSpan> span = result.sourceMap.spanForNode(block.nodeId);
        QVERIFY(span.has_value());
        QCOMPARE(span->kind, RenderSpan::Kind::CodeBlock);
        QVERIFY(span->editable);
        QCOMPARE(span->editPolicy, RenderSpan::EditPolicy::LinearText);
        QVERIFY(block.editable);
        QCOMPARE(span->source.start, block.source.start);
        QCOMPARE(span->source.end, block.source.end);
        QCOMPARE(span->editSource.start, block.content.start);
        QCOMPARE(span->editSource.end, block.content.end);
    }

    void listItemReplacementRangeDefaultsToRenderedContentRange()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 2);
        for (const MarkdownBlock& block : result.blocks) {
            QCOMPARE(block.kind, RenderSpan::Kind::List);
            QCOMPARE(block.effectiveReplacementRenderedStart(), block.renderedStart);
            QCOMPARE(block.effectiveReplacementRenderedEnd(), block.renderedEnd);
        }
        QVERIFY(result.blocks.at(0).effectiveReplacementRenderedEnd() <= result.blocks.at(1).effectiveReplacementRenderedStart());
    }

    void recordsSimpleBlockQuoteAsBlockQuote()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("> quoted"));
        const MarkdownNode* quote = firstNodeOfType(parsed.document, MarkdownNodeType::BlockQuote);
        QVERIFY(quote);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 1);
        const MarkdownBlock& block = result.blocks.first();
        QCOMPARE(block.kind, RenderSpan::Kind::BlockQuote);
        QCOMPARE(block.nodeId, quote->id);
        QCOMPARE(block.source.start, 0);
        QCOMPARE(block.content.start, 2);
        QCOMPARE(block.effectiveReplacementRenderedStart(), block.renderedStart);
        QCOMPARE(block.effectiveReplacementRenderedEnd(), block.renderedEnd);
        std::optional<RenderSpan> span = result.sourceMap.spanForNode(quote->id);
        QVERIFY(span.has_value());
        QCOMPARE(span->kind, RenderSpan::Kind::BlockQuote);
    }

    void recordsMultiParagraphBlockQuoteAsParagraphBlocks()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("> A\n>\n> B"));
        const MarkdownNode* quote = firstNodeOfType(parsed.document, MarkdownNodeType::BlockQuote);
        QVERIFY(quote);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(parsed.document, parsed.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(parsed.document, {quote->id}, parsed.mathSpans);

        QCOMPARE(result.blocks.size(), 2);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
        QVERIFY(!result.sourceMap.spanForNode(quote->id).has_value());
        QCOMPARE(partial.renderedNodeIds, QVector<MarkdownNodeId>{quote->id});
        QCOMPARE(partial.blocks.size(), 2);
        QCOMPARE(partial.blocks.at(0).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(partial.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
    }

    void partialRenderReturnsEmptyForEmptyInvalidation()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        PartialRenderResult result = renderer.renderPartial(parsed.document, {});

        QVERIFY(result.renderedNodeIds.isEmpty());
        QVERIFY(result.replacementRanges.isEmpty());
        QVERIFY(result.blocks.isEmpty());
        QVERIFY(result.sourceMap.spans().isEmpty());
    }

    void partialRenderIgnoresInvalidNodeId()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        PartialRenderResult result = renderer.renderPartial(parsed.document, {99999});

        QVERIFY(result.renderedNodeIds.isEmpty());
        QVERIFY(result.replacementRanges.isEmpty());
        QVERIFY(result.blocks.isEmpty());
        QVERIFY(result.sourceMap.spans().isEmpty());
    }

    void partialRenderPromotesInlineTextToParagraph()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        const MarkdownNode* paragraph = firstNodeOfType(parsed.document, MarkdownNodeType::Paragraph);
        const MarkdownNode* text = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(paragraph);
        QVERIFY(text);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        PartialRenderResult result = renderer.renderPartial(parsed.document, {text->id});

        QCOMPARE(result.renderedNodeIds, QVector<MarkdownNodeId>{paragraph->id});
        QCOMPARE(result.blocks.size(), 1);
        QCOMPARE(result.blocks.first().nodeId, paragraph->id);
        QCOMPARE(result.blocks.first().kind, RenderSpan::Kind::Paragraph);
        QVERIFY(result.sourceMap.spanForNode(paragraph->id).has_value());
    }

    void partialRenderRendersListItemBlock()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNode* item = firstNodeOfType(parsed.document, MarkdownNodeType::ListItem);
        QVERIFY(item);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        PartialRenderResult result = renderer.renderPartial(parsed.document, {item->id});

        QCOMPARE(result.renderedNodeIds, QVector<MarkdownNodeId>{item->id});
        QCOMPARE(result.blocks.size(), 1);
        QCOMPARE(result.blocks.first().nodeId, item->id);
        QCOMPARE(result.blocks.first().kind, RenderSpan::Kind::List);
    }

    void partialRenderIsConsistentWithFullParagraphRender()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        const MarkdownNode* text = firstNodeOfType(parsed.document, MarkdownNodeType::Text);
        QVERIFY(text);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult full = renderer.render(parsed.document, parsed.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(parsed.document, {text->id}, parsed.mathSpans);
        RenderConsistencyResult consistency = RenderConsistency::comparePartialToFull(full, partial);

        QVERIFY2(consistency.ok, qPrintable(consistency.message));
    }

    void partialRenderIsConsistentWithFullListItemRender()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const MarkdownNode* item = firstNodeOfType(parsed.document, MarkdownNodeType::ListItem);
        QVERIFY(item);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult full = renderer.render(parsed.document, parsed.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(parsed.document, {item->id}, parsed.mathSpans);
        RenderConsistencyResult consistency = RenderConsistency::comparePartialToFull(full, partial);

        QVERIFY2(consistency.ok, qPrintable(consistency.message));
    }

    void renderConsistencyReportsBlockMismatch()
    {
        RenderResult full;
        full.blocks.append({{0, 5}, {0, 5}, {}, 0, 5, RenderSpan::Kind::Paragraph, true, 1});
        PartialRenderResult partial;
        partial.blocks.append({{0, 5}, {0, 5}, {}, 0, 5, RenderSpan::Kind::Heading, true, 1});

        RenderConsistencyResult consistency = RenderConsistency::comparePartialToFull(full, partial);

        QVERIFY(!consistency.ok);
        QVERIFY(consistency.message.contains(QStringLiteral("Block mismatch")));
    }

};

QTEST_GUILESS_MAIN(TestDocumentRenderer)
#include "test_document_renderer.moc"
