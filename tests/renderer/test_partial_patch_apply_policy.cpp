#include "renderer/PartialPatchApplyPolicy.h"

#include <QTest>

using namespace Muffin;

class TestPartialPatchApplyPolicy : public QObject
{
    Q_OBJECT

private slots:
    void rejectsWhenFeatureDisabled()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = true;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = true;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(false, plan, dryRun, consistency);

        QVERIFY(!decision.shouldApply);
        QCOMPARE(decision.reason, QStringLiteral("partial-patch-disabled"));
    }

    void rejectsInvalidPlan()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = false;
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = true;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = true;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(true, plan, dryRun, consistency);

        QVERIFY(!decision.shouldApply);
        QVERIFY(decision.reason.startsWith(QStringLiteral("partial-patch-invalid-plan")));
    }

    void rejectsEmptyPlan()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = true;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = true;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(true, plan, dryRun, consistency);

        QVERIFY(!decision.shouldApply);
        QCOMPARE(decision.reason, QStringLiteral("partial-patch-empty-plan"));
    }

    void rejectsInvalidDryRun()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({1, 0, 5, 0, 7, SourceSpan{0, 5}, SourceRange{}, RenderSpan::Kind::Paragraph});
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = false;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = true;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(true, plan, dryRun, consistency);

        QVERIFY(!decision.shouldApply);
        QVERIFY(decision.reason.startsWith(QStringLiteral("partial-patch-invalid-dry-run")));
    }

    void rejectsFailedConsistency()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({1, 0, 5, 0, 7, SourceSpan{0, 5}, SourceRange{}, RenderSpan::Kind::Paragraph});
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = true;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = false;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(true, plan, dryRun, consistency);

        QVERIFY(!decision.shouldApply);
        QVERIFY(decision.reason.startsWith(QStringLiteral("partial-patch-consistency-failed")));
    }

    void allowsWhenAllChecksPass()
    {
        PartialDocumentPatchPlan plan;
        plan.ok = true;
        plan.steps.append({1, 0, 5, 0, 7, SourceSpan{0, 5}, SourceRange{}, RenderSpan::Kind::Paragraph});
        PartialDocumentPatchDryRun dryRun;
        dryRun.ok = true;
        TextDocumentPatchConsistencyResult consistency;
        consistency.ok = true;

        const PartialPatchApplyDecision decision =
            PartialPatchApplyPolicy::decide(true, plan, dryRun, consistency);

        QVERIFY(decision.shouldApply);
        QCOMPARE(decision.reason, QStringLiteral("partial-patch-allowed"));
    }
};

QTEST_GUILESS_MAIN(TestPartialPatchApplyPolicy)
#include "test_partial_patch_apply_policy.moc"
