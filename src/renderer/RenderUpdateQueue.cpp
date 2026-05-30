#include "RenderUpdateQueue.h"

namespace Muffin {

namespace {

void appendUniqueNodeId(QVector<MarkdownNodeId>& nodeIds, MarkdownNodeId nodeId)
{
    if (nodeId == 0 || nodeIds.contains(nodeId)) {
        return;
    }
    nodeIds.append(nodeId);
}

SourceSpan combineSourceSpan(SourceSpan current, SourceSpan next)
{
    if (!next.isValid()) {
        return current;
    }
    if (!current.isValid()) {
        return next;
    }
    return {qMin(current.start, next.start), qMax(current.end, next.end)};
}

} // namespace

void RenderUpdateQueue::enqueue(RenderUpdateRequest request)
{
    QVector<MarkdownNodeId> uniqueNodeIds;
    uniqueNodeIds.reserve(request.nodeIds.size());
    for (MarkdownNodeId nodeId : request.nodeIds) {
        appendUniqueNodeId(uniqueNodeIds, nodeId);
    }
    if (uniqueNodeIds.isEmpty()) {
        return;
    }

    request.nodeIds = std::move(uniqueNodeIds);
    m_requests.append(std::move(request));
}

RenderUpdateBatch RenderUpdateQueue::drain()
{
    RenderUpdateBatch batch;
    for (const RenderUpdateRequest& request : m_requests) {
        for (MarkdownNodeId nodeId : request.nodeIds) {
            appendUniqueNodeId(batch.nodeIds, nodeId);
        }
        batch.combinedEditedSource = combineSourceSpan(batch.combinedEditedSource, request.editedSource);
        if (batch.preferredKind == RenderSpan::Kind::Unsupported
            && request.preferredKind != RenderSpan::Kind::Unsupported) {
            batch.preferredKind = request.preferredKind;
        }
        if (!request.reason.isEmpty()) {
            batch.reasons.append(request.reason);
        }
    }
    m_requests.clear();
    return batch;
}

} // namespace Muffin
