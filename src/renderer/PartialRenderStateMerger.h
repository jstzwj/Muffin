#pragma once

#include "renderer/PartialDocumentPatchPlanner.h"
#include "renderer/PartialRenderResult.h"

#include <QStringList>

namespace Muffin {

struct PartialRenderStateMergeResult {
    bool ok = false;
    QStringList errors;
    RenderSourceMap sourceMap;
    QVector<MarkdownBlock> blocks;
    QVector<SyntaxTokenSpan> syntaxTokens;
    QVector<RenderFragment> fragments;
};

class PartialRenderStateMerger {
public:
    static PartialRenderStateMergeResult merge(const RenderSourceMap& previousSourceMap,
                                               const QVector<MarkdownBlock>& previousBlocks,
                                               const QVector<SyntaxTokenSpan>& previousSyntaxTokens,
                                               const QVector<RenderFragment>& previousFragments,
                                               const PartialRenderResult& partial,
                                               const PartialDocumentPatchPlan& plan);
};

} // namespace Muffin
