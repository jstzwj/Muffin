#pragma once

#include "renderer/RenderSourceMap.h"
#include "renderer/SyntaxTokenSpan.h"

namespace Muffin {

struct RenderFragment {
    enum class Kind {
        Content,
        Marker
    };

    MarkdownNodeId nodeId = 0;
    SourceSpan source;
    RenderSpan rendered;
    Kind kind = Kind::Content;
    SyntaxTokenSpan::Kind markerKind = SyntaxTokenSpan::Kind::EmphasisMarker;
    bool visible = true;
    bool editable = false;

    bool isMarker() const { return kind == Kind::Marker; }
};

} // namespace Muffin
