#include "EditTransactionApplier.h"

#include "model/MarkdownSerializer.h"
#include "model/MarkdownTransform.h"

namespace Muffin {

namespace {

QString directionName(EditTransactionApplyDirection direction)
{
    return direction == EditTransactionApplyDirection::Forward
        ? QStringLiteral("forward")
        : QStringLiteral("reverse");
}

QString expectedMarkdownForDirection(const EditTransaction& transaction,
                                     EditTransactionApplyDirection direction)
{
    return direction == EditTransactionApplyDirection::Forward
        ? transaction.afterMarkdown
        : transaction.beforeMarkdown;
}

const NodeSnapshot& expectedCurrentNode(const EditOperation& operation,
                                        EditTransactionApplyDirection direction)
{
    return direction == EditTransactionApplyDirection::Forward
        ? operation.beforeNode
        : operation.afterNode;
}

const NodeSnapshot& replacementNode(const EditOperation& operation,
                                    EditTransactionApplyDirection direction)
{
    return direction == EditTransactionApplyDirection::Forward
        ? operation.afterNode
        : operation.beforeNode;
}

bool applyUpdateLiteral(MarkdownDocument& document,
                        const EditOperation& operation,
                        EditTransactionApplyDirection direction,
                        QStringList& errors)
{
    const MarkdownNode* node = document.nodeById(operation.nodeId);
    if (!node) {
        errors.append(QStringLiteral("Cannot apply %1 UpdateLiteral: node %2 not found")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }

    const NodeSnapshot& expectedCurrent = expectedCurrentNode(operation, direction);
    if (expectedCurrent.nodeId != 0 && expectedCurrent.nodeId != operation.nodeId) {
        errors.append(QStringLiteral("Cannot apply %1 UpdateLiteral: snapshot node id mismatch for node %2")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    if (expectedCurrent.type != MarkdownNodeType::Unknown && node->type != expectedCurrent.type) {
        errors.append(QStringLiteral("Cannot apply %1 UpdateLiteral: node %2 type mismatch")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    if (node->literal != expectedCurrent.literal) {
        errors.append(QStringLiteral("Cannot apply %1 UpdateLiteral: node %2 literal mismatch")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }

    document = MarkdownTransform::replaceNodeLiteral(document, operation.nodeId,
                                                     replacementNode(operation, direction).literal);
    return true;
}

bool validateMoveNodeOperation(const MarkdownDocument& document,
                               const EditOperation& operation,
                               EditTransactionApplyDirection direction,
                               QStringList& errors)
{
    const MarkdownNode* node = document.nodeById(operation.nodeId);
    if (!node) {
        errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 not found")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }

    const NodeSnapshot& expectedCurrent = expectedCurrentNode(operation, direction);
    const NodeSnapshot& replacement = replacementNode(operation, direction);
    if (expectedCurrent.nodeId != operation.nodeId || replacement.nodeId != operation.nodeId) {
        errors.append(QStringLiteral("Cannot apply %1 MoveNode: snapshot node id mismatch for node %2")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    if (expectedCurrent.type != MarkdownNodeType::ListItem || replacement.type != MarkdownNodeType::ListItem
        || node->type != MarkdownNodeType::ListItem) {
        errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 is not a list item move")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    if (expectedCurrent.parentId == replacement.parentId) {
        errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 parent did not change")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    if (expectedCurrent.parentId != 0 && node->parent != expectedCurrent.parentId) {
        errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 parent mismatch")
                          .arg(directionName(direction))
                          .arg(operation.nodeId));
        return false;
    }
    return true;
}

std::optional<MarkdownDocument> applyListMoveNodes(const MarkdownDocument& document,
                                                   const EditTransaction& transaction,
                                                   EditTransactionApplyDirection direction,
                                                   QStringList& errors)
{
    QVector<MarkdownNodeId> itemIds;
    itemIds.reserve(transaction.operations.size());
    for (const EditOperation& operation : transaction.operations) {
        if (operation.kind != EditOperationKind::MoveNode) {
            errors.append(QStringLiteral("Cannot apply %1 transaction: mixed MoveNode operations")
                              .arg(directionName(direction)));
            return {};
        }
        if (!validateMoveNodeOperation(document, operation, direction, errors)) {
            return {};
        }
        itemIds.append(operation.nodeId);
    }

    const bool demote = (transaction.kind == EditTransactionKind::ListIndent
                         && direction == EditTransactionApplyDirection::Forward)
        || (transaction.kind == EditTransactionKind::ListOutdent
            && direction == EditTransactionApplyDirection::Reverse);
    const bool promote = (transaction.kind == EditTransactionKind::ListOutdent
                          && direction == EditTransactionApplyDirection::Forward)
        || (transaction.kind == EditTransactionKind::ListIndent
            && direction == EditTransactionApplyDirection::Reverse);
    if (demote) {
        return MarkdownTransform::demoteListItems(document, itemIds);
    }
    if (promote) {
        return MarkdownTransform::promoteListItems(document, itemIds);
    }

    errors.append(QStringLiteral("Cannot apply %1 MoveNode transaction: unsupported transaction kind")
                      .arg(directionName(direction)));
    return {};
}

} // namespace

EditTransactionApplyResult EditTransactionApplier::applyForwardDryRun(const MarkdownDocument& document,
                                                                      const EditTransaction& transaction)
{
    return applyDryRun(document, transaction, EditTransactionApplyDirection::Forward);
}

EditTransactionApplyResult EditTransactionApplier::applyReverseDryRun(const MarkdownDocument& document,
                                                                      const EditTransaction& transaction)
{
    return applyDryRun(document, transaction, EditTransactionApplyDirection::Reverse);
}

EditTransactionApplyResult EditTransactionApplier::applyDryRun(const MarkdownDocument& document,
                                                               const EditTransaction& transaction,
                                                               EditTransactionApplyDirection direction)
{
    EditTransactionApplyResult result;
    result.document = document;

    if (transaction.operations.isEmpty()) {
        result.errors.append(QStringLiteral("Cannot apply %1 transaction without operations")
                                 .arg(directionName(direction)));
        return result;
    }

    const bool isListMove = (transaction.kind == EditTransactionKind::ListIndent
                             || transaction.kind == EditTransactionKind::ListOutdent)
        && transaction.operations.first().kind == EditOperationKind::MoveNode;

    if (isListMove) {
        const std::optional<MarkdownDocument> moved = applyListMoveNodes(result.document, transaction, direction, result.errors);
        if (!moved) {
            return result;
        }
        result.document = *moved;
    } else {
    for (const EditOperation& operation : transaction.operations) {
        switch (operation.kind) {
        case EditOperationKind::UpdateLiteral:
            if (!applyUpdateLiteral(result.document, operation, direction, result.errors)) {
                return result;
            }
            break;
        case EditOperationKind::InsertNode: {
            if (direction == EditTransactionApplyDirection::Forward) {
                if (operation.beforeNode.nodeId != 0) {
                    result.errors.append(QStringLiteral("Cannot apply forward InsertNode: before snapshot should be empty"));
                    return result;
                }
                if (operation.afterNode.nodeId != operation.nodeId) {
                    result.errors.append(QStringLiteral("Cannot apply forward InsertNode: snapshot node id mismatch"));
                    return result;
                }
                MarkdownNode newNode;
                newNode.id = operation.afterNode.nodeId;
                newNode.type = operation.afterNode.type;
                newNode.parent = operation.afterNode.parentId;
                newNode.literal = operation.afterNode.literal;
                const auto transformResult = MarkdownTransform::insertNode(
                    std::move(result.document), operation.afterNode.parentId,
                    operation.afterNode.previousSiblingId ? -1 : 0,
                    std::move(newNode));
                result.document = std::move(transformResult.document);
            } else {
                // Reverse: remove the node that was inserted
                const MarkdownNode* existingNode = result.document.nodeById(operation.nodeId);
                if (!existingNode) {
                    result.errors.append(QStringLiteral("Cannot apply reverse InsertNode: node %1 not found")
                                             .arg(operation.nodeId));
                    return result;
                }
                const auto transformResult = MarkdownTransform::removeNode(std::move(result.document), operation.nodeId);
                result.document = std::move(transformResult.document);
            }
            break;
        }
        case EditOperationKind::RemoveNode: {
            if (direction == EditTransactionApplyDirection::Forward) {
                if (operation.afterNode.nodeId != 0) {
                    result.errors.append(QStringLiteral("Cannot apply forward RemoveNode: after snapshot should be empty"));
                    return result;
                }
                const MarkdownNode* existingNode = result.document.nodeById(operation.nodeId);
                if (!existingNode) {
                    result.errors.append(QStringLiteral("Cannot apply forward RemoveNode: node %1 not found")
                                             .arg(operation.nodeId));
                    return result;
                }
                if (operation.beforeNode.type != MarkdownNodeType::Unknown && existingNode->type != operation.beforeNode.type) {
                    result.errors.append(QStringLiteral("Cannot apply forward RemoveNode: node %1 type mismatch")
                                             .arg(operation.nodeId));
                    return result;
                }
                const auto transformResult = MarkdownTransform::removeNode(std::move(result.document), operation.nodeId);
                result.document = std::move(transformResult.document);
            } else {
                // Reverse: re-insert the node that was removed
                if (operation.beforeNode.nodeId != operation.nodeId) {
                    result.errors.append(QStringLiteral("Cannot apply reverse RemoveNode: snapshot node id mismatch"));
                    return result;
                }
                MarkdownNode newNode;
                newNode.id = operation.beforeNode.nodeId;
                newNode.type = operation.beforeNode.type;
                newNode.parent = operation.beforeNode.parentId;
                newNode.literal = operation.beforeNode.literal;
                const auto transformResult = MarkdownTransform::insertNode(
                    std::move(result.document), operation.beforeNode.parentId,
                    operation.beforeNode.previousSiblingId ? -1 : 0,
                    std::move(newNode));
                result.document = std::move(transformResult.document);
            }
            break;
        }
        case EditOperationKind::MoveNode: {
            const MarkdownNode* existingNode = result.document.nodeById(operation.nodeId);
            if (!existingNode) {
                result.errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 not found")
                                         .arg(directionName(direction)).arg(operation.nodeId));
                return result;
            }
            const NodeSnapshot& expectedCurrent = expectedCurrentNode(operation, direction);
            const NodeSnapshot& replacement = replacementNode(operation, direction);
            if (expectedCurrent.type != MarkdownNodeType::Unknown && existingNode->type != expectedCurrent.type) {
                result.errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 type mismatch")
                                         .arg(directionName(direction)).arg(operation.nodeId));
                return result;
            }
            if (expectedCurrent.parentId != 0 && existingNode->parent != expectedCurrent.parentId) {
                result.errors.append(QStringLiteral("Cannot apply %1 MoveNode: node %2 parent mismatch")
                                         .arg(directionName(direction)).arg(operation.nodeId));
                return result;
            }
            const auto transformResult = MarkdownTransform::reparentNode(
                std::move(result.document), operation.nodeId, replacement.parentId,
                replacement.previousSiblingId ? -1 : 0);
            result.document = std::move(transformResult.document);
            break;
        }
        default:
            result.errors.append(QStringLiteral("Cannot apply %1 transaction: unsupported operation kind")
                                     .arg(directionName(direction)));
            return result;
        }
    }
    }

    MarkdownSerializer serializer;
    result.markdown = serializer.serializeDocument(result.document);
    const QString expectedMarkdown = expectedMarkdownForDirection(transaction, direction);
    result.matchesExpectedMarkdown = expectedMarkdown.isEmpty() || result.markdown == expectedMarkdown;
    result.ok = result.matchesExpectedMarkdown;
    if (!result.ok) {
        result.errors.append(QStringLiteral("Cannot apply %1 transaction: serialized markdown mismatch")
                                 .arg(directionName(direction)));
    }
    return result;
}

} // namespace Muffin
