#pragma once

#include "renderer/PartialDocumentPatchApplier.h"
#include "renderer/PartialRenderResult.h"
#include "renderer/PartialRenderStateMerger.h"

#include <QString>

namespace Muffin {

enum class IncrementalRenderValidationMode {
    Auto,
    AlwaysFullCompare,
    TrustLocalChecks
};

struct IncrementalRenderValidationDecision {
    IncrementalRenderValidationMode mode = IncrementalRenderValidationMode::Auto;
    QString reason;
};

class IncrementalRenderValidationPolicy {
public:
    static IncrementalRenderValidationDecision evaluate(IncrementalRenderValidationMode requestedMode,
                                                        const PartialDocumentPatchPlan& plan,
                                                        const PartialDocumentPatchDryRun& dryRun,
                                                        const PartialRenderStateMergeResult& stateMerge,
                                                        const PartialRenderResult& partial = {});
    static IncrementalRenderValidationMode decide(IncrementalRenderValidationMode requestedMode,
                                                  const PartialDocumentPatchPlan& plan,
                                                  const PartialDocumentPatchDryRun& dryRun,
                                                  const PartialRenderStateMergeResult& stateMerge,
                                                  const PartialRenderResult& partial = {});
};

} // namespace Muffin
