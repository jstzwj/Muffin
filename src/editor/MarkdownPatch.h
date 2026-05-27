#pragma once

#include "renderer/RenderSourceMap.h"
#include <QString>

namespace Muffin {

struct RenderedEdit {
    int renderedStart = -1;
    int renderedEnd = -1;
    QString replacement;
};

struct PatchResult {
    bool ok = false;
    QString text;
    int cursorSourceOffset = -1;
    QString error;
};

class MarkdownPatch {
public:
    static PatchResult applyRenderedEdit(const QString& markdown,
                                         const RenderSourceMap& sourceMap,
                                         const RenderedEdit& edit);
};

} // namespace Muffin
