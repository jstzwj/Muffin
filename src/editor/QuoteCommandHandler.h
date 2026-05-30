#pragma once

#include "editor/MarkdownCommand.h"
#include "model/MarkdownDocument.h"

#include <optional>

namespace Muffin {

class QuoteCommandHandler {
public:
    struct QuoteCommandResult {
        MarkdownDocument document;
        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        QVector<MarkdownNodeId> affectedNodeIds;
    };

    static std::optional<QuoteCommandResult> applyQuote(const MarkdownDocument& document,
                                                        SourceSelection selection);
};

} // namespace Muffin
