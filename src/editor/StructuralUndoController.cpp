#include "StructuralUndoController.h"

namespace Muffin {

bool StructuralUndoController::canApplyStructurally(const EditTransaction& transaction,
                                                    const std::optional<EditTransactionValidationResult>& validation)
{
    if (!validation || !validation->ok || transaction.operations.isEmpty()) {
        return false;
    }
    if (transaction.beforeMarkdown.isEmpty() || transaction.afterMarkdown.isEmpty()) {
        return false;
    }
    for (const EditOperation& operation : transaction.operations) {
        if (!isStructurallySupportedOperation(operation.kind)) {
            return false;
        }
    }
    return true;
}

StructuralUndoDecision StructuralUndoController::evaluate(const MarkdownDocument& document,
                                                          const EditTransaction& transaction,
                                                          const std::optional<EditTransactionValidationResult>& validation,
                                                          EditTransactionApplyDirection direction,
                                                          const QString& expectedMarkdown)
{
    StructuralUndoDecision decision;
    decision.structurallyApplicable = canApplyStructurally(transaction, validation);
    if (!decision.structurallyApplicable) {
        return decision;
    }

    decision.dryRunResult = direction == EditTransactionApplyDirection::Forward
        ? EditTransactionApplier::applyForwardDryRun(document, transaction)
        : EditTransactionApplier::applyReverseDryRun(document, transaction);
    const EditTransactionApplyResult& dryRun = *decision.dryRunResult;
    decision.shouldApply = (hasOnlySafeUpdateLiteralOperations(transaction)
                           || hasOnlySafeListMoveOperations(transaction)
                           || hasOnlySafeIntentOperations(transaction))
        && dryRun.ok
        && dryRun.markdown == expectedMarkdown;
    return decision;
}

bool StructuralUndoController::isStructurallySupportedOperation(EditOperationKind kind)
{
    return kind == EditOperationKind::UpdateLiteral
        || kind == EditOperationKind::MoveNode
        || kind == EditOperationKind::InsertNode
        || kind == EditOperationKind::RemoveNode;
}

bool StructuralUndoController::hasOnlySafeUpdateLiteralOperations(const EditTransaction& transaction)
{
    if (transaction.operations.isEmpty()) {
        return false;
    }
    for (const EditOperation& operation : transaction.operations) {
        if (operation.kind != EditOperationKind::UpdateLiteral) {
            return false;
        }
        if (operation.beforeNode.type == MarkdownNodeType::FormulaBlock
            || operation.afterNode.type == MarkdownNodeType::FormulaBlock) {
            return false;
        }
    }
    return true;
}

bool StructuralUndoController::hasOnlySafeListMoveOperations(const EditTransaction& transaction)
{
    if (transaction.kind != EditTransactionKind::ListIndent && transaction.kind != EditTransactionKind::ListOutdent) {
        return false;
    }
    if (transaction.operations.isEmpty()) {
        return false;
    }
    for (const EditOperation& operation : transaction.operations) {
        if (operation.kind != EditOperationKind::MoveNode
            || operation.beforeNode.type != MarkdownNodeType::ListItem
            || operation.afterNode.type != MarkdownNodeType::ListItem
            || operation.beforeNode.parentId == operation.afterNode.parentId) {
            return false;
        }
    }
    return true;
}

bool StructuralUndoController::hasOnlySafeIntentOperations(const EditTransaction& transaction)
{
    switch (transaction.intent) {
    case EditIntent::FormatToggle:
    case EditIntent::HeadingChange:
    case EditIntent::ListToggle:
    case EditIntent::QuoteToggle:
    case EditIntent::SplitBlock:
    case EditIntent::MergeBlock:
        break;
    default:
        return false;
    }

    if (transaction.operations.isEmpty()) {
        return false;
    }
    for (const EditOperation& operation : transaction.operations) {
        if (operation.kind != EditOperationKind::UpdateLiteral
            && operation.kind != EditOperationKind::InsertNode
            && operation.kind != EditOperationKind::RemoveNode
            && operation.kind != EditOperationKind::MoveNode) {
            return false;
        }
    }
    return true;
}

} // namespace Muffin
