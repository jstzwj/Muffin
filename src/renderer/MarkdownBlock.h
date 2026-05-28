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

    bool hasRenderedRange() const { return renderedStart >= 0 && renderedEnd >= renderedStart; }
    bool containsRenderedPosition(int position) const { return position >= renderedStart && position <= renderedEnd; }
};

} // namespace Muffin
