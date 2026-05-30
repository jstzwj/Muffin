#include "BlockReparsePlanner.h"

namespace Muffin {

BlockParseRange BlockReparsePlanner::planForEdit(const MarkdownDocument& document,
                                                 SourceSpan editedSource,
                                                 const QString& replacement)
{
    BlockParseRange range;
    range.editedSource = editedSource;

    if (!editedSource.isValid()) {
        range.reason = QStringLiteral("Invalid edited source span.");
        return range;
    }
    if (replacementChangesBlockShape(replacement)) {
        range.reason = QStringLiteral("Replacement changes block shape.");
        return range;
    }

    const MarkdownNode* block = localReparseCandidateForEdit(document, editedSource);
    if (!block || !block->source.isValid()) {
        range.reason = QStringLiteral("No block contains edited source.");
        return range;
    }

    // Check if the candidate block is inside a simple wrapper (list item / blockquote)
    // that can be locally reparsed as a whole.
    const MarkdownNode* effective = expandToSimpleWrapper(document, *block);
    if (effective != block) {
        // Edit is inside a simple wrapper - use the wrapper as the reparse scope.
        if (editTouchesBlockBoundary(*effective, editedSource)) {
            range.reason = QStringLiteral("Edit touches wrapper boundary.");
            return range;
        }
        block = effective;
    } else if (hasFullReparseAncestor(document, *block) || isKnownFullReparseBlock(block->type)) {
        range.reason = QStringLiteral("Block type requires full reparse.");
        return range;
    }

    if (editedSource.end > block->source.end) {
        range.reason = QStringLiteral("Edit crosses block boundary.");
        return range;
    }
    if (editTouchesBlockBoundary(*block, editedSource)) {
        range.reason = QStringLiteral("Edit touches block boundary.");
        return range;
    }
    if (block->type == MarkdownNodeType::CodeBlock
        && !editInsideCodeBlockContent(*block, editedSource)) {
        range.reason = QStringLiteral("Edit touches code block fence.");
        return range;
    }
    if (!isLocallyReparsableBlock(block->type)) {
        range.reason = QStringLiteral("Unsupported local reparse block type.");
        return range;
    }

    range.requiresFullReparse = false;
    range.reason.clear();
    range.targetBlockType = block->type;
    range.expandedSource = block->source;
    range.affectedBlockIds.append(block->id);
    return range;
}

const MarkdownNode* BlockReparsePlanner::localReparseCandidateForEdit(const MarkdownDocument& document, SourceSpan editedSource)
{
    const MarkdownNode* block = document.blockAtSourceOffset(editedSource.start);
    if (!block || block->type != MarkdownNodeType::Document) {
        return block;
    }

    const MarkdownNode* best = nullptr;
    for (MarkdownNodeId childId : block->children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child || !child->source.isValid() || !child->source.contains(editedSource.start)) {
            continue;
        }
        if (!best || (child->source.end - child->source.start) < (best->source.end - best->source.start)) {
            best = child;
        }
    }
    return best ? best : block;
}

const MarkdownNode* BlockReparsePlanner::expandToSimpleWrapper(const MarkdownDocument& document, const MarkdownNode& block)
{
    if (block.type != MarkdownNodeType::Paragraph) {
        return &block;
    }

    const MarkdownNode* parent = document.nodeById(block.parent);
    if (!parent) {
        return &block;
    }

    // Simple list item: ListItem with exactly one Paragraph child.
    if (parent->type == MarkdownNodeType::ListItem && parent->children.size() == 1) {
        const MarkdownNode* list = document.nodeById(parent->parent);
        if (list && list->type == MarkdownNodeType::List) {
            return parent;
        }
    }

    // Simple blockquote: BlockQuote with exactly one Paragraph child.
    if (parent->type == MarkdownNodeType::BlockQuote && parent->children.size() == 1) {
        return parent;
    }

    return &block;
}

bool BlockReparsePlanner::hasFullReparseAncestor(const MarkdownDocument& document, const MarkdownNode& block)
{
    MarkdownNodeId parentId = block.parent;
    while (parentId != 0) {
        const MarkdownNode* parent = document.nodeById(parentId);
        if (!parent) {
            return true;
        }
        if (isKnownFullReparseBlock(parent->type)) {
            return true;
        }
        parentId = parent->parent;
    }
    return false;
}

bool BlockReparsePlanner::editInsideCodeBlockContent(const MarkdownNode& block, SourceSpan editedSource)
{
    const SourceSpan content = block.content;
    return content.isValid()
        && editedSource.start >= content.start
        && editedSource.end <= content.end;
}

bool BlockReparsePlanner::replacementChangesBlockShape(const QString& replacement)
{
    return replacement.contains(QChar('\n')) || replacement.contains(QChar('\r'));
}

bool BlockReparsePlanner::editTouchesBlockBoundary(const MarkdownNode& block, SourceSpan editedSource)
{
    return editedSource.start <= block.source.start || editedSource.end >= block.source.end;
}

bool BlockReparsePlanner::isLocallyReparsableBlock(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Paragraph
        || type == MarkdownNodeType::Heading
        || type == MarkdownNodeType::CodeBlock
        || type == MarkdownNodeType::ListItem
        || type == MarkdownNodeType::BlockQuote;
}

bool BlockReparsePlanner::isKnownFullReparseBlock(MarkdownNodeType type)
{
    return type == MarkdownNodeType::List
        || type == MarkdownNodeType::ListItem
        || type == MarkdownNodeType::BlockQuote
        || type == MarkdownNodeType::Table
        || type == MarkdownNodeType::TableRow
        || type == MarkdownNodeType::TableCell;
}

} // namespace Muffin
