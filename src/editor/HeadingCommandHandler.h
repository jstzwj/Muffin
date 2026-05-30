#pragma once

#include "editor/MarkdownCommand.h"
#include "model/MarkdownDocument.h"

#include <optional>

namespace Muffin {

class HeadingCommandHandler {
public:
    struct HeadingCommandResult {
        MarkdownDocument document;
        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        QVector<MarkdownNodeId> affectedNodeIds;
    };

    static std::optional<HeadingCommandResult> applyHeading(const MarkdownDocument& document,
                                                            SourceSelection selection,
                                                            int level);
    static std::optional<HeadingCommandResult> applyParagraph(const MarkdownDocument& document,
                                                              SourceSelection selection);

private:
    static std::optional<HeadingCommandResult> applyBlockType(const MarkdownDocument& document,
                                                              SourceSelection selection,
                                                              MarkdownNodeType targetType,
                                                              int level);
};

} // namespace Muffin
