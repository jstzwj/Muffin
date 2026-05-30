#pragma once

#include "model/MarkdownNode.h"
#include "parser/SourceSpan.h"

namespace Muffin {

struct SyntaxTokenSpan {
    enum class Kind {
        EmphasisMarker,
        StrongMarker,
        InlineCodeMarker
    };

    SourceSpan source;
    Kind kind = Kind::EmphasisMarker;
    MarkdownNodeId nodeId = 0;
};

} // namespace Muffin
