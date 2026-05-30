#pragma once

#include "editor/EditTransaction.h"
#include "editor/EditTransactionApplier.h"
#include "editor/EditTransactionValidator.h"
#include "model/MarkdownDocument.h"

#include <QString>
#include <optional>

namespace Muffin {

struct StructuralUndoDecision {
    bool structurallyApplicable = false;
    bool shouldApply = false;
    std::optional<EditTransactionApplyResult> dryRunResult;
};

class StructuralUndoController {
public:
    static bool canApplyStructurally(const EditTransaction& transaction,
                                     const std::optional<EditTransactionValidationResult>& validation);
    static StructuralUndoDecision evaluate(const MarkdownDocument& document,
                                           const EditTransaction& transaction,
                                           const std::optional<EditTransactionValidationResult>& validation,
                                           EditTransactionApplyDirection direction,
                                           const QString& expectedMarkdown);

private:
    static bool isStructurallySupportedOperation(EditOperationKind kind);
    static bool hasOnlySafeUpdateLiteralOperations(const EditTransaction& transaction);
    static bool hasOnlySafeListMoveOperations(const EditTransaction& transaction);
    static bool hasOnlySafeIntentOperations(const EditTransaction& transaction);
};

} // namespace Muffin
