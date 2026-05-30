#pragma once

#include "renderer/MarkdownBlock.h"
#include "renderer/RenderFragment.h"
#include "renderer/RenderSourceMap.h"
#include "renderer/SyntaxTokenSpan.h"

#include <memory>
#include <QVector>

class QTextDocument;

namespace Muffin {

struct PartialReplacementRange {
    MarkdownNodeId nodeId = 0;
    int renderedStart = -1;
    int renderedEnd = -1;
    int contentRenderedStart = -1;
    int contentRenderedEnd = -1;
    SourceSpan source;
    SourceRange sourceRange;
    RenderSpan::Kind kind = RenderSpan::Kind::Unsupported;

    bool hasRenderedRange() const { return renderedStart >= 0 && renderedEnd >= renderedStart; }
    bool hasContentRenderedRange() const { return contentRenderedStart >= 0 && contentRenderedEnd >= contentRenderedStart; }
};

struct PartialRenderResult {
    std::shared_ptr<QTextDocument> document;
    QVector<MarkdownNodeId> renderedNodeIds;
    QVector<PartialReplacementRange> replacementRanges;
    RenderSourceMap sourceMap;
    QVector<MarkdownBlock> blocks;
    QVector<SyntaxTokenSpan> syntaxTokens;
    QVector<RenderFragment> fragments;
};

} // namespace Muffin
