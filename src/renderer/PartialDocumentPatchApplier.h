#pragma once

#include "renderer/PartialDocumentPatchPlanner.h"

#include <QStringList>
#include <memory>

class QTextDocument;

namespace Muffin {

struct PartialDocumentPatchDryRun {
    bool ok = false;
    QStringList errors;
};

struct PartialDocumentPatchApplyResult {
    bool ok = false;
    QStringList errors;
    std::unique_ptr<QTextDocument> document;
};

class PartialDocumentPatchApplier {
public:
    static PartialDocumentPatchDryRun dryRun(const PartialDocumentPatchPlan& plan, int currentRenderedLength);
    static PartialDocumentPatchApplyResult applyRangeReplacement(const QTextDocument& currentDocument,
                                                                 const QTextDocument& partialDocument,
                                                                 const PartialDocumentPatchPlan& plan);
};

} // namespace Muffin
