#include "RenderConsistency.h"

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

bool sameSpanExceptRenderedRange(const RenderSpan& left, const RenderSpan& right)
{
    return left.source.start == right.source.start
        && left.source.end == right.source.end
        && left.kind == right.kind
        && left.editable == right.editable
        && left.block == right.block
        && left.editSource.start == right.editSource.start
        && left.editSource.end == right.editSource.end
        && left.editPolicy == right.editPolicy
        && left.nodeId == right.nodeId;
}

} // namespace

RenderConsistencyResult RenderConsistency::comparePartialToFull(const RenderResult& full,
                                                                const PartialRenderResult& partial)
{
    for (const MarkdownBlock& partialBlock : partial.blocks) {
        const MarkdownBlock* fullBlock = blockForNode(full.blocks, partialBlock.nodeId);
        if (!fullBlock) {
            return {false, QStringLiteral("Missing full block for node %1.").arg(partialBlock.nodeId)};
        }
        if (fullBlock->kind != partialBlock.kind
            || fullBlock->source.start != partialBlock.source.start
            || fullBlock->source.end != partialBlock.source.end
            || fullBlock->content.start != partialBlock.content.start
            || fullBlock->content.end != partialBlock.content.end
            || fullBlock->editable != partialBlock.editable) {
            return {false, QStringLiteral("Block mismatch for node %1.").arg(partialBlock.nodeId)};
        }
    }

    for (const RenderSpan& partialSpan : partial.sourceMap.spans()) {
        if (partialSpan.nodeId == 0) {
            continue;
        }

        const std::optional<RenderSpan> fullSpan = full.sourceMap.spanForNode(partialSpan.nodeId);
        if (!fullSpan) {
            return {false, QStringLiteral("Missing full span for node %1.").arg(partialSpan.nodeId)};
        }
        if (!sameSpanExceptRenderedRange(*fullSpan, partialSpan)) {
            return {false, QStringLiteral("Span mismatch for node %1.").arg(partialSpan.nodeId)};
        }
    }

    return {true, {}};
}

} // namespace Muffin
