#include "IncrementalRenderValidationPolicy.h"

namespace Muffin {

namespace {

bool isSingleBlockReplacement(const PartialDocumentPatchPlan& plan,
                              const PartialRenderResult& partial,
                              RenderSpan::Kind kind)
{
    if (plan.steps.size() != 1 || plan.steps.first().kind != kind) {
        return false;
    }
    return partial.renderedNodeIds.size() == 1
        && partial.replacementRanges.size() == 1
        && partial.blocks.size() == 1
        && partial.blocks.first().kind == kind
        && partial.blocks.first().nodeId == plan.steps.first().nodeId;
}

QString reasonForTrustedSingleBlockKind(RenderSpan::Kind kind)
{
    switch (kind) {
    case RenderSpan::Kind::CodeBlock:
        return QStringLiteral("single-block-code-block");
    case RenderSpan::Kind::List:
        return QStringLiteral("single-block-list");
    case RenderSpan::Kind::BlockQuote:
        return QStringLiteral("single-block-block-quote");
    default:
        return QStringLiteral("single-block-replacement");
    }
}

IncrementalRenderValidationDecision autoDecisionForPatch(const PartialDocumentPatchPlan& plan,
                                                         const PartialRenderResult& partial)
{
    const RenderSpan::Kind kind = plan.steps.first().kind;
    if (kind == RenderSpan::Kind::Paragraph) {
        return {IncrementalRenderValidationMode::TrustLocalChecks, QStringLiteral("paragraph-local-checks")};
    }
    if (kind == RenderSpan::Kind::Heading) {
        return {IncrementalRenderValidationMode::TrustLocalChecks, QStringLiteral("heading-local-checks")};
    }
    if (kind == RenderSpan::Kind::CodeBlock
        || kind == RenderSpan::Kind::List
        || kind == RenderSpan::Kind::BlockQuote) {
        if (isSingleBlockReplacement(plan, partial, kind)) {
            return {IncrementalRenderValidationMode::TrustLocalChecks, reasonForTrustedSingleBlockKind(kind)};
        }
        return {IncrementalRenderValidationMode::AlwaysFullCompare,
                QStringLiteral("not-single-block-replacement")};
    }
    return {IncrementalRenderValidationMode::AlwaysFullCompare, QStringLiteral("unsupported-patch-kind")};
}

} // namespace

IncrementalRenderValidationDecision IncrementalRenderValidationPolicy::evaluate(
    IncrementalRenderValidationMode requestedMode,
    const PartialDocumentPatchPlan& plan,
    const PartialDocumentPatchDryRun& dryRun,
    const PartialRenderStateMergeResult& stateMerge,
    const PartialRenderResult& partial)
{
    if (requestedMode != IncrementalRenderValidationMode::Auto) {
        return {requestedMode, QStringLiteral("explicit-validation-mode")};
    }
    if (!plan.ok || !dryRun.ok || !stateMerge.ok || plan.steps.size() != 1) {
        return {IncrementalRenderValidationMode::AlwaysFullCompare,
                QStringLiteral("local-checks-incomplete")};
    }
    return autoDecisionForPatch(plan, partial);
}

IncrementalRenderValidationMode IncrementalRenderValidationPolicy::decide(
    IncrementalRenderValidationMode requestedMode,
    const PartialDocumentPatchPlan& plan,
    const PartialDocumentPatchDryRun& dryRun,
    const PartialRenderStateMergeResult& stateMerge,
    const PartialRenderResult& partial)
{
    return evaluate(requestedMode, plan, dryRun, stateMerge, partial).mode;
}

} // namespace Muffin
