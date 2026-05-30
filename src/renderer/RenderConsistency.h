#pragma once

#include "renderer/DocumentRenderer.h"

#include <QString>

namespace Muffin {

struct RenderConsistencyResult {
    bool ok = false;
    QString message;
};

class RenderConsistency {
public:
    static RenderConsistencyResult comparePartialToFull(const RenderResult& full,
                                                        const PartialRenderResult& partial);
};

} // namespace Muffin
