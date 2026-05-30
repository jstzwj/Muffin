#include "PartialDocumentPatchPlanner.h"

namespace Muffin {

namespace {

const MarkdownBlock* blockForNode(const QVector<MarkdownBlock>& blocks, MarkdownNodeId nodeId)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.nodeId == nodeId) {
            return &block;
        }
    }
    return nullptr;
}

bool blocksAreOrdered(const QVector<MarkdownBlock>& blocks)
{
    int previousEnd = -1;
    for (const MarkdownBlock& block : blocks) {
        if (!block.hasRenderedRange()) {
            return false;
        }
        if (previousEnd > block.effectiveReplacementRenderedStart()) {
            return false;
        }
        previousEnd = block.effectiveReplacementRenderedEnd();
    }
    return true;
}

} // namespace

PartialDocumentPatchPlan PartialDocumentPatchPlanner::plan(const PartialRenderResult& partial)
{
    PartialDocumentPatchPlan plan;

    if (partial.renderedNodeIds.isEmpty()) {
        plan.ok = partial.replacementRanges.isEmpty() && partial.blocks.isEmpty();
        if (!plan.ok) {
            plan.errors.append(QStringLiteral("Partial render has replacement data without rendered nodes."));
        }
        return plan;
    }

    if (partial.replacementRanges.size() == 1 && partial.blocks.size() > 1) {
        const PartialReplacementRange& replacement = partial.replacementRanges.first();
        const MarkdownBlock* aggregateBlock = blockForNode(partial.blocks, replacement.nodeId);
        if (aggregateBlock && replacement.kind == aggregateBlock->kind && replacement.hasRenderedRange()
            && aggregateBlock->hasRenderedRange()) {
            plan.steps.append({replacement.nodeId,
                               replacement.renderedStart,
                               replacement.renderedEnd,
                               aggregateBlock->effectiveReplacementRenderedStart(),
                               aggregateBlock->effectiveReplacementRenderedEnd(),
                               aggregateBlock->source,
                               aggregateBlock->sourceRange,
                               aggregateBlock->kind});
            plan.ok = true;
            return plan;
        }

        if (!replacement.hasRenderedRange() || !blocksAreOrdered(partial.blocks)) {
            plan.errors.append(QStringLiteral("Invalid rendered range for multi-block replacement node %1.")
                                   .arg(replacement.nodeId));
            return plan;
        }
        const MarkdownBlock& firstBlock = partial.blocks.first();
        const MarkdownBlock& lastBlock = partial.blocks.last();
        plan.steps.append({replacement.nodeId,
                           replacement.renderedStart,
                           replacement.renderedEnd,
                           firstBlock.effectiveReplacementRenderedStart(),
                           lastBlock.effectiveReplacementRenderedEnd(),
                           replacement.source,
                           replacement.sourceRange,
                           replacement.kind});
        plan.ok = true;
        return plan;
    }

    if (partial.replacementRanges.size() != partial.blocks.size()) {
        plan.errors.append(QStringLiteral("Replacement range count does not match partial block count."));
        return plan;
    }

    for (const PartialReplacementRange& replacement : partial.replacementRanges) {
        const MarkdownBlock* block = blockForNode(partial.blocks, replacement.nodeId);
        if (!block) {
            plan.errors.append(QStringLiteral("Missing partial block for replacement node %1.").arg(replacement.nodeId));
            continue;
        }
        if (!replacement.hasRenderedRange() || !block->hasRenderedRange()) {
            plan.errors.append(QStringLiteral("Invalid rendered range for replacement node %1.").arg(replacement.nodeId));
            continue;
        }
        if (replacement.kind != block->kind) {
            plan.errors.append(QStringLiteral("Replacement metadata mismatch for node %1.").arg(replacement.nodeId));
            continue;
        }

        plan.steps.append({replacement.nodeId,
                           replacement.renderedStart,
                           replacement.renderedEnd,
                           block->effectiveReplacementRenderedStart(),
                           block->effectiveReplacementRenderedEnd(),
                           block->source,
                           block->sourceRange,
                           block->kind});
    }

    plan.ok = plan.errors.isEmpty() && plan.steps.size() == partial.replacementRanges.size();
    return plan;
}

} // namespace Muffin
