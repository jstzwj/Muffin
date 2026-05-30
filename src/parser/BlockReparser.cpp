#include "BlockReparser.h"
#include "parser/CmarkParser.h"

namespace Muffin {

BlockReparseResult BlockReparser::reparse(const MarkdownDocument& previousDocument,
                                          const BlockParseRange& range,
                                          const QString& fullMarkdown)
{
    BlockReparseResult result;
    result.range = range;
    result.fullMarkdown = fullMarkdown;

    if (!range.canReparseLocally()) {
        result.errors.append(QStringLiteral("Block parse range is not locally reparsable."));
        return result;
    }
    if (range.expandedSource.end > fullMarkdown.size()) {
        result.errors.append(QStringLiteral("Expanded source range exceeds markdown length."));
        return result;
    }
    if (range.affectedBlockIds.size() != 1) {
        result.errors.append(QStringLiteral("Local block reparse requires exactly one affected block."));
        return result;
    }

    const MarkdownNode* previousBlock = previousDocument.nodeById(range.affectedBlockIds.first());
    if (!previousBlock) {
        result.errors.append(QStringLiteral("Affected block no longer exists in previous document."));
        return result;
    }
    if (!isSupportedTopLevelBlock(previousBlock->type)) {
        result.errors.append(QStringLiteral("Affected block type is not supported for local reparse."));
        return result;
    }

    result.attempted = true;
    result.localMarkdown = fullMarkdown.mid(range.expandedSource.start,
                                            range.expandedSource.end - range.expandedSource.start);

    CmarkParser parser;
    result.localDocument = parser.parseDocument(result.localMarkdown).document;
    const MarkdownNode* root = result.localDocument.nodeById(result.localDocument.rootId());
    if (!root) {
        result.errors.append(QStringLiteral("Local parse produced no root document."));
        return result;
    }

    const MarkdownNode* localBlock = findLocalBlock(result.localDocument, previousBlock);
    if (!localBlock) {
        result.errors.append(QStringLiteral("Local parse did not produce a matching block."));
        return result;
    }
    if (localBlock->type != previousBlock->type) {
        result.errors.append(QStringLiteral("Local parse changed the block type."));
        return result;
    }
    if (localBlock->type == MarkdownNodeType::CodeBlock
        && localBlock->fenceInfo != previousBlock->fenceInfo) {
        result.errors.append(QStringLiteral("Local parse changed the code block fence info."));
        return result;
    }

    result.ok = true;
    return result;
}

const MarkdownNode* BlockReparser::findLocalBlock(const MarkdownDocument& localDoc,
                                                   const MarkdownNode* previousBlock)
{
    const MarkdownNode* root = localDoc.nodeById(localDoc.rootId());
    if (!root) {
        return nullptr;
    }

    // For Paragraph, Heading, CodeBlock: top-level block must match.
    if (previousBlock->type == MarkdownNodeType::Paragraph
        || previousBlock->type == MarkdownNodeType::Heading
        || previousBlock->type == MarkdownNodeType::CodeBlock) {
        if (root->children.size() != 1) {
            return nullptr;
        }
        const MarkdownNode* top = localDoc.nodeById(root->children.first());
        return (top && top->type == previousBlock->type) ? top : nullptr;
    }

    // For ListItem: parse produces List > ListItem, find the ListItem inside.
    if (previousBlock->type == MarkdownNodeType::ListItem) {
        if (root->children.size() != 1) {
            return nullptr;
        }
        const MarkdownNode* list = localDoc.nodeById(root->children.first());
        if (!list || list->type != MarkdownNodeType::List) {
            return nullptr;
        }
        if (list->children.size() != 1) {
            return nullptr;
        }
        const MarkdownNode* item = localDoc.nodeById(list->children.first());
        return (item && item->type == MarkdownNodeType::ListItem) ? item : nullptr;
    }

    // For BlockQuote: parse produces BlockQuote as top-level.
    if (previousBlock->type == MarkdownNodeType::BlockQuote) {
        if (root->children.size() != 1) {
            return nullptr;
        }
        const MarkdownNode* top = localDoc.nodeById(root->children.first());
        return (top && top->type == MarkdownNodeType::BlockQuote) ? top : nullptr;
    }

    return nullptr;
}

bool BlockReparser::isSupportedTopLevelBlock(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Paragraph
        || type == MarkdownNodeType::Heading
        || type == MarkdownNodeType::CodeBlock
        || type == MarkdownNodeType::ListItem
        || type == MarkdownNodeType::BlockQuote;
}

} // namespace Muffin
