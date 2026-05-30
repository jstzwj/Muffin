#pragma once

#include "editor/MarkdownCommand.h"
#include "model/MarkdownDocument.h"

#include <optional>

namespace Muffin {

class ListCommandHandler {
public:
    struct ListCommandResult {
        MarkdownDocument document;
        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        QVector<MarkdownNodeId> affectedNodeIds;
    };

    static std::optional<ListCommandResult> applyList(const MarkdownDocument& document,
                                                      SourceSelection selection,
                                                      MarkdownCommand::ListType type);
};

} // namespace Muffin
