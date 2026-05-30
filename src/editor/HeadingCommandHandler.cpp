#include "HeadingCommandHandler.h"

namespace Muffin {

namespace {

bool selectionTouchesSpan(SourceSelection selection, SourceSpan span)
{
    if (!span.isValid()) {
        return false;
    }

    const int start = selection.normalizedStart();
    const int end = selection.normalizedEnd();
    if (start == end) {
        return start >= span.start && start <= span.end;
    }
    return start < span.end && end > span.start;
}

const MarkdownNode* lastTextChild(const MarkdownDocument& document, const MarkdownNode& node)
{
    if (node.children.isEmpty()) {
        return nullptr;
    }
    const MarkdownNode* child = document.nodeById(node.children.last());
    return child && child->type == MarkdownNodeType::Text ? child : nullptr;
}

} // namespace

std::optional<HeadingCommandHandler::HeadingCommandResult> HeadingCommandHandler::applyBlockType(const MarkdownDocument& source,
                                                                                                 SourceSelection selection,
                                                                                                 MarkdownNodeType targetType,
                                                                                                 int level)
{
    if (targetType == MarkdownNodeType::Heading) {
        level = qBound(1, level, 6);
    }

    MarkdownDocument document = source;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    bool changedAny = false;
    QVector<MarkdownNodeId> affectedNodeIds;

    for (MarkdownNode& node : document.m_nodes) {
        if (node.type != MarkdownNodeType::Paragraph && node.type != MarkdownNodeType::Heading) {
            continue;
        }
        if (!selectionTouchesSpan(selection, node.source)) {
            continue;
        }
        if (node.children.isEmpty()) {
            return std::nullopt;
        }

        const MarkdownNode* sourceText = lastTextChild(source, node);
        if (!sourceText) {
            return std::nullopt;
        }

        node.type = targetType;
        node.headingLevel = targetType == MarkdownNodeType::Heading ? level : 0;
        node.source = {};
        node.content = {};
        node.sourceRange = {};
        node.inlineTokens.clear();

        const MarkdownNode* text = document.nodeById(sourceText->id);
        if (!text) {
            return std::nullopt;
        }
        anchorNodeId = text->id;
        anchorOffsetInNode = static_cast<int>(text->literal.size());
        changedAny = true;
        affectedNodeIds.append(node.id);
    }

    if (!changedAny) {
        return std::nullopt;
    }
    return HeadingCommandHandler::HeadingCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affectedNodeIds)};
}

std::optional<HeadingCommandHandler::HeadingCommandResult> HeadingCommandHandler::applyHeading(const MarkdownDocument& source,
                                                                                               SourceSelection selection,
                                                                                               int level)
{
    return applyBlockType(source, selection, MarkdownNodeType::Heading, level);
}

std::optional<HeadingCommandHandler::HeadingCommandResult> HeadingCommandHandler::applyParagraph(const MarkdownDocument& source,
                                                                                                 SourceSelection selection)
{
    return applyBlockType(source, selection, MarkdownNodeType::Paragraph, 0);
}

} // namespace Muffin
