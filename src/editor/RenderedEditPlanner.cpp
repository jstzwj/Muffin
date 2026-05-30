#include "RenderedEditPlanner.h"

namespace Muffin {

namespace {

QString normalizeReplacement(QString replacement)
{
    replacement.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    replacement.replace(QChar('\r'), QChar('\n'));
    replacement.replace(QChar::ParagraphSeparator, QChar('\n'));
    return replacement;
}

bool isSerializableTextLeaf(const MarkdownNode& node)
{
    return node.type == MarkdownNodeType::Text
        || node.type == MarkdownNodeType::InlineCode
        || node.type == MarkdownNodeType::CodeBlock;
}

bool isFormattedInlineType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Strong || type == MarkdownNodeType::Emphasis || type == MarkdownNodeType::Link;
}

bool isSimpleInlineCode(const MarkdownNode& node)
{
    return node.type == MarkdownNodeType::InlineCode && !node.literal.contains(QChar('`'));
}

SourceSpan editableSourceForNode(const MarkdownNode& node)
{
    if (node.type == MarkdownNodeType::CodeBlock && node.content.isValid()) {
        return node.content;
    }
    return node.source;
}

const MarkdownNode* serializableLeafForSourceSpan(const MarkdownDocument& document, SourceSpan sourceSpan)
{
    const MarkdownNode* best = nullptr;
    for (const MarkdownNode& node : document.nodes()) {
        if (!isSerializableTextLeaf(node)) {
            continue;
        }

        const SourceSpan editableSource = editableSourceForNode(node);
        if (!editableSource.isValid() || sourceSpan.start < editableSource.start || sourceSpan.end > editableSource.end) {
            continue;
        }

        if (!best || editableSource.end - editableSource.start < editableSourceForNode(*best).end - editableSourceForNode(*best).start) {
            best = &node;
        }
    }
    return best;
}

const MarkdownNode* formulaLeafForSourceSpan(const MarkdownDocument& document, SourceSpan sourceSpan)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::FormulaInline && node.source.isValid()
            && node.source.start == sourceSpan.start && node.source.end == sourceSpan.end) {
            return &node;
        }
    }
    return nullptr;
}

SourceSpan editableSourceForSpan(const RenderSpan& span)
{
    return span.editSource.isValid() ? span.editSource : span.source;
}

bool overlapsAtomicSpanBoundary(const RenderSourceMap& sourceMap, SourceSpan sourceSpan)
{
    for (const RenderSpan& span : sourceMap.spans()) {
        const SourceSpan editableSource = editableSourceForSpan(span);
        if (!span.editable || span.editPolicy != RenderSpan::EditPolicy::Atomic || !editableSource.isValid()) {
            continue;
        }
        if (sourceSpan.end <= editableSource.start || sourceSpan.start >= editableSource.end) {
            continue;
        }
        if (sourceSpan.start == editableSource.start && sourceSpan.end == editableSource.end) {
            continue;
        }
        return true;
    }
    return false;
}

bool isPlainTextBlockType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Paragraph || type == MarkdownNodeType::Heading;
}

const MarkdownNode* smallestNodeContainingSourceSpan(const MarkdownDocument& document, SourceSpan sourceSpan)
{
    const MarkdownNode* best = nullptr;
    for (const MarkdownNode& node : document.nodes()) {
        if (!node.source.isValid() || sourceSpan.start < node.source.start || sourceSpan.end > node.source.end) {
            continue;
        }
        if (!best || node.source.end - node.source.start < best->source.end - best->source.start) {
            best = &node;
        }
    }
    return best;
}

std::optional<int> editableSourceStartForNode(const MarkdownNode& node)
{
    if (!node.source.isValid()) {
        return std::nullopt;
    }
    if (node.type == MarkdownNodeType::InlineCode && node.source.end - node.source.start >= 2) {
        return node.source.start + 1;
    }
    if (node.type == MarkdownNodeType::CodeBlock && node.content.isValid()) {
        return node.content.start;
    }
    if (isSerializableTextLeaf(node)) {
        return node.source.start;
    }
    return std::nullopt;
}

std::optional<int> editableSourceEndForNode(const MarkdownNode& node)
{
    if (!node.source.isValid()) {
        return std::nullopt;
    }
    if (node.type == MarkdownNodeType::InlineCode && node.source.end - node.source.start >= 2) {
        return node.source.end - 1;
    }
    if (node.type == MarkdownNodeType::CodeBlock && node.content.isValid()) {
        return node.content.end;
    }
    if (isSerializableTextLeaf(node)) {
        return node.source.end;
    }
    return std::nullopt;
}

int formattedSplitCursorOffset(const MarkdownNode& inlineParent, SourceSpan editSource, int splitOffset)
{
    int markerLength = 1;
    if (inlineParent.type == MarkdownNodeType::Strong) {
        markerLength = 2;
    } else if (inlineParent.type == MarkdownNodeType::Link) {
        markerLength = 1 + inlineParent.url.size() + 2;
    }
    return editSource.start + splitOffset + 2 + markerLength;
}

struct PlainListItemContext {
    const MarkdownNode* item = nullptr;
    const MarkdownNode* list = nullptr;
    int itemPosition = -1;
};

int markerLengthForListPosition(const MarkdownNode& list, int itemPosition, bool taskList)
{
    const int taskMarkerLength = taskList ? 4 : 0;
    if (!list.orderedList) {
        return 2 + taskMarkerLength;
    }

    const int start = list.listStart == 0 ? 1 : list.listStart;
    return QString::number(start + itemPosition).size() + 2 + taskMarkerLength;
}

std::optional<int> continuationIndentForListItemParagraph(const MarkdownDocument& document, const MarkdownNode& paragraph)
{
    const MarkdownNode* item = document.nodeById(paragraph.parent);
    if (!item || item->type != MarkdownNodeType::ListItem) {
        return std::nullopt;
    }

    const MarkdownNode* list = document.nodeById(item->parent);
    if (!list || list->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int itemPosition = list->children.indexOf(item->id);
    if (itemPosition < 0) {
        return std::nullopt;
    }

    return markerLengthForListPosition(*list, itemPosition, item->taskList);
}

std::optional<PlainListItemContext> plainListItemForTextNode(const MarkdownDocument& document, const MarkdownNode& text)
{
    const MarkdownNode* paragraph = document.nodeById(text.parent);
    if (!paragraph || paragraph->type != MarkdownNodeType::Paragraph || paragraph->children.size() != 1) {
        return std::nullopt;
    }

    const MarkdownNode* item = document.nodeById(paragraph->parent);
    if (!item || item->type != MarkdownNodeType::ListItem) {
        return std::nullopt;
    }

    const int paragraphPosition = item->children.indexOf(paragraph->id);
    if (paragraphPosition < 0) {
        return std::nullopt;
    }

    bool canSplitItem = item->children.size() == 1;
    for (int i = paragraphPosition + 1; i < item->children.size(); ++i) {
        const MarkdownNode* child = document.nodeById(item->children.at(i));
        if (child && child->type != MarkdownNodeType::Paragraph) {
            canSplitItem = true;
            break;
        }
    }
    if (!canSplitItem) {
        return std::nullopt;
    }

    const MarkdownNode* list = document.nodeById(item->parent);
    if (!list || list->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int itemPosition = list->children.indexOf(item->id);
    if (itemPosition < 0) {
        return std::nullopt;
    }

    return PlainListItemContext{item, list, itemPosition};
}

std::optional<RenderedEditPlan> planParagraphChildBoundarySplit(const MarkdownDocument& document,
                                                                const RenderSourceMap& sourceMap,
                                                                const RenderedEdit& edit)
{
    if (edit.operation != RenderedEditOperation::Enter || edit.renderedStart != edit.renderedEnd || edit.renderedStart < 0) {
        return std::nullopt;
    }

    const std::optional<int> sourceOffset = sourceMap.editableSourceInsertionPoint(edit.renderedStart);
    if (!sourceOffset) {
        return std::nullopt;
    }

    for (const MarkdownNode& paragraph : document.nodes()) {
        if (paragraph.type != MarkdownNodeType::Paragraph || paragraph.children.size() <= 1) {
            continue;
        }

        for (int i = 1; i < paragraph.children.size(); ++i) {
            const MarkdownNode* child = document.nodeById(paragraph.children.at(i));
            if (!child || !child->source.isValid() || child->source.start != *sourceOffset) {
                continue;
            }

            return RenderedEditPlan{
                RenderedEditPlan::Kind::SplitParagraphAtChildBoundary,
                paragraph.id,
                0,
                {},
                i,
                -1,
                *sourceOffset + 2
            };
        }
    }
    return std::nullopt;
}

std::optional<RenderedEditPlan> planLeafEdit(const MarkdownDocument& document,
                                             const RenderSourceMap& sourceMap,
                                             const RenderedEdit& edit)
{
    if (edit.operation != RenderedEditOperation::InsertText
        && edit.operation != RenderedEditOperation::ReplaceSelection
        && edit.operation != RenderedEditOperation::Paste) {
        return std::nullopt;
    }

    int renderedStart = edit.renderedStart;
    int renderedEnd = edit.renderedEnd;
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }
    if (renderedStart < 0 || renderedEnd < 0) {
        return std::nullopt;
    }

    const std::optional<SourceSpan> sourceSpan = sourceMap.editableSourceSpanForRenderedRange(renderedStart, renderedEnd);
    if (!sourceSpan) {
        return std::nullopt;
    }

    const QString replacement = normalizeReplacement(edit.replacement);
    if (const MarkdownNode* formulaNode = formulaLeafForSourceSpan(document, *sourceSpan)) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::ReplaceFormulaNode,
            formulaNode->id,
            0,
            replacement,
            -1,
            -1,
            sourceSpan->start + static_cast<int>(replacement.size())
        };
    }
    if (overlapsAtomicSpanBoundary(sourceMap, *sourceSpan)) {
        return std::nullopt;
    }

    const MarkdownNode* node = serializableLeafForSourceSpan(document, *sourceSpan);
    if (!node) {
        return std::nullopt;
    }

    const SourceSpan editSource = editableSourceForNode(*node);
    if (!editSource.isValid() || sourceSpan->start < editSource.start || sourceSpan->end > editSource.end) {
        return std::nullopt;
    }

    QString literal = node->literal;
    const int replaceStart = qBound(0, sourceSpan->start - editSource.start, literal.size());
    const int replaceEnd = qBound(replaceStart, sourceSpan->end - editSource.start, literal.size());
    literal.replace(replaceStart, replaceEnd - replaceStart, replacement);

    return RenderedEditPlan{
        RenderedEditPlan::Kind::ReplaceNodeLiteral,
        node->id,
        0,
        literal,
        -1,
        -1,
        sourceSpan->start + static_cast<int>(replacement.size())
    };
}

std::optional<RenderedEditPlan> planCodeBlockBoundaryEdit(const MarkdownDocument& document,
                                                          const RenderSourceMap& sourceMap,
                                                          const RenderedEdit& edit)
{
    if (edit.renderedStart < 0 || edit.renderedEnd < 0 || edit.renderedStart != edit.renderedEnd) {
        return std::nullopt;
    }
    if (edit.operation != RenderedEditOperation::Enter
        && edit.operation != RenderedEditOperation::Backspace
        && edit.operation != RenderedEditOperation::Delete) {
        return std::nullopt;
    }

    const std::optional<RenderSpan> span = sourceMap.editableSpanAtRenderedPosition(edit.renderedStart);
    const std::optional<int> sourceOffset = sourceMap.editableSourceInsertionPoint(edit.renderedStart);
    if (!span || !sourceOffset || span->nodeId == 0 || span->kind != RenderSpan::Kind::CodeBlock) {
        return std::nullopt;
    }

    const MarkdownNode* node = document.nodeById(span->nodeId);
    if (!node || node->type != MarkdownNodeType::CodeBlock) {
        return std::nullopt;
    }

    const SourceSpan editSource = editableSourceForNode(*node);
    if (!editSource.isValid() || *sourceOffset < editSource.start || *sourceOffset > editSource.end) {
        return std::nullopt;
    }

    QString literal = node->literal;
    int cursorSourceOffset = *sourceOffset;
    if (edit.operation == RenderedEditOperation::Backspace) {
        if (*sourceOffset <= editSource.start) {
            return RenderedEditPlan{RenderedEditPlan::Kind::NoOp, 0, 0, {}, -1, -1, editSource.start};
        }
        const int removeOffset = qBound(0, *sourceOffset - editSource.start - 1, literal.size());
        if (removeOffset >= literal.size()) {
            return std::nullopt;
        }
        literal.remove(removeOffset, 1);
        cursorSourceOffset = *sourceOffset - 1;
    } else if (edit.operation == RenderedEditOperation::Delete) {
        if (*sourceOffset >= editSource.end) {
            return RenderedEditPlan{RenderedEditPlan::Kind::NoOp, 0, 0, {}, -1, -1, editSource.end};
        }
        const int removeOffset = qBound(0, *sourceOffset - editSource.start, literal.size());
        if (removeOffset >= literal.size()) {
            return std::nullopt;
        }
        literal.remove(removeOffset, 1);
    } else {
        const int insertOffset = qBound(0, *sourceOffset - editSource.start, literal.size());
        literal.insert(insertOffset, QChar('\n'));
        cursorSourceOffset = *sourceOffset + 1;
    }

    return RenderedEditPlan{
        RenderedEditPlan::Kind::ReplaceNodeLiteral,
        node->id,
        0,
        literal,
        -1,
        -1,
        cursorSourceOffset
    };
}

std::optional<RenderedEditPlan> planSourceSpanEdit(const MarkdownDocument& document, const RenderedEdit& edit)
{
    if (edit.targetKind != RenderedEdit::TargetKind::SourceSpan) {
        return std::nullopt;
    }
    if (edit.operation != RenderedEditOperation::InsertText
        && edit.operation != RenderedEditOperation::ReplaceSelection
        && edit.operation != RenderedEditOperation::Paste
        && edit.operation != RenderedEditOperation::Backspace
        && edit.operation != RenderedEditOperation::Delete) {
        return std::nullopt;
    }

    SourceSpan sourceSpan = edit.sourceSpan;
    if (!sourceSpan.isValid()) {
        return std::nullopt;
    }
    if (sourceSpan.start > sourceSpan.end) {
        std::swap(sourceSpan.start, sourceSpan.end);
    }
    sourceSpan.start = qBound(0, sourceSpan.start, document.source().size());
    sourceSpan.end = qBound(sourceSpan.start, sourceSpan.end, document.source().size());

    const MarkdownNode* affectedNode = smallestNodeContainingSourceSpan(document, sourceSpan);
    return RenderedEditPlan{
        RenderedEditPlan::Kind::ReplaceSourceSpan,
        affectedNode ? affectedNode->id : 0,
        0,
        normalizeReplacement(edit.replacement),
        -1,
        -1,
        sourceSpan.start + static_cast<int>(normalizeReplacement(edit.replacement).size()),
        {},
        sourceSpan
    };
}

std::optional<RenderedEditPlan> planParagraphSplit(const MarkdownDocument& document,
                                                   const RenderSourceMap& sourceMap,
                                                   const RenderedEdit& edit)
{
    if (edit.operation != RenderedEditOperation::Enter || edit.renderedStart < 0 || edit.renderedEnd < 0) {
        return std::nullopt;
    }

    int renderedStart = edit.renderedStart;
    int renderedEnd = edit.renderedEnd;
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }

    const std::optional<RenderSpan> span = sourceMap.editableSpanAtRenderedPosition(renderedStart);
    const std::optional<SourceSpan> sourceSpan = sourceMap.editableSourceSpanForRenderedRange(renderedStart, renderedEnd);
    if (!span || !sourceSpan || span->nodeId == 0 || span->editPolicy != RenderSpan::EditPolicy::LinearText
        || !span->containsRenderedRange(renderedStart, renderedEnd)) {
        return std::nullopt;
    }

    const MarkdownNode* node = document.nodeById(span->nodeId);
    if (!node || (node->type != MarkdownNodeType::Text && node->type != MarkdownNodeType::InlineCode)) {
        return std::nullopt;
    }

    const MarkdownNode* inlineParent = document.nodeById(node->parent);
    const bool insideInlineCode = node->type == MarkdownNodeType::InlineCode;
    if (insideInlineCode && !isSimpleInlineCode(*node)) {
        return std::nullopt;
    }
    const std::optional<PlainListItemContext> listItem = insideInlineCode ? std::nullopt : plainListItemForTextNode(document, *node);
    const bool insideFormatted = inlineParent && node->type == MarkdownNodeType::Text
        && isFormattedInlineType(inlineParent->type);
    const MarkdownNode* paragraph = insideFormatted ? document.nodeById(inlineParent->parent) : inlineParent;
    if (!paragraph || !isPlainTextBlockType(paragraph->type)) {
        return std::nullopt;
    }
    if (!insideFormatted && !insideInlineCode && paragraph->type != MarkdownNodeType::Paragraph
        && paragraph->children.size() != 1) {
        return std::nullopt;
    }

    const SourceSpan editSource = span->editSource.isValid() ? span->editSource : span->source;
    if (!editSource.isValid() || sourceSpan->start < editSource.start || sourceSpan->end > editSource.end) {
        return std::nullopt;
    }

    const int splitOffset = qBound(0, sourceSpan->start - editSource.start, node->literal.size());
    const int splitEndOffset = qBound(splitOffset, sourceSpan->end - editSource.start, node->literal.size());
    if (splitOffset <= 0 || splitEndOffset >= node->literal.size()) {
        return std::nullopt;
    }

    if (listItem) {
        const int nextMarkerLength = markerLengthForListPosition(*listItem->list,
                                                                 listItem->itemPosition + 1,
                                                                 listItem->item->taskList);
        return RenderedEditPlan{
            RenderedEditPlan::Kind::SplitTextNodeIntoListItems,
            node->id,
            0,
            {},
            splitOffset,
            splitEndOffset,
            editSource.start + splitOffset + 1 + nextMarkerLength
        };
    }

    if (paragraph->type == MarkdownNodeType::Heading) {
        const int markerLength = qMax(1, paragraph->headingLevel) + 1;
        return RenderedEditPlan{
            RenderedEditPlan::Kind::SplitTextNodeIntoHeading,
            node->id,
            0,
            {},
            splitOffset,
            splitEndOffset,
            editSource.start + splitOffset + 2 + markerLength
        };
    }

    if (insideInlineCode) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs,
            node->id,
            0,
            {},
            splitOffset,
            splitEndOffset,
            editSource.start + splitOffset + 3
        };
    }

    if (insideFormatted) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs,
            node->id,
            0,
            {},
            splitOffset,
            splitEndOffset,
            formattedSplitCursorOffset(*inlineParent, editSource, splitOffset)
        };
    }

    int cursorSourceOffset = editSource.start + splitOffset + 2;
    if (const std::optional<int> continuationIndent = continuationIndentForListItemParagraph(document, *paragraph)) {
        cursorSourceOffset += *continuationIndent;
    }

    if (paragraph->children.size() != 1) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs,
            node->id,
            0,
            {},
            splitOffset,
            splitEndOffset,
            cursorSourceOffset
        };
    }

    return RenderedEditPlan{
        RenderedEditPlan::Kind::SplitTextNodeIntoParagraphs,
        node->id,
        0,
        {},
        splitOffset,
        splitEndOffset,
        cursorSourceOffset
    };
}

const MarkdownNode* paragraphForBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* paragraph = document.nodeById(block.nodeId);
    if (!paragraph || paragraph->type != MarkdownNodeType::Paragraph) {
        return nullptr;
    }

    return paragraph;
}

int firstEditableSourceOffsetInBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* node = document.nodeById(block.nodeId);
    if (!node) {
        return block.content.isValid() ? block.content.start : block.source.start;
    }

    QVector<MarkdownNodeId> pending = node->children;
    while (!pending.isEmpty()) {
        const MarkdownNode* child = document.nodeById(pending.takeFirst());
        if (!child) {
            continue;
        }
        if (const std::optional<int> editableStart = editableSourceStartForNode(*child)) {
            return *editableStart;
        }
        for (int i = child->children.size() - 1; i >= 0; --i) {
            pending.prepend(child->children.at(i));
        }
    }

    return block.content.isValid() ? block.content.start : block.source.start;
}

int lastEditableSourceOffsetInBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* node = document.nodeById(block.nodeId);
    if (!node) {
        return block.content.isValid() ? block.content.end : block.source.end;
    }

    QVector<MarkdownNodeId> pending = node->children;
    while (!pending.isEmpty()) {
        const MarkdownNode* child = document.nodeById(pending.takeLast());
        if (!child) {
            continue;
        }
        if (const std::optional<int> editableEnd = editableSourceEndForNode(*child)) {
            return *editableEnd;
        }
        for (MarkdownNodeId grandchildId : child->children) {
            pending.append(grandchildId);
        }
    }

    return block.content.isValid() ? block.content.end : block.source.end;
}

const MarkdownNode* plainTextBlockForBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* textBlock = document.nodeById(block.nodeId);
    if (!textBlock || !isPlainTextBlockType(textBlock->type) || textBlock->children.size() != 1) {
        return nullptr;
    }

    const MarkdownNode* text = document.nodeById(textBlock->children.first());
    if (!text || text->type != MarkdownNodeType::Text) {
        return nullptr;
    }

    return textBlock;
}

std::optional<PlainListItemContext> plainListItemForBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* item = document.nodeById(block.nodeId);
    if (item && item->type == MarkdownNodeType::Paragraph) {
        item = document.nodeById(item->parent);
    }
    if (!item || item->type != MarkdownNodeType::ListItem) {
        return std::nullopt;
    }

    bool hasPlainParagraph = false;
    for (MarkdownNodeId childId : item->children) {
        const MarkdownNode* paragraph = document.nodeById(childId);
        if (!paragraph || paragraph->type != MarkdownNodeType::Paragraph || paragraph->children.size() != 1) {
            continue;
        }

        const MarkdownNode* text = document.nodeById(paragraph->children.first());
        if (text && text->type == MarkdownNodeType::Text) {
            hasPlainParagraph = true;
            break;
        }
    }
    if (!hasPlainParagraph) {
        return std::nullopt;
    }

    const MarkdownNode* list = document.nodeById(item->parent);
    if (!list || list->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int itemPosition = list->children.indexOf(item->id);
    if (itemPosition < 0) {
        return std::nullopt;
    }

    return PlainListItemContext{item, list, itemPosition};
}

std::optional<PlainListItemContext> emptyPlainListItemForBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* item = document.nodeById(block.nodeId);
    if (!item || item->type != MarkdownNodeType::ListItem || item->children.size() > 1) {
        return std::nullopt;
    }

    if (item->children.size() == 1) {
        const MarkdownNode* paragraph = document.nodeById(item->children.first());
        if (!paragraph || paragraph->type != MarkdownNodeType::Paragraph || paragraph->children.size() > 1) {
            return std::nullopt;
        }

        if (paragraph->children.size() == 1) {
            const MarkdownNode* text = document.nodeById(paragraph->children.first());
            if (!text || text->type != MarkdownNodeType::Text || !text->literal.isEmpty()) {
                return std::nullopt;
            }
        }
    }

    const MarkdownNode* list = document.nodeById(item->parent);
    if (!list || list->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int itemPosition = list->children.indexOf(item->id);
    if (itemPosition < 0) {
        return std::nullopt;
    }

    return PlainListItemContext{item, list, itemPosition};
}

std::optional<PlainListItemContext> listItemContainingBlock(const MarkdownDocument& document, const MarkdownBlock& block)
{
    const MarkdownNode* node = document.nodeById(block.nodeId);
    while (node && node->type != MarkdownNodeType::ListItem) {
        node = document.nodeById(node->parent);
    }
    if (!node || node->type != MarkdownNodeType::ListItem) {
        return std::nullopt;
    }

    const MarkdownNode* list = document.nodeById(node->parent);
    if (!list || list->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int itemPosition = list->children.indexOf(node->id);
    if (itemPosition < 0) {
        return std::nullopt;
    }

    return PlainListItemContext{node, list, itemPosition};
}

QVector<PlainListItemContext> listItemsInRenderedSelection(const MarkdownDocument& document,
                                                          const QVector<MarkdownBlock>& blocks,
                                                          int renderedStart,
                                                          int renderedEnd)
{
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }

    QVector<PlainListItemContext> items;
    for (const MarkdownBlock& block : blocks) {
        if (block.renderedEnd < renderedStart || block.renderedStart > renderedEnd) {
            continue;
        }

        const std::optional<PlainListItemContext> item = listItemContainingBlock(document, block);
        if (!item) {
            continue;
        }
        if (!items.isEmpty() && items.last().item->id == item->item->id) {
            continue;
        }
        items.append(*item);
    }

    QVector<PlainListItemContext> deepestItems;
    for (const PlainListItemContext& candidate : items) {
        bool hasSelectedDescendant = false;
        for (const PlainListItemContext& other : items) {
            if (candidate.item->id == other.item->id) {
                continue;
            }

            const MarkdownNode* node = other.item;
            while (node && node->parent != 0) {
                if (node->parent == candidate.item->id) {
                    hasSelectedDescendant = true;
                    break;
                }
                node = document.nodeById(node->parent);
            }
            if (hasSelectedDescendant) {
                break;
            }
        }
        if (!hasSelectedDescendant) {
            deepestItems.append(candidate);
        }
    }
    return deepestItems;
}

bool isContiguousSameParentListSelection(const QVector<PlainListItemContext>& items)
{
    if (items.isEmpty()) {
        return false;
    }

    const MarkdownNodeId listId = items.first().list->id;
    for (int i = 0; i < items.size(); ++i) {
        if (!items.at(i).item || !items.at(i).list || items.at(i).list->id != listId) {
            return false;
        }
        if (i > 0 && items.at(i).itemPosition != items.at(i - 1).itemPosition + 1) {
            return false;
        }
    }
    return true;
}

QVector<MarkdownNodeId> itemIdsForSelection(const QVector<PlainListItemContext>& items)
{
    QVector<MarkdownNodeId> ids;
    ids.reserve(items.size());
    for (const PlainListItemContext& item : items) {
        ids.append(item.item->id);
    }
    return ids;
}

int firstEditableSourceOffsetInListItem(const MarkdownDocument& document, const MarkdownNode& item)
{
    for (MarkdownNodeId childId : item.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }
        if (child->type == MarkdownNodeType::Paragraph) {
            for (MarkdownNodeId inlineId : child->children) {
                const MarkdownNode* inlineNode = document.nodeById(inlineId);
                if (inlineNode && inlineNode->source.isValid()) {
                    return inlineNode->source.start;
                }
            }
        }
    }
    return item.content.isValid() ? item.content.start : item.source.start;
}

std::optional<MarkdownBlock> blockBeforeSourceOffset(const QVector<MarkdownBlock>& blocks, int sourceOffset)
{
    std::optional<MarkdownBlock> best;
    for (const MarkdownBlock& block : blocks) {
        if (block.source.isValid() && block.source.end <= sourceOffset && (!best || block.source.end > best->source.end)) {
            best = block;
        }
    }
    return best;
}

std::optional<MarkdownBlock> blockAfterSourceOffset(const QVector<MarkdownBlock>& blocks, int sourceOffset)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.source.isValid() && block.source.start >= sourceOffset) {
            return block;
        }
    }
    return std::nullopt;
}

std::optional<MarkdownBlock> blockContainingContentOffset(const QVector<MarkdownBlock>& blocks, int sourceOffset)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.source.isValid() && block.content.isValid()
            && sourceOffset >= block.content.start && sourceOffset <= block.source.end) {
            return block;
        }
    }
    return std::nullopt;
}

std::optional<RenderedEditPlan> planExitEmptyListItem(const MarkdownDocument& document,
                                                      const RenderSourceMap& sourceMap,
                                                      const QVector<MarkdownBlock>& blocks,
                                                      const RenderedEdit& edit)
{
    if ((edit.operation != RenderedEditOperation::Enter && edit.operation != RenderedEditOperation::Backspace)
        || edit.renderedStart != edit.renderedEnd || edit.renderedStart < 0) {
        return std::nullopt;
    }

    Q_UNUSED(sourceMap);

    std::optional<MarkdownBlock> block;
    for (const MarkdownBlock& candidate : blocks) {
        if (candidate.renderedStart == edit.renderedStart && candidate.nodeId != 0) {
            block = candidate;
            break;
        }
    }

    if (!block || block->kind != RenderSpan::Kind::List || !block->content.isValid()
        || block->content.start != block->content.end || edit.renderedStart != block->renderedStart) {
        return std::nullopt;
    }

    const std::optional<PlainListItemContext> listItem = emptyPlainListItemForBlock(document, *block);
    if (!listItem) {
        return std::nullopt;
    }

    int cursorSourceOffset = qMax(0, block->source.start - (listItem->itemPosition > 0 ? 1 : 0));
    if (listItem->itemPosition > 0) {
        if (const MarkdownNode* previousItem = document.nodeById(listItem->list->children.at(listItem->itemPosition - 1))) {
            if (previousItem->source.isValid()) {
                cursorSourceOffset = previousItem->source.end;
            }
        }
    }

    return RenderedEditPlan{
        RenderedEditPlan::Kind::ExitEmptyListItem,
        listItem->item->id,
        0,
        {},
        -1,
        -1,
        cursorSourceOffset
    };
}

std::optional<RenderedEditPlan> planParagraphMerge(const MarkdownDocument& document,
                                                   const RenderSourceMap& sourceMap,
                                                   const QVector<MarkdownBlock>& blocks,
                                                   const RenderedEdit& edit)
{
    if ((edit.operation != RenderedEditOperation::Backspace && edit.operation != RenderedEditOperation::Delete)
        || edit.renderedStart != edit.renderedEnd || edit.renderedStart < 0) {
        return std::nullopt;
    }

    std::optional<MarkdownBlock> previous;
    std::optional<MarkdownBlock> next;
    std::optional<int> sourceOffset;
    if (edit.operation == RenderedEditOperation::Backspace) {
        sourceOffset = sourceMap.editableSourceInsertionPoint(edit.renderedStart, RenderSourceMap::Bias::Forward);
        if (!sourceOffset) {
            return std::nullopt;
        }

        next = blockContainingContentOffset(blocks, *sourceOffset);
        if (!next || !next->content.isValid() || *sourceOffset != firstEditableSourceOffsetInBlock(document, *next)) {
            return std::nullopt;
        }
        previous = blockBeforeSourceOffset(blocks, next->source.start);
    } else {
        sourceOffset = sourceMap.editableSourceInsertionPoint(edit.renderedStart, RenderSourceMap::Bias::Backward);
        if (!sourceOffset) {
            return std::nullopt;
        }

        previous = blockBeforeSourceOffset(blocks, *sourceOffset + 1);
        if (!previous || (*sourceOffset != lastEditableSourceOffsetInBlock(document, *previous)
            && *sourceOffset != previous->source.end)) {
            for (const MarkdownBlock& block : blocks) {
                if (block.editable && block.renderedEnd == edit.renderedStart) {
                    previous = block;
                    break;
                }
            }
            if (!previous || previous->renderedEnd != edit.renderedStart) {
                return std::nullopt;
            }
        }
        next = blockAfterSourceOffset(blocks, previous->source.end);
    }

    if (!previous || !next || previous->nodeId == 0 || next->nodeId == 0) {
        return std::nullopt;
    }

    const MarkdownNode* previousParagraph = paragraphForBlock(document, *previous);
    const MarkdownNode* nextParagraph = paragraphForBlock(document, *next);
    if (previousParagraph && nextParagraph && previousParagraph->parent == nextParagraph->parent) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::MergeParagraphs,
            previousParagraph->id,
            nextParagraph->id,
            {},
            -1,
            -1,
            previous->source.end
        };
    }

    const MarkdownNode* previousTextBlock = plainTextBlockForBlock(document, *previous);
    const MarkdownNode* nextTextBlock = plainTextBlockForBlock(document, *next);
    if (previousTextBlock && nextTextBlock) {
        return RenderedEditPlan{
            RenderedEditPlan::Kind::MergeTextBlocks,
            previousTextBlock->id,
            nextTextBlock->id,
            {},
            -1,
            -1,
            previous->source.end
        };
    }

    const std::optional<PlainListItemContext> previousItem = plainListItemForBlock(document, *previous);
    const std::optional<PlainListItemContext> nextItem = plainListItemForBlock(document, *next);
    if (!previousItem || !nextItem || previousItem->item->parent != nextItem->item->parent) {
        return std::nullopt;
    }

    return RenderedEditPlan{
        RenderedEditPlan::Kind::MergeListItems,
        previousItem->item->id,
        nextItem->item->id,
        {},
        -1,
        -1,
        previous->source.end
    };
}

std::optional<RenderedEditPlan> planUnsupportedBoundaryNoOp(const RenderSourceMap& sourceMap,
                                                            const QVector<MarkdownBlock>& blocks,
                                                            const RenderedEdit& edit)
{
    if (edit.operation != RenderedEditOperation::Delete || edit.renderedStart != edit.renderedEnd || edit.renderedStart < 0) {
        return std::nullopt;
    }

    const std::optional<int> sourceOffset = sourceMap.editableSourceInsertionPoint(edit.renderedStart, RenderSourceMap::Bias::Backward);
    if (!sourceOffset) {
        return std::nullopt;
    }

    const std::optional<MarkdownBlock> previous = blockBeforeSourceOffset(blocks, *sourceOffset + 1);
    if (!previous || *sourceOffset != previous->source.end) {
        return std::nullopt;
    }

    const std::optional<MarkdownBlock> next = blockAfterSourceOffset(blocks, previous->source.end);
    if (!next || next->kind != RenderSpan::Kind::Table) {
        return std::nullopt;
    }

    return RenderedEditPlan{
        RenderedEditPlan::Kind::NoOp,
        0,
        0,
        {},
        -1,
        -1,
        previous->source.end
    };
}

std::optional<RenderedEditPlan> planListIndentOutdent(const MarkdownDocument& document,
                                                      const QVector<MarkdownBlock>& blocks,
                                                      const RenderedEdit& edit)
{
    if ((edit.operation != RenderedEditOperation::Indent && edit.operation != RenderedEditOperation::Outdent)
        || edit.renderedStart < 0 || edit.renderedEnd < 0) {
        return std::nullopt;
    }

    if (edit.renderedStart != edit.renderedEnd) {
        const QVector<PlainListItemContext> selectedItems =
            listItemsInRenderedSelection(document, blocks, edit.renderedStart, edit.renderedEnd);
        if (selectedItems.size() <= 1 || !isContiguousSameParentListSelection(selectedItems)) {
            return std::nullopt;
        }

        if (edit.operation == RenderedEditOperation::Indent) {
            if (selectedItems.first().itemPosition <= 0) {
                return std::nullopt;
            }
            return RenderedEditPlan{
                RenderedEditPlan::Kind::DemoteListItem,
                selectedItems.first().item->id,
                0,
                {},
                -1,
                -1,
                firstEditableSourceOffsetInListItem(document, *selectedItems.last().item),
                itemIdsForSelection(selectedItems)
            };
        }

        const MarkdownNode* parentItem = document.nodeById(selectedItems.first().list->parent);
        if (!parentItem || parentItem->type != MarkdownNodeType::ListItem) {
            return std::nullopt;
        }
        return RenderedEditPlan{
            RenderedEditPlan::Kind::PromoteListItem,
            selectedItems.first().item->id,
            0,
            {},
            -1,
            -1,
            firstEditableSourceOffsetInListItem(document, *selectedItems.last().item),
            itemIdsForSelection(selectedItems)
        };
    }

    std::optional<MarkdownBlock> best;
    for (const MarkdownBlock& block : blocks) {
        if (!block.source.isValid() || block.nodeId == 0) {
            continue;
        }
        if (edit.renderedStart < block.renderedStart || edit.renderedStart > block.renderedEnd) {
            continue;
        }
        if (!best || block.renderedEnd - block.renderedStart < best->renderedEnd - best->renderedStart) {
            best = block;
        }
    }
    if (!best) {
        return std::nullopt;
    }

    const std::optional<PlainListItemContext> listItem = listItemContainingBlock(document, *best);
    if (!listItem) {
        return std::nullopt;
    }

    if (edit.operation == RenderedEditOperation::Indent) {
        if (listItem->itemPosition <= 0) {
            return std::nullopt;
        }
        const int cursorSourceOffset = firstEditableSourceOffsetInListItem(document, *listItem->item)
            + markerLengthForListPosition(*listItem->list, listItem->itemPosition - 1, false);
        return RenderedEditPlan{
            RenderedEditPlan::Kind::DemoteListItem,
            listItem->item->id,
            0,
            {},
            -1,
            -1,
            cursorSourceOffset
        };
    }

    const MarkdownNode* parentItem = document.nodeById(listItem->list->parent);
    if (!parentItem || parentItem->type != MarkdownNodeType::ListItem) {
        return std::nullopt;
    }

    const MarkdownNode* parentList = document.nodeById(parentItem->parent);
    if (!parentList || parentList->type != MarkdownNodeType::List) {
        return std::nullopt;
    }

    const int parentItemPosition = parentList->children.indexOf(parentItem->id);
    if (parentItemPosition < 0) {
        return std::nullopt;
    }

    const int cursorSourceOffset = firstEditableSourceOffsetInListItem(document, *listItem->item)
        - markerLengthForListPosition(*parentList, parentItemPosition, false);
    return RenderedEditPlan{
        RenderedEditPlan::Kind::PromoteListItem,
        listItem->item->id,
        0,
        {},
        -1,
        -1,
        cursorSourceOffset
    };
}

} // namespace

std::optional<RenderedEditPlan> RenderedEditPlanner::plan(const MarkdownDocument& document,
                                                          const RenderSourceMap& sourceMap,
                                                          const QVector<MarkdownBlock>& blocks,
                                                          const RenderedEdit& edit)
{
    if (std::optional<RenderedEditPlan> plan = planSourceSpanEdit(document, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planExitEmptyListItem(document, sourceMap, blocks, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planCodeBlockBoundaryEdit(document, sourceMap, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planParagraphChildBoundarySplit(document, sourceMap, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planParagraphSplit(document, sourceMap, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planParagraphMerge(document, sourceMap, blocks, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planUnsupportedBoundaryNoOp(sourceMap, blocks, edit)) {
        return plan;
    }
    if (std::optional<RenderedEditPlan> plan = planListIndentOutdent(document, blocks, edit)) {
        return plan;
    }
    return planLeafEdit(document, sourceMap, edit);
}

} // namespace Muffin
