#include "QuoteCommandHandler.h"

namespace Muffin {

class QuoteCommandHandlerPrivate {
public:
    static MarkdownNode* mutableNode(MarkdownDocument& document, MarkdownNodeId id);
    static std::optional<QuoteCommandHandler::QuoteCommandResult> tryUnwrapQuote(const MarkdownDocument& source,
                                                                                 SourceSelection selection);
    static std::optional<QuoteCommandHandler::QuoteCommandResult> tryWrapPartialList(const MarkdownDocument& source,
                                                                                     SourceSelection selection);
    static std::optional<QuoteCommandHandler::QuoteCommandResult> tryWrapTopLevelBlocks(const MarkdownDocument& source,
                                                                                        SourceSelection selection);
};

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

const MarkdownNode* lastTextInList(const MarkdownDocument& document, const MarkdownNode& list)
{
    if (list.type != MarkdownNodeType::List || list.children.isEmpty()) {
        return nullptr;
    }

    for (int i = list.children.size() - 1; i >= 0; --i) {
        const MarkdownNode* item = document.nodeById(list.children.at(i));
        if (!item) {
            continue;
        }
        if (const MarkdownNode* text = lastTextInListItem(document, *item)) {
            return text;
        }
    }
    return nullptr;
}

const MarkdownNode* lastTextInBlock(const MarkdownDocument& document, const MarkdownNode& node)
{
    if (node.type == MarkdownNodeType::Paragraph || node.type == MarkdownNodeType::Heading) {
        return lastTextChild(document, node);
    }
    if (node.type == MarkdownNodeType::List) {
        return lastTextInList(document, node);
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

int listStartForPosition(bool orderedList, int listStart, int position)
{
    if (!orderedList) {
        return 0;
    }
    const int start = listStart == 0 ? 1 : listStart;
    return start + position;
}

bool isQuoteCommandBlock(const MarkdownNode& node)
{
    return node.type == MarkdownNodeType::Paragraph || node.type == MarkdownNodeType::Heading
        || node.type == MarkdownNodeType::List;
}

bool positionsAreContiguous(const QVector<int>& positions)
{
    for (int i = 1; i < positions.size(); ++i) {
        if (positions.at(i) != positions.at(i - 1) + 1) {
            return false;
        }
    }
    return true;
}

void replaceRootChildRange(MarkdownNode& root, int firstPosition, int count, const QVector<MarkdownNodeId>& replacementIds)
{
    root.children.remove(firstPosition, count);
    for (int i = 0; i < replacementIds.size(); ++i) {
        root.children.insert(firstPosition + i, replacementIds.at(i));
    }
}

} // namespace

MarkdownNode* QuoteCommandHandlerPrivate::mutableNode(MarkdownDocument& document, MarkdownNodeId id)
{
    const auto it = document.m_indexById.constFind(id);
    return it == document.m_indexById.constEnd() ? nullptr : &document.m_nodes[*it];
}

std::optional<QuoteCommandHandler::QuoteCommandResult> QuoteCommandHandlerPrivate::tryUnwrapQuote(const MarkdownDocument& source,
                                                                                                  SourceSelection selection)
{
    MarkdownDocument document = source;
    const MarkdownNodeId rootId = document.rootId();
    MarkdownNode* root = mutableNode(document, rootId);
    if (!root || root->type != MarkdownNodeType::Document) {
        return std::nullopt;
    }

    for (int rootIndex = 0; rootIndex < root->children.size(); ++rootIndex) {
        MarkdownNode* quote = mutableNode(document, root->children.at(rootIndex));
        if (!quote || quote->type != MarkdownNodeType::BlockQuote || !selectionTouchesSpan(selection, quote->source)) {
            continue;
        }

        QVector<int> selectedPositions;
        QVector<MarkdownNodeId> selectedIds;
        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        for (int i = 0; i < quote->children.size(); ++i) {
            const MarkdownNodeId childId = quote->children.at(i);
            const MarkdownNode* child = document.nodeById(childId);
            if (!child || !isQuoteCommandBlock(*child) || !selectionTouchesSpan(selection, child->source)) {
                continue;
            }

            const MarkdownNode* text = lastTextInBlock(document, *child);
            if (!text) {
                return std::nullopt;
            }
            selectedPositions.append(i);
            selectedIds.append(childId);
            anchorNodeId = text->id;
            anchorOffsetInNode = static_cast<int>(text->literal.size());
        }

        if (selectedIds.isEmpty() || !positionsAreContiguous(selectedPositions)) {
            return std::nullopt;
        }

        const MarkdownNodeId originalQuoteId = quote->id;
        const QVector<MarkdownNodeId> originalChildren = quote->children;
        const int firstSelectedPosition = selectedPositions.first();
        const int lastSelectedPosition = selectedPositions.last();

        QVector<MarkdownNodeId> beforeIds;
        QVector<MarkdownNodeId> afterIds;
        for (int i = 0; i < originalChildren.size(); ++i) {
            if (i < firstSelectedPosition) {
                beforeIds.append(originalChildren.at(i));
            } else if (i > lastSelectedPosition) {
                afterIds.append(originalChildren.at(i));
            }
        }

        QVector<MarkdownNodeId> replacementIds;
        if (!beforeIds.isEmpty()) {
            quote->children = beforeIds;
            clearGeneratedSource(*quote);
            replacementIds.append(originalQuoteId);
            for (MarkdownNodeId id : beforeIds) {
                MarkdownNode* child = mutableNode(document, id);
                if (!child) {
                    return std::nullopt;
                }
                child->parent = originalQuoteId;
                clearGeneratedSource(*child);
            }
        }

        for (MarkdownNodeId id : selectedIds) {
            MarkdownNode* child = mutableNode(document, id);
            if (!child) {
                return std::nullopt;
            }
            child->parent = rootId;
            clearGeneratedSource(*child);
            replacementIds.append(id);
        }

        MarkdownNodeId afterQuoteId = 0;
        if (!afterIds.isEmpty()) {
            afterQuoteId = originalQuoteId;
            if (!beforeIds.isEmpty()) {
                MarkdownNode afterQuote = *quote;
                afterQuote.id = document.m_nextId++;
                afterQuote.parent = rootId;
                afterQuote.type = MarkdownNodeType::BlockQuote;
                afterQuote.children = afterIds;
                clearGeneratedSource(afterQuote);
                afterQuoteId = afterQuote.id;
                document.m_nodes.append(std::move(afterQuote));
            } else {
                quote = mutableNode(document, originalQuoteId);
                if (!quote) {
                    return std::nullopt;
                }
                quote->children = afterIds;
                clearGeneratedSource(*quote);
            }

            for (MarkdownNodeId id : afterIds) {
                MarkdownNode* child = mutableNode(document, id);
                if (!child) {
                    return std::nullopt;
                }
                child->parent = afterQuoteId;
                clearGeneratedSource(*child);
            }
            replacementIds.append(afterQuoteId);
        } else if (beforeIds.isEmpty()) {
            quote->parent = 0;
            clearGeneratedSource(*quote);
        }

        root = mutableNode(document, rootId);
        if (!root) {
            return std::nullopt;
        }
        replaceRootChildRange(*root, rootIndex, 1, replacementIds);
        document.rebuildIndex();
        QVector<MarkdownNodeId> affected = {originalQuoteId};
        affected.append(selectedIds);
        affected.append(beforeIds);
        affected.append(afterIds);
        if (afterQuoteId && afterQuoteId != originalQuoteId) {
            affected.append(afterQuoteId);
        }
        return QuoteCommandHandler::QuoteCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
    }

    return std::nullopt;
}

std::optional<QuoteCommandHandler::QuoteCommandResult> QuoteCommandHandlerPrivate::tryWrapPartialList(const MarkdownDocument& source,
                                                                                                      SourceSelection selection)
{
    MarkdownDocument document = source;
    const MarkdownNodeId rootId = document.rootId();
    MarkdownNode* root = mutableNode(document, rootId);
    if (!root || root->type != MarkdownNodeType::Document) {
        return std::nullopt;
    }

    for (int rootIndex = 0; rootIndex < root->children.size(); ++rootIndex) {
        const MarkdownNodeId listId = root->children.at(rootIndex);
        MarkdownNode* list = mutableNode(document, listId);
        if (!list || list->type != MarkdownNodeType::List || !selectionTouchesSpan(selection, list->source)) {
            continue;
        }

        QVector<int> itemPositions;
        QVector<MarkdownNodeId> selectedItemIds;
        for (int i = 0; i < list->children.size(); ++i) {
            const MarkdownNodeId itemId = list->children.at(i);
            const MarkdownNode* item = document.nodeById(itemId);
            if (!item || item->type != MarkdownNodeType::ListItem || !selectionTouchesSpan(selection, item->source)) {
                continue;
            }
            itemPositions.append(i);
            selectedItemIds.append(itemId);
        }

        if (selectedItemIds.isEmpty()) {
            return std::nullopt;
        }
        if (selectedItemIds.size() == list->children.size()) {
            return std::nullopt;
        }
        if (!positionsAreContiguous(itemPositions)) {
            return std::nullopt;
        }

        const MarkdownNode* anchorText = nullptr;
        for (int i = selectedItemIds.size() - 1; i >= 0 && !anchorText; --i) {
            const MarkdownNode* item = document.nodeById(selectedItemIds.at(i));
            if (item) {
                anchorText = lastTextInListItem(document, *item);
            }
        }
        if (!anchorText) {
            return std::nullopt;
        }
        const MarkdownNodeId anchorNodeId = anchorText->id;
        const int anchorOffsetInNode = static_cast<int>(anchorText->literal.size());

        const bool originalOrderedList = list->orderedList;
        const int originalListStart = list->listStart;
        const int firstItemPosition = itemPositions.first();
        const int lastItemPosition = itemPositions.last();
        const QVector<MarkdownNodeId> originalItemIds = list->children;
        QVector<MarkdownNodeId> beforeItemIds;
        QVector<MarkdownNodeId> afterItemIds;
        for (int i = 0; i < originalItemIds.size(); ++i) {
            if (i < firstItemPosition) {
                beforeItemIds.append(originalItemIds.at(i));
            } else if (i > lastItemPosition) {
                afterItemIds.append(originalItemIds.at(i));
            }
        }

        MarkdownNode quoteNode;
        quoteNode.id = document.m_nextId++;
        quoteNode.parent = rootId;
        quoteNode.type = MarkdownNodeType::BlockQuote;
        clearGeneratedSource(quoteNode);

        MarkdownNodeId selectedListId = list->id;
        if (!beforeItemIds.isEmpty()) {
            MarkdownNode selectedList = *list;
            selectedList.id = document.m_nextId++;
            selectedList.parent = quoteNode.id;
            selectedList.children = selectedItemIds;
            selectedList.listStart = listStartForPosition(originalOrderedList, originalListStart, firstItemPosition);
            clearGeneratedSource(selectedList);
            selectedListId = selectedList.id;
            document.m_nodes.append(std::move(selectedList));

            list = mutableNode(document, listId);
            if (!list) {
                return std::nullopt;
            }
            list->children = beforeItemIds;
            list->listStart = listStartForPosition(originalOrderedList, originalListStart, 0);
            clearGeneratedSource(*list);
        } else {
            list->parent = quoteNode.id;
            list->children = selectedItemIds;
            list->listStart = listStartForPosition(originalOrderedList, originalListStart, firstItemPosition);
            clearGeneratedSource(*list);
        }

        quoteNode.children.append(selectedListId);

        MarkdownNodeId afterListId = 0;
        if (!afterItemIds.isEmpty()) {
            MarkdownNode afterList = *list;
            afterList.id = document.m_nextId++;
            afterList.parent = rootId;
            afterList.children = afterItemIds;
            afterList.listStart = listStartForPosition(originalOrderedList, originalListStart, lastItemPosition + 1);
            clearGeneratedSource(afterList);
            afterListId = afterList.id;
            document.m_nodes.append(std::move(afterList));
        }

        for (MarkdownNodeId itemId : selectedItemIds) {
            MarkdownNode* item = mutableNode(document, itemId);
            if (!item) {
                return std::nullopt;
            }
            item->parent = selectedListId;
            clearGeneratedSource(*item);
        }
        for (MarkdownNodeId itemId : afterItemIds) {
            MarkdownNode* item = mutableNode(document, itemId);
            if (!item) {
                return std::nullopt;
            }
            item->parent = afterListId;
            clearGeneratedSource(*item);
        }

        QVector<MarkdownNodeId> replacementIds;
        if (!beforeItemIds.isEmpty()) {
            replacementIds.append(listId);
        }
        replacementIds.append(quoteNode.id);
        if (afterListId) {
            replacementIds.append(afterListId);
        }

        root = mutableNode(document, rootId);
        if (!root) {
            return std::nullopt;
        }
        const MarkdownNodeId quoteNodeId = quoteNode.id;
        replaceRootChildRange(*root, rootIndex, 1, replacementIds);
        document.m_nodes.append(std::move(quoteNode));
        document.rebuildIndex();
        QVector<MarkdownNodeId> affected = {quoteNodeId, listId};
        affected.append(selectedItemIds);
        if (afterListId) {
            affected.append(afterListId);
        }
        return QuoteCommandHandler::QuoteCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
    }

    return std::nullopt;
}

std::optional<QuoteCommandHandler::QuoteCommandResult> QuoteCommandHandlerPrivate::tryWrapTopLevelBlocks(const MarkdownDocument& source,
                                                                                                         SourceSelection selection)
{
    MarkdownDocument document = source;
    const MarkdownNodeId rootId = document.rootId();
    MarkdownNode* root = mutableNode(document, rootId);
    if (!root || root->type != MarkdownNodeType::Document) {
        return std::nullopt;
    }

    QVector<int> blockPositions;
    QVector<MarkdownNodeId> blockIds;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    for (int i = 0; i < root->children.size(); ++i) {
        const MarkdownNodeId childId = root->children.at(i);
        const MarkdownNode* child = document.nodeById(childId);
        if (!child || !isQuoteCommandBlock(*child) || !selectionTouchesSpan(selection, child->source)) {
            continue;
        }

        const MarkdownNode* anchorText = lastTextInBlock(document, *child);
        if (!anchorText) {
            return std::nullopt;
        }
        blockPositions.append(i);
        blockIds.append(childId);
        anchorNodeId = anchorText->id;
        anchorOffsetInNode = static_cast<int>(anchorText->literal.size());
    }

    if (blockIds.isEmpty() || !positionsAreContiguous(blockPositions)) {
        return std::nullopt;
    }

    MarkdownNode quoteNode;
    quoteNode.id = document.m_nextId++;
    quoteNode.parent = rootId;
    quoteNode.type = MarkdownNodeType::BlockQuote;
    quoteNode.children = blockIds;
    clearGeneratedSource(quoteNode);

    for (MarkdownNodeId blockId : blockIds) {
        MarkdownNode* block = mutableNode(document, blockId);
        if (!block) {
            return std::nullopt;
        }
        block->parent = quoteNode.id;
        clearGeneratedSource(*block);
    }

    root = mutableNode(document, rootId);
    if (!root) {
        return std::nullopt;
    }
    const MarkdownNodeId quoteNodeId = quoteNode.id;
    replaceRootChildRange(*root, blockPositions.first(), blockPositions.size(), {quoteNodeId});
    document.m_nodes.append(std::move(quoteNode));
    document.rebuildIndex();

    QVector<MarkdownNodeId> affected = {quoteNodeId};
    affected.append(blockIds);
    return QuoteCommandHandler::QuoteCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
}

std::optional<QuoteCommandHandler::QuoteCommandResult> QuoteCommandHandler::applyQuote(const MarkdownDocument& document,
                                                                                       SourceSelection selection)
{
    if (std::optional<QuoteCommandResult> result = QuoteCommandHandlerPrivate::tryUnwrapQuote(document, selection)) {
        return result;
    }
    if (std::optional<QuoteCommandResult> result = QuoteCommandHandlerPrivate::tryWrapPartialList(document, selection)) {
        return result;
    }
    return QuoteCommandHandlerPrivate::tryWrapTopLevelBlocks(document, selection);
}

} // namespace Muffin
