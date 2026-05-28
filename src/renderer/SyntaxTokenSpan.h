#pragma once

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
};

} // namespace Muffin
