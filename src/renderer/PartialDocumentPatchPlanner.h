#pragma once

#include "renderer/PartialRenderResult.h"

#include <QStringList>

namespace Muffin {

struct PartialDocumentPatchStep {
    MarkdownNodeId nodeId = 0;
    int oldRenderedStart = -1;
    int oldRenderedEnd = -1;
    int newRenderedStart = -1;
    int newRenderedEnd = -1;
    SourceSpan source;
    SourceRange sourceRange;
    RenderSpan::Kind kind = RenderSpan::Kind::Unsupported;
};

struct PartialDocumentPatchPlan {
    bool ok = false;
    QStringList errors;
    QVector<PartialDocumentPatchStep> steps;
};

class PartialDocumentPatchPlanner {
public:
    static PartialDocumentPatchPlan plan(const PartialRenderResult& partial);
};

} // namespace Muffin
