#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/PartialDocumentPatchPlanner.h"
#include "renderer/PartialRenderStateMerger.h"
#include "theme/ThemeStylesheet.h"

#include <QTest>

using namespace Muffin;

namespace {

const MarkdownNode* firstChildBlock(const MarkdownDocument& document)
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    if (!root || root->children.isEmpty()) {
        return nullptr;
    }
    return document.nodeById(root->children.first());
}

const MarkdownNode* nthChildBlock(const MarkdownDocument& document, int index)
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    if (!root || index < 0 || index >= root->children.size()) {
        return nullptr;
    }
    return document.nodeById(root->children.at(index));
}

const MarkdownBlock* blockForNode(const QVector<MarkdownBlock>& blocks, MarkdownNodeId nodeId)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.nodeId == nodeId) {
            return &block;
        }
    }
    return nullptr;
}

PartialReplacementRange replacementForBlock(const MarkdownBlock& block)
{
    PartialReplacementRange range;
    range.nodeId = block.nodeId;
    range.renderedStart = block.effectiveReplacementRenderedStart();
    range.renderedEnd = block.effectiveReplacementRenderedEnd();
    range.contentRenderedStart = block.renderedStart;
    range.contentRenderedEnd = block.renderedEnd;
    range.source = block.source;
    range.sourceRange = block.sourceRange;
    range.kind = block.kind;
    return range;
}

void compareSourceSpan(SourceSpan actual, SourceSpan expected)
{
    QCOMPARE(actual.start, expected.start);
    QCOMPARE(actual.end, expected.end);
}

void compareSourceRange(const SourceRange& actual, const SourceRange& expected)
{
    QCOMPARE(actual.startLine, expected.startLine);
    QCOMPARE(actual.startColumn, expected.startColumn);
    QCOMPARE(actual.endLine, expected.endLine);
    QCOMPARE(actual.endColumn, expected.endColumn);
}

void compareRenderSpan(const RenderSpan& actual, const RenderSpan& expected)
{
    QCOMPARE(actual.renderedStart, expected.renderedStart);
    QCOMPARE(actual.renderedEnd, expected.renderedEnd);
    compareSourceSpan(actual.source, expected.source);
    compareSourceRange(actual.sourceRange, expected.sourceRange);
    QCOMPARE(actual.kind, expected.kind);
    QCOMPARE(actual.editable, expected.editable);
    QCOMPARE(actual.block, expected.block);
    compareSourceSpan(actual.editSource, expected.editSource);
    QCOMPARE(actual.editPolicy, expected.editPolicy);
    QCOMPARE(actual.nodeId, expected.nodeId);
}

void compareSourceMap(const RenderSourceMap& actual, const RenderSourceMap& expected)
{
    QCOMPARE(actual.spans().size(), expected.spans().size());
    for (int i = 0; i < expected.spans().size(); ++i) {
        compareRenderSpan(actual.spans().at(i), expected.spans().at(i));
    }
}

void compareBlocks(const QVector<MarkdownBlock>& actual, const QVector<MarkdownBlock>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        const MarkdownBlock& actualBlock = actual.at(i);
        const MarkdownBlock& expectedBlock = expected.at(i);
        compareSourceSpan(actualBlock.source, expectedBlock.source);
        compareSourceSpan(actualBlock.content, expectedBlock.content);
        compareSourceRange(actualBlock.sourceRange, expectedBlock.sourceRange);
        QCOMPARE(actualBlock.renderedStart, expectedBlock.renderedStart);
        QCOMPARE(actualBlock.renderedEnd, expectedBlock.renderedEnd);
        QCOMPARE(actualBlock.kind, expectedBlock.kind);
        QCOMPARE(actualBlock.editable, expectedBlock.editable);
        QCOMPARE(actualBlock.nodeId, expectedBlock.nodeId);
        QCOMPARE(actualBlock.replacementRenderedStart, expectedBlock.replacementRenderedStart);
        QCOMPARE(actualBlock.replacementRenderedEnd, expectedBlock.replacementRenderedEnd);
    }
}

void compareSyntaxTokens(const QVector<SyntaxTokenSpan>& actual, const QVector<SyntaxTokenSpan>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        compareSourceSpan(actual.at(i).source, expected.at(i).source);
        QCOMPARE(actual.at(i).kind, expected.at(i).kind);
    }
}

void compareFragments(const QVector<RenderFragment>& actual, const QVector<RenderFragment>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        const RenderFragment& actualFragment = actual.at(i);
        const RenderFragment& expectedFragment = expected.at(i);
        QCOMPARE(actualFragment.nodeId, expectedFragment.nodeId);
        compareSourceSpan(actualFragment.source, expectedFragment.source);
        compareRenderSpan(actualFragment.rendered, expectedFragment.rendered);
        QCOMPARE(actualFragment.kind, expectedFragment.kind);
        QCOMPARE(actualFragment.markerKind, expectedFragment.markerKind);
        QCOMPARE(actualFragment.visible, expectedFragment.visible);
        QCOMPARE(actualFragment.editable, expectedFragment.editable);
    }
}

PartialRenderStateMergeResult mergeSingleBlock(const QString& beforeMarkdown,
                                               const QString& afterMarkdown,
                                               int blockIndex)
{
    CmarkParser parser;
    ParseResult before = parser.parseDocument(beforeMarkdown);
    ParseResult after = parser.parseDocument(afterMarkdown, before.document);
    const MarkdownNode* beforeBlock = nthChildBlock(before.document, blockIndex);
    const MarkdownNode* afterBlock = nthChildBlock(after.document, blockIndex);
    Q_ASSERT(beforeBlock);
    Q_ASSERT(afterBlock);
    Q_ASSERT(beforeBlock->id == afterBlock->id);

    ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
    DocumentRenderer renderer(stylesheet);
    RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
    PartialRenderResult partial = renderer.renderPartial(after.document, {afterBlock->id}, after.mathSpans);
    const MarkdownBlock* beforeRenderedBlock = blockForNode(beforeFull.blocks, beforeBlock->id);
    Q_ASSERT(beforeRenderedBlock);
    partial.replacementRanges.append(replacementForBlock(*beforeRenderedBlock));
    const PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
    Q_ASSERT(plan.ok);

    return PartialRenderStateMerger::merge(beforeFull.sourceMap,
                                           beforeFull.blocks,
                                           beforeFull.syntaxTokens,
                                           beforeFull.fragments,
                                           partial,
                                           plan);
}

} // namespace

class TestPartialRenderStateMerger : public QObject
{
    Q_OBJECT

private slots:
    void mergesSingleParagraphState()
    {
        const QString beforeMarkdown = QStringLiteral("Before\n\nHello\n\nAfter");
        const QString afterMarkdown = QStringLiteral("Before\n\nHello Qt\n\nAfter");
        CmarkParser parser;
        ParseResult before = parser.parseDocument(beforeMarkdown);
        ParseResult after = parser.parseDocument(afterMarkdown, before.document);
        const MarkdownNode* afterParagraph = nthChildBlock(after.document, 1);
        QVERIFY(afterParagraph);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterParagraph->id}, after.mathSpans);
        const MarkdownBlock* beforeRenderedBlock = blockForNode(beforeFull.blocks, afterParagraph->id);
        QVERIFY(beforeRenderedBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeRenderedBlock));
        const PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        const PartialRenderStateMergeResult merged = PartialRenderStateMerger::merge(beforeFull.sourceMap,
                                                                                     beforeFull.blocks,
                                                                                     beforeFull.syntaxTokens,
                                                                                     beforeFull.fragments,
                                                                                     partial,
                                                                                     plan);

        QVERIFY2(merged.ok, qPrintable(merged.errors.join(QStringLiteral("; "))));
        compareSourceMap(merged.sourceMap, afterFull.sourceMap);
        compareBlocks(merged.blocks, afterFull.blocks);
        compareSyntaxTokens(merged.syntaxTokens, afterFull.syntaxTokens);
        compareFragments(merged.fragments, afterFull.fragments);
    }

    void mergesSingleHeadingState()
    {
        const QString beforeMarkdown = QStringLiteral("Before\n\n# Old\n\nAfter");
        const QString afterMarkdown = QStringLiteral("Before\n\n# Newer\n\nAfter");
        CmarkParser parser;
        ParseResult before = parser.parseDocument(beforeMarkdown);
        ParseResult after = parser.parseDocument(afterMarkdown, before.document);
        const MarkdownNode* afterHeading = nthChildBlock(after.document, 1);
        QVERIFY(afterHeading);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterHeading->id}, after.mathSpans);
        const MarkdownBlock* beforeRenderedBlock = blockForNode(beforeFull.blocks, afterHeading->id);
        QVERIFY(beforeRenderedBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeRenderedBlock));
        const PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        const PartialRenderStateMergeResult merged = PartialRenderStateMerger::merge(beforeFull.sourceMap,
                                                                                     beforeFull.blocks,
                                                                                     beforeFull.syntaxTokens,
                                                                                     beforeFull.fragments,
                                                                                     partial,
                                                                                     plan);

        QVERIFY2(merged.ok, qPrintable(merged.errors.join(QStringLiteral("; "))));
        compareSourceMap(merged.sourceMap, afterFull.sourceMap);
        compareBlocks(merged.blocks, afterFull.blocks);
        compareSyntaxTokens(merged.syntaxTokens, afterFull.syntaxTokens);
        compareFragments(merged.fragments, afterFull.fragments);
    }

    void mergesSyntaxTokensForInlineStyleInsideParagraph()
    {
        const QString beforeMarkdown = QStringLiteral("Before\n\nHello **old**\n\nAfter");
        const QString afterMarkdown = QStringLiteral("Before\n\nHello **newer**\n\nAfter");
        CmarkParser parser;
        ParseResult before = parser.parseDocument(beforeMarkdown);
        ParseResult after = parser.parseDocument(afterMarkdown, before.document);
        const MarkdownNode* afterParagraph = nthChildBlock(after.document, 1);
        QVERIFY(afterParagraph);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterParagraph->id}, after.mathSpans);
        const MarkdownBlock* beforeRenderedBlock = blockForNode(beforeFull.blocks, afterParagraph->id);
        QVERIFY(beforeRenderedBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeRenderedBlock));
        const PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        const PartialRenderStateMergeResult merged = PartialRenderStateMerger::merge(beforeFull.sourceMap,
                                                                                     beforeFull.blocks,
                                                                                     beforeFull.syntaxTokens,
                                                                                     beforeFull.fragments,
                                                                                     partial,
                                                                                     plan);

        QVERIFY2(merged.ok, qPrintable(merged.errors.join(QStringLiteral("; "))));
        compareSourceMap(merged.sourceMap, afterFull.sourceMap);
        compareBlocks(merged.blocks, afterFull.blocks);
        compareSyntaxTokens(merged.syntaxTokens, afterFull.syntaxTokens);
        compareFragments(merged.fragments, afterFull.fragments);
    }

    void rejectsInvalidPlan()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = false;
        plan.errors.append(QStringLiteral("bad plan"));
        PartialRenderResult partial;
        RenderSourceMap sourceMap;

        const PartialRenderStateMergeResult merged =
            PartialRenderStateMerger::merge(sourceMap, {}, {}, {}, partial, plan);

        QVERIFY(!merged.ok);
        QVERIFY(merged.errors.join(QStringLiteral("; ")).contains(QStringLiteral("bad plan")));
    }
};

QTEST_GUILESS_MAIN(TestPartialRenderStateMerger)
#include "test_partial_render_state_merger.moc"
