#pragma once

#include "model/MarkdownDocument.h"
#include "model/MarkdownSerializer.h"
#include "parser/CmarkParser.h"

#include <optional>

namespace Muffin::TestUtils {

struct CommandHandlerResult {
    QString markdown;
    int cursor = -1;
};

inline ParseResult parseDocument(const QString& markdown)
{
    CmarkParser parser;
    return parser.parseDocument(markdown);
}

inline CommandHandlerResult serializeCommandResult(const MarkdownDocument& document,
                                                   MarkdownNodeId anchorNodeId,
                                                   int anchorOffsetInNode)
{
    MarkdownSerializer serializer;
    const MarkdownSerializationResult serialized = serializer.serializeDocumentWithSourceMap(document);
    return {serialized.markdown,
            serialized.sourceOffsetForNodeOffset(anchorNodeId, anchorOffsetInNode).value_or(-1)};
}

template <typename Result>
CommandHandlerResult serializeCommandResult(const std::optional<Result>& result)
{
    if (!result) {
        return {};
    }
    return serializeCommandResult(result->document, result->anchorNodeId, result->anchorOffsetInNode);
}

} // namespace Muffin::TestUtils
