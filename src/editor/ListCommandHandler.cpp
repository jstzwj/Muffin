#include "ListCommandHandler.h"

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

const MarkdownNode* lastTextInListItem(const MarkdownDocument& document, const MarkdownNode& item)
{
    if (item.type != MarkdownNodeType::ListItem || item.children.isEmpty()) {
        return nullptr;
    }

    for (int i = item.children.size() - 1; i >= 0; --i) {
        const MarkdownNode* child = document.nodeById(item.children.at(i));
        if (!child) {
            continue;
        }
        if (child->type == MarkdownNodeType::Paragraph || child->type == MarkdownNodeType::Heading) {
            if (const MarkdownNode* text = lastTextChild(document, *child)) {
                return text;
            }
        }
    }
    return nullptr;
}

void clearGeneratedSource(MarkdownNode& node)
{
    node.source = {};
    node.content = {};
    node.sourceRange = {};
    node.inlineTokens.clear();
}

} // namespace

std::optional<ListCommandHandler::ListCommandResult> ListCommandHandler::applyList(const MarkdownDocument& source,
                                                                                   SourceSelection selection,
                                                                                   MarkdownCommand::ListType type)
{
    MarkdownDocument document = source;
    const MarkdownNodeId rootId = document.rootId();
    MarkdownNode* root = nullptr;
    const auto rootIt = document.m_indexById.constFind(rootId);
    if (rootIt != document.m_indexById.constEnd()) {
        root = &document.m_nodes[*rootIt];
    }
    if (!root || root->type != MarkdownNodeType::Document) {
        return std::nullopt;
    }

    for (MarkdownNode& node : document.m_nodes) {
        if (node.type != MarkdownNodeType::List || node.parent != rootId || !selectionTouchesSpan(selection, node.source)) {
            continue;
        }
        if (node.children.isEmpty()) {
            return std::nullopt;
        }

        node.orderedList = type == MarkdownCommand::ListType::Ordered;
        node.listStart = node.orderedList ? 1 : 0;
        clearGeneratedSource(node);

        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        for (MarkdownNodeId itemId : node.children) {
            const auto itemIt = document.m_indexById.constFind(itemId);
            if (itemIt == document.m_indexById.constEnd()) {
                return std::nullopt;
            }

            MarkdownNode& item = document.m_nodes[*itemIt];
            if (item.type != MarkdownNodeType::ListItem) {
                return std::nullopt;
            }
            item.taskList = type == MarkdownCommand::ListType::Task;
            item.taskChecked = false;
            clearGeneratedSource(item);

            const MarkdownNode* anchorText = lastTextInListItem(document, item);
            if (!anchorText) {
                return std::nullopt;
            }
            anchorNodeId = anchorText->id;
            anchorOffsetInNode = static_cast<int>(anchorText->literal.size());
        }
        document.rebuildIndex();
        QVector<MarkdownNodeId> affected;
        affected.append(node.id);
        affected.append(node.children);
        return ListCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
    }

    QVector<int> blockPositions;
    QVector<MarkdownNodeId> blockIds;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    for (int i = 0; i < root->children.size(); ++i) {
        const MarkdownNodeId childId = root->children.at(i);
        MarkdownNode* child = nullptr;
        const auto childIt = document.m_indexById.constFind(childId);
        if (childIt != document.m_indexById.constEnd()) {
            child = &document.m_nodes[*childIt];
        }
        if (!child || (child->type != MarkdownNodeType::Paragraph && child->type != MarkdownNodeType::Heading)) {
            continue;
        }
        if (!selectionTouchesSpan(selection, child->source)) {
            continue;
        }
        if (child->children.isEmpty()) {
            return std::nullopt;
        }

        const MarkdownNode* anchorText = lastTextChild(document, *child);
        if (!anchorText) {
            return std::nullopt;
        }

        blockPositions.append(i);
        blockIds.append(childId);
        anchorNodeId = anchorText->id;
        anchorOffsetInNode = static_cast<int>(anchorText->literal.size());
    }

    if (blockIds.isEmpty()) {
        return std::nullopt;
    }
    for (int i = 1; i < blockPositions.size(); ++i) {
        if (blockPositions.at(i) != blockPositions.at(i - 1) + 1) {
            return std::nullopt;
        }
    }

    MarkdownNode listNode;
    listNode.id = document.m_nextId++;
    listNode.parent = rootId;
    listNode.type = MarkdownNodeType::List;
    listNode.orderedList = type == MarkdownCommand::ListType::Ordered;
    listNode.listStart = listNode.orderedList ? 1 : 0;
    clearGeneratedSource(listNode);

    QVector<MarkdownNodeId> listItemIds;
    listItemIds.reserve(blockIds.size());
    for (MarkdownNodeId blockId : blockIds) {
        const auto blockIt = document.m_indexById.constFind(blockId);
        if (blockIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }
        MarkdownNode& block = document.m_nodes[*blockIt];
        block.type = MarkdownNodeType::Paragraph;
        block.headingLevel = 0;
        clearGeneratedSource(block);

        MarkdownNode itemNode;
        itemNode.id = document.m_nextId++;
        itemNode.parent = listNode.id;
        itemNode.type = MarkdownNodeType::ListItem;
        itemNode.children.append(block.id);
        itemNode.taskList = type == MarkdownCommand::ListType::Task;
        itemNode.taskChecked = false;
        clearGeneratedSource(itemNode);

        block.parent = itemNode.id;
        listItemIds.append(itemNode.id);
        document.m_nodes.append(std::move(itemNode));
    }
    listNode.children = listItemIds;

    root = &document.m_nodes[*rootIt];
    const int firstPosition = blockPositions.first();
    root->children.remove(firstPosition, blockPositions.size());
    root->children.insert(firstPosition, listNode.id);
    document.m_nodes.append(std::move(listNode));
    document.rebuildIndex();

    QVector<MarkdownNodeId> affected;
    affected.append(document.m_nodes.last().id); // listNode
    affected.append(listItemIds);
    affected.append(blockIds);
    return ListCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
}

} // namespace Muffin
