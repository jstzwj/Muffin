#include "renderer/PartialDocumentPatchPlanner.h"

#include <QTest>

using namespace Muffin;

namespace {

PartialReplacementRange replacement(MarkdownNodeId nodeId,
                                    int renderedStart,
                                    int renderedEnd,
                                    SourceSpan source,
                                    SourceRange sourceRange,
                                    RenderSpan::Kind kind)
{
    PartialReplacementRange range;
    range.nodeId = nodeId;
    range.renderedStart = renderedStart;
    range.renderedEnd = renderedEnd;
    range.contentRenderedStart = renderedStart;
    range.contentRenderedEnd = renderedEnd;
    range.source = source;
    range.sourceRange = sourceRange;
    range.kind = kind;
    return range;
}

} // namespace

class TestPartialDocumentPatchPlanner : public QObject
{
    Q_OBJECT

private slots:
    void createsPlanForMatchingReplacementAndBlock()
    {
        PartialRenderResult partial;
        partial.renderedNodeIds = {42};
        partial.replacementRanges.append(replacement(42, 10, 20, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph));
        partial.blocks.append({{1, 6}, {1, 6}, {1, 2, 1, 6}, 0, 5, RenderSpan::Kind::Paragraph, true, 42});

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);

        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));
        QCOMPARE(plan.steps.size(), 1);
        QCOMPARE(plan.steps.first().nodeId, static_cast<MarkdownNodeId>(42));
        QCOMPARE(plan.steps.first().oldRenderedStart, 10);
        QCOMPARE(plan.steps.first().oldRenderedEnd, 20);
        QCOMPARE(plan.steps.first().newRenderedStart, 0);
        QCOMPARE(plan.steps.first().newRenderedEnd, 5);
        QCOMPARE(plan.steps.first().source.start, 1);
        QCOMPARE(plan.steps.first().source.end, 6);
    }

    void reportsReplacementBlockCountMismatch()
    {
        PartialRenderResult partial;
        partial.renderedNodeIds = {42};
        partial.replacementRanges.append(replacement(42, 10, 20, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph));

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);

        QVERIFY(!plan.ok);
        QVERIFY(plan.errors.join(QStringLiteral("; ")).contains(QStringLiteral("count")));
        QVERIFY(plan.steps.isEmpty());
    }

    void createsSingleRangePlanForMultiBlockPartial()
    {
        PartialRenderResult partial;
        partial.renderedNodeIds = {42};
        partial.replacementRanges.append(replacement(42, 10, 20, {0, 12}, {1, 1, 3, 3}, RenderSpan::Kind::BlockQuote));
        partial.blocks.append({{2, 3}, {2, 3}, {1, 3, 1, 3}, 0, 1, RenderSpan::Kind::Paragraph, true, 50});
        partial.blocks.append({{9, 10}, {9, 10}, {3, 3, 3, 3}, 2, 3, RenderSpan::Kind::Paragraph, true, 51});

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);

        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));
        QCOMPARE(plan.steps.size(), 1);
        QCOMPARE(plan.steps.first().nodeId, static_cast<MarkdownNodeId>(42));
        QCOMPARE(plan.steps.first().oldRenderedStart, 10);
        QCOMPARE(plan.steps.first().oldRenderedEnd, 20);
        QCOMPARE(plan.steps.first().newRenderedStart, 0);
        QCOMPARE(plan.steps.first().newRenderedEnd, 3);
        QCOMPARE(plan.steps.first().kind, RenderSpan::Kind::BlockQuote);
    }

    void acceptsChangedSourceMetadataForSameNodeAndKind()
    {
        PartialRenderResult partial;
        partial.renderedNodeIds = {42};
        partial.replacementRanges.append(replacement(42, 10, 20, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph));
        partial.blocks.append({{1, 7}, {1, 7}, {1, 2, 1, 7}, 0, 5, RenderSpan::Kind::Paragraph, true, 42});

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);

        QVERIFY2(plan.ok, qPrintable(plan.errors.join(QStringLiteral("; "))));
        QCOMPARE(plan.steps.size(), 1);
        QCOMPARE(plan.steps.first().source.start, 1);
        QCOMPARE(plan.steps.first().source.end, 7);
    }

    void reportsKindMismatch()
    {
        PartialRenderResult partial;
        partial.renderedNodeIds = {42};
        partial.replacementRanges.append(replacement(42, 10, 20, {1, 6}, {1, 2, 1, 6}, RenderSpan::Kind::Paragraph));
        partial.blocks.append({{1, 7}, {1, 7}, {1, 2, 1, 7}, 0, 5, RenderSpan::Kind::Heading, true, 42});

        PartialDocumentPatchPlan plan = PartialDocumentPatchPlanner::plan(partial);

        QVERIFY(!plan.ok);
        QVERIFY(plan.errors.join(QStringLiteral("; ")).contains(QStringLiteral("metadata")));
        QVERIFY(plan.steps.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPartialDocumentPatchPlanner)
#include "test_partial_document_patch_planner.moc"
