#include "PartialPatchApplyPolicy.h"

namespace Muffin {

PartialPatchApplyDecision PartialPatchApplyPolicy::decide(bool featureEnabled,
                                                          const PartialDocumentPatchPlan& plan,
                                                          const PartialDocumentPatchDryRun& dryRun,
                                                          const TextDocumentPatchConsistencyResult& consistency)
{
    if (!featureEnabled) {
        return {false, QStringLiteral("partial-patch-disabled")};
    }
    if (!plan.ok) {
        return {false, QStringLiteral("partial-patch-invalid-plan: %1").arg(plan.errors.join(QStringLiteral("; ")))};
    }
    if (plan.steps.isEmpty()) {
        return {false, QStringLiteral("partial-patch-empty-plan")};
    }
    if (!dryRun.ok) {
        return {false, QStringLiteral("partial-patch-invalid-dry-run: %1").arg(dryRun.errors.join(QStringLiteral("; ")))};
    }
    if (!consistency.ok) {
        return {false, QStringLiteral("partial-patch-consistency-failed: %1")
                           .arg(consistency.errors.join(QStringLiteral("; ")))};
    }
    return {true, QStringLiteral("partial-patch-allowed")};
}

} // namespace Muffin
