#pragma once

#include "renderer/PartialDocumentPatchApplier.h"
#include "renderer/PartialDocumentPatchPlanner.h"
#include "renderer/TextDocumentPatchConsistency.h"

#include <QString>

namespace Muffin {

struct PartialPatchApplyDecision {
    bool shouldApply = false;
    QString reason;
};

class PartialPatchApplyPolicy {
public:
    static PartialPatchApplyDecision decide(bool featureEnabled,
                                            const PartialDocumentPatchPlan& plan,
                                            const PartialDocumentPatchDryRun& dryRun,
                                            const TextDocumentPatchConsistencyResult& consistency);
};

} // namespace Muffin
