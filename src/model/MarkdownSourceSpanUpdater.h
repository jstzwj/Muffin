#pragma once

#include "model/MarkdownDocument.h"
#include "model/MarkdownSerializer.h"

namespace Muffin {

class MarkdownSourceSpanUpdater {
public:
    static MarkdownDocument applySerializedSourceSpans(MarkdownDocument document,
                                                       const MarkdownSerializationResult& serialization);
};

} // namespace Muffin
