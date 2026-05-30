#include "RenderInvalidation.h"

namespace Muffin {

namespace {

bool isInlineNodeType(MarkdownNodeType type)
{
    switch (type) {
    case MarkdownNodeType::Text:
    case MarkdownNodeType::SoftBreak:
    case MarkdownNodeType::LineBreak:
    case MarkdownNodeType::Emphasis:
    case MarkdownNodeType::Strong:
    case MarkdownNodeType::InlineCode:
    case MarkdownNodeType::Link:
    case MarkdownNodeType::Image:
    case MarkdownNodeType::FormulaInline:
    case MarkdownNodeType::Strikethrough:
        return true;
    default:
        return false;
    }
}

void appendUnique(QVector<MarkdownNodeId>& nodeIds, MarkdownNodeId nodeId)
{
    if (nodeId != 0 && !nodeIds.contains(nodeId)) {
        nodeIds.append(nodeId);
    }
}

void appendStructuralContext(QVector<MarkdownNodeId>& nodeIds, const NodeSnapshot& snapshot)
{
    appendUnique(nodeIds, snapshot.parentId);
    appendUnique(nodeIds, snapshot.previousSiblingId);
    appendUnique(nodeIds, snapshot.nextSiblingId);
}

void appendRenderableNode(QVector<MarkdownNodeId>& nodeIds, const NodeSnapshot& snapshot)
{
    if (isInlineNodeType(snapshot.type) && snapshot.parentId != 0) {
        const MarkdownNodeId renderableParentId = snapshot.ancestorIds.size() > 2
            && snapshot.ancestorIds.first() == snapshot.parentId
            ? snapshot.ancestorIds.at(1)
            : snapshot.parentId;
        appendUnique(nodeIds, renderableParentId);
        return;
    }
    appendUnique(nodeIds, snapshot.nodeId);
}

void appendChangedNodeInvalidation(QVector<MarkdownNodeId>& nodeIds, const NodeSnapshot& snapshot)
{
    appendRenderableNode(nodeIds, snapshot);
    if (snapshot.type == MarkdownNodeType::ListItem || snapshot.type == MarkdownNodeType::List) {
        appendUnique(nodeIds, snapshot.parentId);
    }
}

} // namespace

QVector<MarkdownNodeId> RenderInvalidation::invalidatedNodeIdsForTransaction(const EditTransaction& transaction)
{
    QVector<MarkdownNodeId> nodeIds;

    if (transaction.kind == EditTransactionKind::ListIndent || transaction.kind == EditTransactionKind::ListOutdent) {
        for (const NodeSnapshot& snapshot : transaction.beforeNodes) {
            appendStructuralContext(nodeIds, snapshot);
        }
        for (const NodeSnapshot& snapshot : transaction.afterNodes) {
            appendStructuralContext(nodeIds, snapshot);
        }
    } else {
        for (const NodeSnapshot& snapshot : transaction.beforeNodes) {
            appendChangedNodeInvalidation(nodeIds, snapshot);
        }
        for (const NodeSnapshot& snapshot : transaction.afterNodes) {
            appendChangedNodeInvalidation(nodeIds, snapshot);
        }
    }

    if (nodeIds.isEmpty()) {
        for (MarkdownNodeId nodeId : transaction.affectedNodeIds) {
            appendUnique(nodeIds, nodeId);
        }
    }

    return nodeIds;
}

} // namespace Muffin
