#include "MarkdownSourceSpanUpdater.h"

#include "parser/SourceCoordinateMapper.h"

namespace Muffin {

namespace {

SourceRange sourceRangeForSpan(const SourceCoordinateMapper& mapper, SourceSpan span)
{
    if (!span.isValid()) {
        return {};
    }
    const int endOffset = span.end > span.start ? span.end - 1 : span.end;
    return {
        mapper.lineForOffset(span.start),
        mapper.columnForOffset(span.start),
        mapper.lineForOffset(endOffset),
        mapper.columnForOffset(endOffset)
    };
}

} // namespace

MarkdownDocument MarkdownSourceSpanUpdater::applySerializedSourceSpans(MarkdownDocument document,
                                                                       const MarkdownSerializationResult& serialization)
{
    document.m_source = serialization.markdown;
    const SourceCoordinateMapper mapper(document.m_source);
    for (MarkdownNode& node : document.m_nodes) {
        const auto it = serialization.nodeSpans.constFind(node.id);
        if (it == serialization.nodeSpans.constEnd()) {
            continue;
        }
        node.source = it->source;
        node.content = it->content;
        node.sourceRange = sourceRangeForSpan(mapper, node.source);
    }
    return document;
}

} // namespace Muffin
