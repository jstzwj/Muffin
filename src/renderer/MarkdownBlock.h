#pragma once

#include "renderer/RenderSourceMap.h"

namespace Muffin {

struct MarkdownBlock {
    SourceSpan source;
    SourceSpan content;
    SourceRange sourceRange;
    int renderedStart = -1;
    int renderedEnd = -1;
    RenderSpan::Kind kind = RenderSpan::Kind::Unsupported;
    bool editable = false;
    MarkdownNodeId nodeId = 0;
    int replacementRenderedStart = -1;
    int replacementRenderedEnd = -1;

    bool hasRenderedRange() const { return renderedStart >= 0 && renderedEnd >= renderedStart; }
    bool hasReplacementRenderedRange() const
    {
        return replacementRenderedStart >= 0 && replacementRenderedEnd >= replacementRenderedStart;
    }
    int effectiveReplacementRenderedStart() const
    {
        return hasReplacementRenderedRange() ? replacementRenderedStart : renderedStart;
    }
    int effectiveReplacementRenderedEnd() const
    {
        return hasReplacementRenderedRange() ? replacementRenderedEnd : renderedEnd;
    }
    bool containsRenderedPosition(int position) const { return position >= renderedStart && position <= renderedEnd; }
};

} // namespace Muffin
