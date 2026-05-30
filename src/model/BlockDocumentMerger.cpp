#include "BlockDocumentMerger.h"

#include <QSet>

namespace Muffin {

namespace {

MarkdownNodeId maxNodeId(const QVector<MarkdownNode>& nodes)
{
    MarkdownNodeId maxId = 0;
    for (const MarkdownNode& node : nodes) {
        maxId = qMax(maxId, node.id);
    }
    return maxId;
}

} // namespace

BlockMergeResult BlockDocumentMerger::mergeReparsedBlock(const MarkdownDocument& previousDocument,
                                                         const BlockReparseResult& reparseResult)
{
    BlockMergeResult result;
    result.document = previousDocument;

    if (!reparseResult.ok) {
        result.errors.append(QStringLiteral("Block reparse result is not ok."));
        return result;
    }
    if (reparseResult.range.affectedBlockIds.size() != 1) {
        result.errors.append(QStringLiteral("Block merge requires exactly one affected block."));
        return result;
    }

    const MarkdownNodeId replacedNodeId = reparseResult.range.affectedBlockIds.first();
    const MarkdownNode* previousBlock = previousDocument.nodeById(replacedNodeId);
    if (!previousBlock) {
        result.errors.append(QStringLiteral("Affected block does not exist in previous document."));
        return result;
    }

    const MarkdownNode* localBlock = localTopLevelBlock(reparseResult.localDocument);
    if (!localBlock) {
        result.errors.append(QStringLiteral("Local document has no top-level block."));
        return result;
    }
    // For ListItem: local parse wraps in List > ListItem, so find the inner ListItem.
    if (previousBlock->type == MarkdownNodeType::ListItem
        && localBlock->type == MarkdownNodeType::List
        && localBlock->children.size() == 1) {
        const MarkdownNode* inner = reparseResult.localDocument.nodeById(localBlock->children.first());
        if (inner && inner->type == MarkdownNodeType::ListItem) {
            localBlock = inner;
        }
    }
    if (localBlock->type != previousBlock->type) {
        result.errors.append(QStringLiteral("Local block type does not match previous block type."));
        return result;
    }

    QVector<MarkdownNodeId> oldSubtreeIds;
    collectSubtreeIds(previousDocument, replacedNodeId, oldSubtreeIds);
    QSet<MarkdownNodeId> oldSubtreeIdSet(oldSubtreeIds.cbegin(), oldSubtreeIds.cend());

    QVector<MarkdownNode> replacementNodes;
    QHash<MarkdownNodeId, MarkdownNodeId> remappedIds;
    MarkdownNodeId nextId = maxNodeId(previousDocument.m_nodes) + 1;

    QVector<MarkdownNodeId> localSubtreeIds;
    collectSubtreeIds(reparseResult.localDocument, localBlock->id, localSubtreeIds);
    for (MarkdownNodeId localNodeId : localSubtreeIds) {
        const MarkdownNode* localNode = reparseResult.localDocument.nodeById(localNodeId);
        if (!localNode) {
            result.errors.append(QStringLiteral("Local subtree contains an invalid node id."));
            return result;
        }
        const MarkdownNodeId newId = localNodeId == localBlock->id ? replacedNodeId : nextId++;
        remappedIds.insert(localNodeId, newId);
    }

    for (MarkdownNodeId localNodeId : localSubtreeIds) {
        const MarkdownNode* localNode = reparseResult.localDocument.nodeById(localNodeId);
        MarkdownNode rebased = rebaseNode(*localNode, reparseResult.range.expandedSource.start);
        rebased.id = remappedIds.value(localNode->id);
        rebased.parent = localNode->id == localBlock->id
            ? previousBlock->parent
            : remappedIds.value(localNode->parent, previousBlock->parent);
        for (MarkdownNodeId& childId : rebased.children) {
            childId = remappedIds.value(childId, childId);
        }
        replacementNodes.append(std::move(rebased));
    }

    MarkdownDocument merged = previousDocument;
    merged.m_source = reparseResult.fullMarkdown;

    QVector<MarkdownNode> nodes;
    nodes.reserve(previousDocument.m_nodes.size() - oldSubtreeIds.size() + replacementNodes.size());
    for (const MarkdownNode& node : previousDocument.m_nodes) {
        if (node.id == replacedNodeId) {
            for (const MarkdownNode& replacement : replacementNodes) {
                nodes.append(replacement);
            }
            continue;
        }
        if (oldSubtreeIdSet.contains(node.id)) {
            continue;
        }
        nodes.append(node);
    }
    merged.m_nodes = std::move(nodes);
    merged.m_nextId = qMax(maxNodeId(merged.m_nodes), nextId - 1) + 1;
    merged.rebuildIndex();

    result.document = std::move(merged);
    result.replacedNodeId = replacedNodeId;
    result.ok = true;
    return result;
}

const MarkdownNode* BlockDocumentMerger::localTopLevelBlock(const MarkdownDocument& document)
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    if (!root || root->children.size() != 1) {
        return nullptr;
    }
    return document.nodeById(root->children.first());
}

BlockMergeConsistencyResult BlockDocumentMerger::compareMergedBlockToFullParse(const MarkdownDocument& mergedDocument,
                                                                               const MarkdownDocument& fullDocument,
                                                                               MarkdownNodeId mergedBlockId,
                                                                               SourceSpan blockSource)
{
    BlockMergeConsistencyResult result;
    if (mergedDocument.source() != fullDocument.source()) {
        result.errors.append(QStringLiteral("Merged document source does not match full parse source."));
    }

    const MarkdownNode* mergedBlock = mergedDocument.nodeById(mergedBlockId);
    const MarkdownNode* fullBlock = blockBySource(fullDocument, blockSource);
    if (!mergedBlock) {
        result.errors.append(QStringLiteral("Merged block does not exist."));
    }
    if (!fullBlock) {
        result.errors.append(QStringLiteral("Full parse block does not exist."));
    }
    if (!mergedBlock || !fullBlock) {
        return result;
    }

    if (mergedBlock->type != fullBlock->type) {
        result.errors.append(QStringLiteral("Merged block type does not match full parse block type."));
    }
    if (mergedBlock->source.start != fullBlock->source.start || mergedBlock->source.end != fullBlock->source.end) {
        result.errors.append(QStringLiteral("Merged block source span does not match full parse block source span."));
    }
    if (mergedBlock->content.start != fullBlock->content.start || mergedBlock->content.end != fullBlock->content.end) {
        result.errors.append(QStringLiteral("Merged block content span does not match full parse block content span."));
    }
    if (mergedBlock->headingLevel != fullBlock->headingLevel) {
        result.errors.append(QStringLiteral("Merged block heading level does not match full parse block heading level."));
    }
    if (mergedBlock->fenceInfo != fullBlock->fenceInfo) {
        result.errors.append(QStringLiteral("Merged block fence info does not match full parse block fence info."));
    }
    if (mergedBlock->literal != fullBlock->literal) {
        result.errors.append(QStringLiteral("Merged block literal does not match full parse block literal."));
    }
    if (mergedBlock->type != MarkdownNodeType::CodeBlock
        && textLiteralForBlock(mergedDocument, *mergedBlock) != textLiteralForBlock(fullDocument, *fullBlock)) {
        result.errors.append(QStringLiteral("Merged block text literal does not match full parse block text literal."));
    }

    result.ok = result.errors.isEmpty();
    return result;
}

const MarkdownNode* BlockDocumentMerger::blockBySource(const MarkdownDocument& document, SourceSpan source)
{
    for (const MarkdownNode& node : document.nodes()) {
        if ((node.type == MarkdownNodeType::Paragraph
             || node.type == MarkdownNodeType::Heading
             || node.type == MarkdownNodeType::CodeBlock
             || node.type == MarkdownNodeType::ListItem
             || node.type == MarkdownNodeType::BlockQuote)
            && node.source.start == source.start
            && node.source.end == source.end) {
            return &node;
        }
    }
    return nullptr;
}

QString BlockDocumentMerger::textLiteralForBlock(const MarkdownDocument& document, const MarkdownNode& block)
{
    QString literal;
    for (MarkdownNodeId childId : block.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }
        if (child->type == MarkdownNodeType::Text || child->type == MarkdownNodeType::InlineCode) {
            literal.append(child->literal);
        }
    }
    return literal;
}

void BlockDocumentMerger::collectSubtreeIds(const MarkdownDocument& document,
                                            MarkdownNodeId nodeId,
                                            QVector<MarkdownNodeId>& ids)
{
    const MarkdownNode* node = document.nodeById(nodeId);
    if (!node) {
        return;
    }
    ids.append(nodeId);
    for (MarkdownNodeId childId : node->children) {
        collectSubtreeIds(document, childId, ids);
    }
}

MarkdownNode BlockDocumentMerger::rebaseNode(MarkdownNode node, int sourceOffset)
{
    node.source = rebaseSpan(node.source, sourceOffset);
    node.content = rebaseSpan(node.content, sourceOffset);
    for (MarkdownInlineToken& token : node.inlineTokens) {
        token.source = rebaseSpan(token.source, sourceOffset);
    }
    return node;
}

SourceSpan BlockDocumentMerger::rebaseSpan(SourceSpan span, int sourceOffset)
{
    if (!span.isValid()) {
        return span;
    }
    return {span.start + sourceOffset, span.end + sourceOffset};
}

} // namespace Muffin
