#include "renderer/IncrementalRenderValidationPolicy.h"

#include <QTest>

using namespace Muffin;

namespace {

PartialDocumentPatchPlan validPlan(RenderSpan::Kind kind)
{
    PartialDocumentPatchPlan plan;
    plan.ok = true;
    plan.steps.append({1, 0, 5, 0, 7, SourceSpan{0, 5}, SourceRange{}, kind});
    return plan;
}

PartialDocumentPatchDryRun validDryRun()
{
    PartialDocumentPatchDryRun dryRun;
    dryRun.ok = true;
    return dryRun;
}

PartialRenderStateMergeResult validStateMerge()
{
    PartialRenderStateMergeResult stateMerge;
    stateMerge.ok = true;
    return stateMerge;
}

PartialRenderResult singleBlockListPartial()
{
    PartialRenderResult partial;
    partial.renderedNodeIds = {1};
    partial.replacementRanges.append({1, 0, 5, 0, 5, SourceSpan{0, 5}, SourceRange{}, RenderSpan::Kind::List});
    partial.blocks.append({SourceSpan{0, 5}, SourceSpan{2, 5}, SourceRange{}, 0, 7, RenderSpan::Kind::List, true, 1});
    return partial;
}

PartialRenderResult multiBlockListPartial()
{
    PartialRenderResult partial = singleBlockListPartial();
    partial.blocks.append({SourceSpan{8, 12}, SourceSpan{8, 12}, SourceRange{}, 8, 12, RenderSpan::Kind::Paragraph, true, 2});
    return partial;
}

PartialRenderResult singleBlockQuotePartial()
{
    PartialRenderResult partial;
    partial.renderedNodeIds = {1};
    partial.replacementRanges.append({1, 0, 5, 0, 5, SourceSpan{0, 5}, SourceRange{}, RenderSpan::Kind::BlockQuote});
    partial.blocks.append({SourceSpan{0, 5}, SourceSpan{2, 5}, SourceRange{}, 0, 3, RenderSpan::Kind::BlockQuote, true, 1});
    return partial;
}

PartialRenderResult singleBlockCodeBlockPartial()
{
    PartialRenderResult partial;
    partial.renderedNodeIds = {1};
    partial.replacementRanges.append({1, 0, 8, 0, 8, SourceSpan{0, 12}, SourceRange{}, RenderSpan::Kind::CodeBlock});
    partial.blocks.append({SourceSpan{0, 12}, SourceSpan{4, 8}, SourceRange{}, 0, 10, RenderSpan::Kind::CodeBlock, true, 1});
    return partial;
}

PartialRenderResult mismatchedCodeBlockPartial()
{
    PartialRenderResult partial = singleBlockCodeBlockPartial();
    partial.blocks.first().nodeId = 2;
    return partial;
}

PartialRenderResult multiBlockCodeBlockPartial()
{
    PartialRenderResult partial = singleBlockCodeBlockPartial();
    partial.blocks.append({SourceSpan{14, 18}, SourceSpan{14, 18}, SourceRange{}, 12, 16, RenderSpan::Kind::Paragraph, true, 2});
    return partial;
}

PartialRenderResult multiBlockQuotePartial()
{
    PartialRenderResult partial = singleBlockQuotePartial();
    partial.blocks.append({SourceSpan{8, 12}, SourceSpan{8, 12}, SourceRange{}, 4, 8, RenderSpan::Kind::Paragraph, true, 2});
    return partial;
}

} // namespace

class TestIncrementalRenderValidationPolicy : public QObject
{
    Q_OBJECT

private slots:
    void autoTrustsParagraphAndHeading()
    {
        const QVector<RenderSpan::Kind> trustedKinds{
            RenderSpan::Kind::Paragraph,
            RenderSpan::Kind::Heading
        };

        for (RenderSpan::Kind kind : trustedKinds) {
            QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                               validPlan(kind),
                                                               validDryRun(),
                                                               validStateMerge()),
                     IncrementalRenderValidationMode::TrustLocalChecks);
        }
    }

    void autoRequiresFullCompareForAggregateCodeBlockListAndBlockQuote()
    {
        const QVector<RenderSpan::Kind> fullCompareKinds{
            RenderSpan::Kind::CodeBlock,
            RenderSpan::Kind::List,
            RenderSpan::Kind::BlockQuote
        };

        for (RenderSpan::Kind kind : fullCompareKinds) {
            QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                               validPlan(kind),
                                                               validDryRun(),
                                                               validStateMerge()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
        }
    }

    void autoTrustsSingleBlockCodeBlock()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::CodeBlock),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           singleBlockCodeBlockPartial()),
                 IncrementalRenderValidationMode::TrustLocalChecks);
        const IncrementalRenderValidationDecision decision =
            IncrementalRenderValidationPolicy::evaluate(IncrementalRenderValidationMode::Auto,
                                                        validPlan(RenderSpan::Kind::CodeBlock),
                                                        validDryRun(),
                                                        validStateMerge(),
                                                        singleBlockCodeBlockPartial());
        QCOMPARE(decision.mode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(decision.reason, QStringLiteral("single-block-code-block"));
    }

    void autoRequiresFullCompareForCodeBlockNodeMismatch()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::CodeBlock),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           mismatchedCodeBlockPartial()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
        const IncrementalRenderValidationDecision decision =
            IncrementalRenderValidationPolicy::evaluate(IncrementalRenderValidationMode::Auto,
                                                        validPlan(RenderSpan::Kind::CodeBlock),
                                                        validDryRun(),
                                                        validStateMerge(),
                                                        mismatchedCodeBlockPartial());
        QCOMPARE(decision.mode, IncrementalRenderValidationMode::AlwaysFullCompare);
        QCOMPARE(decision.reason, QStringLiteral("not-single-block-replacement"));
    }

    void autoRequiresFullCompareForMultiBlockCodeBlock()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::CodeBlock),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           multiBlockCodeBlockPartial()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
    }

    void autoTrustsSingleBlockList()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::List),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           singleBlockListPartial()),
                 IncrementalRenderValidationMode::TrustLocalChecks);
    }

    void autoRequiresFullCompareForMultiBlockList()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::List),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           multiBlockListPartial()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
    }

    void autoTrustsSingleBlockQuote()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::BlockQuote),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           singleBlockQuotePartial()),
                 IncrementalRenderValidationMode::TrustLocalChecks);
    }

    void autoRequiresFullCompareForMultiBlockQuote()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::BlockQuote),
                                                           validDryRun(),
                                                           validStateMerge(),
                                                           multiBlockQuotePartial()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
    }

    void autoRequiresFullCompareWhenChecksFail()
    {
        PartialDocumentPatchPlan invalidPlan = validPlan(RenderSpan::Kind::Paragraph);
        invalidPlan.ok = false;
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           invalidPlan,
                                                           validDryRun(),
                                                           validStateMerge()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);

        PartialDocumentPatchDryRun invalidDryRun = validDryRun();
        invalidDryRun.ok = false;
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::Paragraph),
                                                           invalidDryRun,
                                                           validStateMerge()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);

        PartialRenderStateMergeResult invalidStateMerge = validStateMerge();
        invalidStateMerge.ok = false;
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           validPlan(RenderSpan::Kind::Paragraph),
                                                           validDryRun(),
                                                           invalidStateMerge),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
    }

    void autoRequiresFullCompareForMultipleSteps()
    {
        PartialDocumentPatchPlan plan = validPlan(RenderSpan::Kind::Paragraph);
        plan.steps.append({2, 8, 12, 8, 14, SourceSpan{8, 12}, SourceRange{}, RenderSpan::Kind::Paragraph});

        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::Auto,
                                                           plan,
                                                           validDryRun(),
                                                           validStateMerge()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
    }

    void explicitModesBypassAutoDecision()
    {
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::AlwaysFullCompare,
                                                           validPlan(RenderSpan::Kind::Paragraph),
                                                           validDryRun(),
                                                           validStateMerge()),
                 IncrementalRenderValidationMode::AlwaysFullCompare);
        QCOMPARE(IncrementalRenderValidationPolicy::decide(IncrementalRenderValidationMode::TrustLocalChecks,
                                                           validPlan(RenderSpan::Kind::List),
                                                           validDryRun(),
                                                           validStateMerge()),
                 IncrementalRenderValidationMode::TrustLocalChecks);
        const IncrementalRenderValidationDecision decision =
            IncrementalRenderValidationPolicy::evaluate(IncrementalRenderValidationMode::AlwaysFullCompare,
                                                        validPlan(RenderSpan::Kind::Paragraph),
                                                        validDryRun(),
                                                        validStateMerge());
        QCOMPARE(decision.mode, IncrementalRenderValidationMode::AlwaysFullCompare);
        QCOMPARE(decision.reason, QStringLiteral("explicit-validation-mode"));
    }
};

QTEST_GUILESS_MAIN(TestIncrementalRenderValidationPolicy)
#include "test_incremental_render_validation_policy.moc"
