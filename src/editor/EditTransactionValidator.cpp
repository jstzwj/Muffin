#include "EditTransactionValidator.h"

namespace Muffin {

namespace {

void addError(QStringList& errors, const QString& message)
{
    errors.append(message);
}

bool containsNodeId(const QVector<MarkdownNodeId>& nodeIds, MarkdownNodeId nodeId)
{
    return nodeIds.contains(nodeId);
}

bool hasDuplicateSnapshotNodeIds(const QVector<NodeSnapshot>& snapshots)
{
    QVector<MarkdownNodeId> seen;
    for (const NodeSnapshot& snapshot : snapshots) {
        if (snapshot.nodeId == 0) {
            continue;
        }
        if (seen.contains(snapshot.nodeId)) {
            return true;
        }
        seen.append(snapshot.nodeId);
    }
    return false;
}

bool nodeStructureChanged(const NodeSnapshot& before, const NodeSnapshot& after)
{
    return before.parentId != after.parentId
        || before.previousSiblingId != after.previousSiblingId
        || before.nextSiblingId != after.nextSiblingId
        || before.ancestorIds != after.ancestorIds
        || before.childIds != after.childIds;
}

} // namespace

EditTransactionValidationResult EditTransactionValidator::validate(const EditTransaction& transaction)
{
    QStringList errors;

    if (hasDuplicateSnapshotNodeIds(transaction.beforeNodes)) {
        addError(errors, QStringLiteral("Duplicate before node snapshot."));
    }
    if (hasDuplicateSnapshotNodeIds(transaction.afterNodes)) {
        addError(errors, QStringLiteral("Duplicate after node snapshot."));
    }

    for (const EditOperation& operation : transaction.operations) {
        if (operation.nodeId == 0) {
            addError(errors, QStringLiteral("Operation nodeId is zero."));
            continue;
        }
        if (!containsNodeId(transaction.affectedNodeIds, operation.nodeId)) {
            addError(errors, QStringLiteral("Operation node %1 is not affected.").arg(operation.nodeId));
        }

        switch (operation.kind) {
        case EditOperationKind::UpdateLiteral:
            if (operation.beforeNode.nodeId != operation.nodeId || operation.afterNode.nodeId != operation.nodeId) {
                addError(errors, QStringLiteral("UpdateLiteral node snapshot id mismatch for node %1.").arg(operation.nodeId));
            }
            if (operation.beforeNode.literal == operation.afterNode.literal) {
                addError(errors, QStringLiteral("UpdateLiteral literal did not change for node %1.").arg(operation.nodeId));
            }
            break;
        case EditOperationKind::MoveNode:
            if (operation.beforeNode.nodeId != operation.nodeId || operation.afterNode.nodeId != operation.nodeId) {
                addError(errors, QStringLiteral("MoveNode node snapshot id mismatch for node %1.").arg(operation.nodeId));
            }
            if (!nodeStructureChanged(operation.beforeNode, operation.afterNode)) {
                addError(errors, QStringLiteral("MoveNode structure did not change for node %1.").arg(operation.nodeId));
            }
            break;
        case EditOperationKind::InsertNode:
            if (operation.beforeNode.nodeId != 0) {
                addError(errors, QStringLiteral("InsertNode has before snapshot for node %1.").arg(operation.nodeId));
            }
            if (operation.afterNode.nodeId != operation.nodeId) {
                addError(errors, QStringLiteral("InsertNode after snapshot id mismatch for node %1.").arg(operation.nodeId));
            }
            break;
        case EditOperationKind::RemoveNode:
            if (operation.beforeNode.nodeId != operation.nodeId) {
                addError(errors, QStringLiteral("RemoveNode before snapshot id mismatch for node %1.").arg(operation.nodeId));
            }
            if (operation.afterNode.nodeId != 0) {
                addError(errors, QStringLiteral("RemoveNode has after snapshot for node %1.").arg(operation.nodeId));
            }
            break;
        case EditOperationKind::UpdateAttributes:
        case EditOperationKind::Unknown:
            addError(errors, QStringLiteral("Unsupported operation kind for node %1.").arg(operation.nodeId));
            break;
        }
    }

    return {errors.isEmpty(), errors};
}

} // namespace Muffin
