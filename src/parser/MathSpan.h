#pragma once

#include "parser/SourceSpan.h"
#include <QString>

namespace Muffin {

struct MathSpan {
    SourceSpan source;
    SourceSpan content;
    QString tex;
    bool display = false;
};

} // namespace Muffin
