#include "renderer/PartialDocumentPatchApplier.h"

#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/TextDocumentPatchConsistency.h"
#include "theme/ThemeStylesheet.h"

#include <QTest>
#include <QTextDocument>
#include <QPair>
#include <QVector>

using namespace Muffin;

namespace {

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

const MarkdownBlock* blockForNode(const QVector<MarkdownBlock>& blocks, MarkdownNodeId nodeId)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.nodeId == nodeId) {
            return &block;
        }
    }
    return nullptr;
}

PartialReplacementRange replacementForBlocks(MarkdownNodeId nodeId,
                                             RenderSpan::Kind kind,
                                             SourceSpan source,
                                             SourceRange sourceRange,
                                             const QVector<MarkdownBlock>& blocks)
{
    PartialReplacementRange range;
    range.nodeId = nodeId;
    range.renderedStart = blocks.first().effectiveReplacementRenderedStart();
    range.renderedEnd = blocks.last().effectiveReplacementRenderedEnd();
    range.contentRenderedStart = blocks.first().renderedStart;
    range.contentRenderedEnd = blocks.last().renderedEnd;
    range.source = source;
    range.sourceRange = sourceRange;
    range.kind = kind;
    return range;
}

} // namespace

class TestPartialDocumentPatchApplier : public QObject
{
    Q_OBJECT

private slots:
    void acceptsSingleParagraphStepInBounds()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({42, 2, 7, 0, 5, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph});

        PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

        QVERIFY2(dryRun.ok, qPrintable(dryRun.errors.join(QStringLiteral("; "))));
        QVERIFY(dryRun.errors.isEmpty());
    }

    void acceptsSingleHeadingStepInBounds()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({42, 2, 7, 0, 5, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Heading});

        PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

        QVERIFY2(dryRun.ok, qPrintable(dryRun.errors.join(QStringLiteral("; "))));
        QVERIFY(dryRun.errors.isEmpty());
    }

    void rejectsInvalidPlan()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = false;
        plan.errors.append(QStringLiteral("planner failed"));

        PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

        QVERIFY(!dryRun.ok);
        QVERIFY(dryRun.errors.join(QStringLiteral("; ")).contains(QStringLiteral("planner failed")));
    }

    void rejectsMultipleStepsUntilRangeReplacementIsImplemented()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({1, 0, 3, 0, 3, {0, 3}, {1, 1, 1, 4}, RenderSpan::Kind::Paragraph});
        plan.steps.append({2, 4, 8, 0, 4, {4, 8}, {2, 1, 2, 5}, RenderSpan::Kind::Paragraph});

        PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

        QVERIFY(!dryRun.ok);
        QVERIFY(dryRun.errors.join(QStringLiteral("; ")).contains(QStringLiteral("exactly one replacement step")));
    }

    void rejectsUnsupportedBlockKinds()
    {
        const QVector<QPair<RenderSpan::Kind, QString>> cases{
            {RenderSpan::Kind::Table, QStringLiteral("Table")}
        };

        for (const auto& testCase : cases) {
            PartialDocumentPatchPlan plan;
            plan.ok = true;
            plan.steps.append({42, 0, 8, 0, 8, {0, 8}, {1, 1, 1, 9}, testCase.first});

            PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

            QVERIFY(!dryRun.ok);
            const QString errors = dryRun.errors.join(QStringLiteral("; "));
            QVERIFY2(errors.contains(QStringLiteral("Range patch does not support")),
                     qPrintable(errors));
            QVERIFY2(errors.contains(testCase.second), qPrintable(errors));
        }
    }

    void rejectsOldRangeOutsideCurrentDocument()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({42, 2, 12, 0, 5, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph});

        PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, 10);

        QVERIFY(!dryRun.ok);
        QVERIFY(dryRun.errors.join(QStringLiteral("; ")).contains(QStringLiteral("exceeds")));
    }

    void appliesSingleParagraphFragment()
    {
        QTextDocument current;
        current.setPlainText(QStringLiteral("Hello"));
        QTextDocument partial;
        partial.setPlainText(QStringLiteral("World"));
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({42, 0, 5, 0, 5, {0, 5}, {1, 1, 1, 6}, RenderSpan::Kind::Paragraph});

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(current, partial, plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.document);
        QCOMPARE(result.document->toPlainText(), QStringLiteral("World"));
    }

    void appliesRendererProducedSingleParagraphPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("Hello"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeParagraph = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeParagraph);

        ParseResult after = parser.parseDocument(QStringLiteral("World"), before.document);
        const MarkdownNode* afterRoot = after.document.nodeById(after.document.rootId());
        QVERIFY(afterRoot);
        const MarkdownNode* afterParagraph = after.document.nodeById(afterRoot->children.first());
        QVERIFY(afterParagraph);
        QCOMPARE(afterParagraph->id, beforeParagraph->id);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterParagraph->id}, after.mathSpans);
        QVERIFY(partial.document);
        partial.replacementRanges.append(replacementForBlock(beforeFull.blocks.first()));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
    }

    void appliesRendererProducedSingleHeadingPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("# Old"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeHeading = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeHeading);

        ParseResult after = parser.parseDocument(QStringLiteral("# New"), before.document);
        const MarkdownNode* afterRoot = after.document.nodeById(after.document.rootId());
        QVERIFY(afterRoot);
        const MarkdownNode* afterHeading = after.document.nodeById(afterRoot->children.first());
        QVERIFY(afterHeading);
        QCOMPARE(afterHeading->id, beforeHeading->id);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterHeading->id}, after.mathSpans);
        QVERIFY(partial.document);
        partial.replacementRanges.append(replacementForBlock(beforeFull.blocks.first()));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedSingleListItemPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("- old"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeList = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeList);
        const MarkdownNode* beforeItem = before.document.nodeById(beforeList->children.first());
        QVERIFY(beforeItem);

        ParseResult after = parser.parseDocument(QStringLiteral("- new"), before.document);
        const MarkdownNode* afterItem = after.document.nodeById(beforeItem->id);
        QVERIFY(afterItem);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterItem->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeItemBlock = blockForNode(beforeFull.blocks, beforeItem->id);
        QVERIFY(beforeItemBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeItemBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedMiddleListItemPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("- A\n- old\n- C"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeList = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeList);
        QCOMPARE(beforeList->children.size(), 3);
        const MarkdownNode* beforeItem = before.document.nodeById(beforeList->children.at(1));
        QVERIFY(beforeItem);

        ParseResult after = parser.parseDocument(QStringLiteral("- A\n- new\n- C"), before.document);
        const MarkdownNode* afterItem = after.document.nodeById(beforeItem->id);
        QVERIFY(afterItem);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterItem->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeItemBlock = blockForNode(beforeFull.blocks, beforeItem->id);
        QVERIFY(beforeItemBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeItemBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedLooseListItemPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("- A\n\n  old"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeList = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeList);
        const MarkdownNode* beforeItem = before.document.nodeById(beforeList->children.first());
        QVERIFY(beforeItem);

        ParseResult after = parser.parseDocument(QStringLiteral("- A\n\n  new"), before.document);
        const MarkdownNode* afterItem = after.document.nodeById(beforeItem->id);
        QVERIFY(afterItem);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterItem->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeItemBlock = blockForNode(beforeFull.blocks, beforeItem->id);
        QVERIFY(beforeItemBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeItemBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedLooseListItemPatchBetweenItems()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("- X\n- A\n\n  old\n- Y"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeList = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeList);
        QCOMPARE(beforeList->children.size(), 3);
        const MarkdownNode* beforeItem = before.document.nodeById(beforeList->children.at(1));
        QVERIFY(beforeItem);

        ParseResult after = parser.parseDocument(QStringLiteral("- X\n- A\n\n  new\n- Y"), before.document);
        const MarkdownNode* afterItem = after.document.nodeById(beforeItem->id);
        QVERIFY(afterItem);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterItem->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeItemBlock = blockForNode(beforeFull.blocks, beforeItem->id);
        QVERIFY(beforeItemBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeItemBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedSingleBlockQuotePatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("> old"));
        const MarkdownNode* beforeQuote = nullptr;
        for (const MarkdownNode& node : before.document.nodes()) {
            if (node.type == MarkdownNodeType::BlockQuote) {
                beforeQuote = &node;
                break;
            }
        }
        QVERIFY(beforeQuote);

        ParseResult after = parser.parseDocument(QStringLiteral("> new"), before.document);
        const MarkdownNode* afterQuote = after.document.nodeById(beforeQuote->id);
        QVERIFY(afterQuote);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterQuote->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeQuoteBlock = blockForNode(beforeFull.blocks, beforeQuote->id);
        QVERIFY(beforeQuoteBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeQuoteBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedBlockQuotePatchBetweenParagraphs()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("Before\n\n> old\n\nAfter"));
        const MarkdownNode* beforeQuote = nullptr;
        for (const MarkdownNode& node : before.document.nodes()) {
            if (node.type == MarkdownNodeType::BlockQuote) {
                beforeQuote = &node;
                break;
            }
        }
        QVERIFY(beforeQuote);

        ParseResult after = parser.parseDocument(QStringLiteral("Before\n\n> new\n\nAfter"), before.document);
        const MarkdownNode* afterQuote = after.document.nodeById(beforeQuote->id);
        QVERIFY(afterQuote);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterQuote->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeQuoteBlock = blockForNode(beforeFull.blocks, beforeQuote->id);
        QVERIFY(beforeQuoteBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeQuoteBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedMultiParagraphBlockQuotePatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("> A\n> \n> B"));
        const MarkdownNode* beforeQuote = nullptr;
        for (const MarkdownNode& node : before.document.nodes()) {
            if (node.type == MarkdownNodeType::BlockQuote) {
                beforeQuote = &node;
                break;
            }
        }
        QVERIFY(beforeQuote);

        ParseResult after = parser.parseDocument(QStringLiteral("> A\n> \n> C"), before.document);
        const MarkdownNode* afterQuote = after.document.nodeById(beforeQuote->id);
        QVERIFY(afterQuote);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterQuote->id}, after.mathSpans);
        QVERIFY(partial.document);
        QCOMPARE(partial.blocks.size(), 2);
        partial.replacementRanges.append(replacementForBlocks(beforeQuote->id,
                                                              RenderSpan::Kind::BlockQuote,
                                                              beforeQuote->source,
                                                              beforeQuote->sourceRange,
                                                              beforeFull.blocks));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedSingleCodeBlockPatch()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("```\nold\n```"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeCode = before.document.nodeById(beforeRoot->children.first());
        QVERIFY(beforeCode);

        ParseResult after = parser.parseDocument(QStringLiteral("```\nnew\n```"), before.document);
        const MarkdownNode* afterRoot = after.document.nodeById(after.document.rootId());
        QVERIFY(afterRoot);
        const MarkdownNode* afterCode = after.document.nodeById(afterRoot->children.first());
        QVERIFY(afterCode);
        QCOMPARE(afterCode->id, beforeCode->id);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterCode->id}, after.mathSpans);
        QVERIFY(partial.document);
        partial.replacementRanges.append(replacementForBlock(beforeFull.blocks.first()));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }

    void appliesRendererProducedCodeBlockPatchBetweenParagraphs()
    {
        CmarkParser parser;
        ParseResult before = parser.parseDocument(QStringLiteral("Before\n\n```\nold\n```\n\nAfter"));
        const MarkdownNode* beforeRoot = before.document.nodeById(before.document.rootId());
        QVERIFY(beforeRoot);
        const MarkdownNode* beforeCode = nullptr;
        for (MarkdownNodeId childId : beforeRoot->children) {
            const MarkdownNode* child = before.document.nodeById(childId);
            if (child && child->type == MarkdownNodeType::CodeBlock) {
                beforeCode = child;
                break;
            }
        }
        QVERIFY(beforeCode);

        ParseResult after = parser.parseDocument(QStringLiteral("Before\n\n```\nnew\n```\n\nAfter"), before.document);
        const MarkdownNode* afterCode = after.document.nodeById(beforeCode->id);
        QVERIFY(afterCode);

        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);
        RenderResult beforeFull = renderer.render(before.document, before.mathSpans);
        RenderResult afterFull = renderer.render(after.document, after.mathSpans);
        PartialRenderResult partial = renderer.renderPartial(after.document, {afterCode->id}, after.mathSpans);
        QVERIFY(partial.document);
        const MarkdownBlock* beforeCodeBlock = blockForNode(beforeFull.blocks, beforeCode->id);
        QVERIFY(beforeCodeBlock);
        partial.replacementRanges.append(replacementForBlock(*beforeCodeBlock));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);
        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));

        PartialDocumentPatchApplyResult result = PartialDocumentPatchApplier::applyRangeReplacement(*beforeFull.document,
                                                                                               *partial.document,
                                                                                               plan);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.document->toPlainText(), afterFull.document->toPlainText());
        TextDocumentPatchConsistencyResult consistency = TextDocumentPatchConsistency::compare(*result.document,
                                                                                              *afterFull.document);
        QVERIFY2(consistency.ok, qPrintable(consistency.message()));
    }
};

QTEST_GUILESS_MAIN(TestPartialDocumentPatchApplier)
#include "test_partial_document_patch_applier.moc"
