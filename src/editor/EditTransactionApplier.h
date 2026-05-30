#pragma once

#include "editor/EditTransaction.h"
#include "model/MarkdownDocument.h"

#include <QString>
#include <QStringList>

namespace Muffin {

enum class EditTransactionApplyDirection {
    Forward,
    Reverse
};

struct EditTransactionApplyResult {
    bool ok = false;
    bool matchesExpectedMarkdown = false;
    QStringList errors;
    MarkdownDocument document;
    QString markdown;
};

class EditTransactionApplier {
public:
    static EditTransactionApplyResult applyForwardDryRun(const MarkdownDocument& document,
                                                         const EditTransaction& transaction);
    static EditTransactionApplyResult applyReverseDryRun(const MarkdownDocument& document,
                                                         const EditTransaction& transaction);

private:
    static EditTransactionApplyResult applyDryRun(const MarkdownDocument& document,
                                                  const EditTransaction& transaction,
                                                  EditTransactionApplyDirection direction);
};

} // namespace Muffin
