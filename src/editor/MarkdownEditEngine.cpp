#include "MarkdownEditEngine.h"

#include "editor/EditTransactionValidator.h"
#include "editor/HeadingCommandHandler.h"
#include "editor/ListCommandHandler.h"
#include "editor/QuoteCommandHandler.h"
#include "editor/RenderedEditPlanner.h"
#include "editor/StyleCommandHandler.h"
#include "model/MarkdownSerializer.h"
#include "model/MarkdownTransform.h"

namespace Muffin {

namespace {

std::optional<DocumentTransformResult> applyPlan(const MarkdownDocument& document, const RenderedEditPlan& plan)
{
    switch (plan.kind) {
    case RenderedEditPlan::Kind::ReplaceNodeLiteral:
        return MarkdownTransform::replaceNodeLiteralWithResult(document,
                                                               plan.primaryNodeId,
                                                               plan.literal,
                                                               plan.cursorSourceOffset);
    case RenderedEditPlan::Kind::ReplaceFormulaNode:
        return MarkdownTransform::replaceFormulaNodeWithResult(document,
                                                               plan.primaryNodeId,
                                                               plan.literal,
                                                               plan.cursorSourceOffset);
    case RenderedEditPlan::Kind::ReplaceSourceSpan:
        return std::nullopt;
    case RenderedEditPlan::Kind::SplitTextNodeIntoParagraphs:
        return MarkdownTransform::splitTextNodeIntoParagraphsWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::SplitParagraphAtChildBoundary:
        return MarkdownTransform::splitParagraphAtChildBoundaryWithResult(document, plan.primaryNodeId, plan.splitOffset);
    case RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs:
        return MarkdownTransform::splitInlineNodeIntoParagraphsWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::SplitFormattedTextIntoParagraphs:
        return MarkdownTransform::splitFormattedTextIntoParagraphsWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::SplitInlineCodeIntoParagraphs:
        return MarkdownTransform::splitInlineCodeIntoParagraphsWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::SplitTextNodeIntoHeading:
        return MarkdownTransform::splitTextNodeIntoHeadingWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::MergeParagraphs:
        return MarkdownTransform::mergeParagraphsWithResult(document,
                                                            plan.primaryNodeId,
                                                            plan.secondaryNodeId);
    case RenderedEditPlan::Kind::MergeTextBlocks:
        return MarkdownTransform::mergeTextBlocksWithResult(document,
                                                            plan.primaryNodeId,
                                                            plan.secondaryNodeId);
    case RenderedEditPlan::Kind::SplitTextNodeIntoListItems:
        return MarkdownTransform::splitTextNodeIntoListItemsWithResult(document, plan.primaryNodeId, plan.splitOffset, plan.splitEndOffset);
    case RenderedEditPlan::Kind::MergeListItems:
        return MarkdownTransform::mergeListItemsWithResult(document,
                                                           plan.primaryNodeId,
                                                           plan.secondaryNodeId);
    case RenderedEditPlan::Kind::ExitEmptyListItem:
        return MarkdownTransform::removeEmptyListItemWithResult(document,
                                                                plan.primaryNodeId);
    case RenderedEditPlan::Kind::DemoteListItem:
        if (!plan.nodeIds.isEmpty()) {
            return MarkdownTransform::demoteListItemsWithResult(document,
                                                                plan.nodeIds);
        }
        return MarkdownTransform::demoteListItemWithResult(document,
                                                           plan.primaryNodeId);
    case RenderedEditPlan::Kind::PromoteListItem:
        if (!plan.nodeIds.isEmpty()) {
            return MarkdownTransform::promoteListItemsWithResult(document,
                                                                 plan.nodeIds);
        }
        return MarkdownTransform::promoteListItemWithResult(document,
                                                            plan.primaryNodeId);
    case RenderedEditPlan::Kind::NoOp:
        return std::nullopt;
    }
    return std::nullopt;
}

bool isNoOpPlan(const RenderedEditPlan& plan)
{
    return plan.kind == RenderedEditPlan::Kind::NoOp;
}

QString normalizeParagraphBoundaryWhitespace(QString markdown)
{
    QString normalized;
    normalized.reserve(markdown.size());
    for (int i = 0; i < markdown.size(); ++i) {
        if ((markdown.at(i) == QChar(' ') || markdown.at(i) == QChar('\t'))
            && i + 1 < markdown.size() && markdown.at(i + 1) == QChar('\n')) {
            int j = i + 1;
            while (j < markdown.size() && markdown.at(j) == QChar('\n')) {
                ++j;
            }
            if (j - (i + 1) >= 2) {
                continue;
            }
        }
        normalized.append(markdown.at(i));
    }
    return normalized;
}

QString normalizeBlockquoteBlankLines(QString markdown)
{
    QString normalized;
    normalized.reserve(markdown.size());

    int lineStart = 0;
    while (lineStart < markdown.size()) {
        int lineEnd = markdown.indexOf(QChar('\n'), lineStart);
        if (lineEnd < 0) {
            lineEnd = markdown.size();
        }

        QStringView line = QStringView(markdown).mid(lineStart, lineEnd - lineStart);
        int i = 0;
        while (i < line.size() && (line.at(i) == QChar(' ') || line.at(i) == QChar('\t'))) {
            ++i;
        }
        if (i < line.size() && line.at(i) == QChar('>')) {
            int j = i + 1;
            if (j < line.size() && line.at(j) == QChar(' ')) {
                ++j;
            }
            bool blankQuoteLine = true;
            while (j < line.size()) {
                if (line.at(j) != QChar(' ') && line.at(j) != QChar('\t')) {
                    blankQuoteLine = false;
                    break;
                }
                ++j;
            }
            if (blankQuoteLine) {
                normalized.append(line.left(i + 1));
            } else {
                normalized.append(line);
            }
        } else {
            normalized.append(line);
        }

        if (lineEnd < markdown.size()) {
            normalized.append(QChar('\n'));
        }
        lineStart = lineEnd + 1;
    }

    return normalized;
}

QString normalizeSerializableMarkdown(QString markdown)
{
    return normalizeBlockquoteBlankLines(normalizeParagraphBoundaryWhitespace(std::move(markdown)));
}

std::optional<SourceSelection> sourceSelectionForListItems(const MarkdownSerializationResult& serialized,
                                                           const MarkdownDocument& document,
                                                           const QVector<MarkdownNodeId>& itemIds)
{
    if (itemIds.isEmpty()) {
        return std::nullopt;
    }

    int start = -1;
    int end = -1;
    const auto firstEditableLeaf = [&](const MarkdownNode& node) -> const MarkdownNode* {
        QVector<MarkdownNodeId> pending{node.id};
        while (!pending.isEmpty()) {
            const MarkdownNode* current = document.nodeById(pending.takeFirst());
            if (!current) {
                continue;
            }
            if (current->type == MarkdownNodeType::Text || current->type == MarkdownNodeType::InlineCode) {
                return current;
            }
            for (int i = current->children.size() - 1; i >= 0; --i) {
                pending.prepend(current->children.at(i));
            }
        }
        return nullptr;
    };
    const auto lastEditableLeaf = [&](const MarkdownNode& node) -> const MarkdownNode* {
        QVector<MarkdownNodeId> pending{node.id};
        while (!pending.isEmpty()) {
            const MarkdownNode* current = document.nodeById(pending.takeLast());
            if (!current) {
                continue;
            }
            if (current->type == MarkdownNodeType::Text || current->type == MarkdownNodeType::InlineCode) {
                return current;
            }
            for (MarkdownNodeId childId : current->children) {
                pending.append(childId);
            }
        }
        return nullptr;
    };

    for (MarkdownNodeId itemId : itemIds) {
        const MarkdownNode* item = document.nodeById(itemId);
        if (!item || item->type != MarkdownNodeType::ListItem) {
            return std::nullopt;
        }

        std::optional<int> itemStart;
        std::optional<int> itemEnd;
        for (MarkdownNodeId childId : item->children) {
            const MarkdownNode* child = document.nodeById(childId);
            if (!child || child->type != MarkdownNodeType::Paragraph || child->children.isEmpty()) {
                continue;
            }

            const MarkdownNode* firstInline = firstEditableLeaf(*child);
            const MarkdownNode* lastInline = lastEditableLeaf(*child);
            if (!firstInline || !lastInline) {
                continue;
            }

            itemStart = serialized.sourceOffsetForNodeOffset(firstInline->id, 0);
            itemEnd = serialized.sourceOffsetForNodeOffset(lastInline->id, lastInline->literal.size());
            if (itemStart && itemEnd) {
                break;
            }
        }

        if (!itemStart || !itemEnd) {
            return std::nullopt;
        }

        start = start < 0 ? *itemStart : qMin(start, *itemStart);
        end = end < 0 ? *itemEnd : qMax(end, *itemEnd);
    }

    if (start < 0 || end < start) {
        return std::nullopt;
    }
    return SourceSelection{start, end};
}

SourceSelection sourceSelectionForRenderedEdit(const RenderSourceMap& sourceMap, const RenderedEdit& edit)
{
    if (edit.targetKind == RenderedEdit::TargetKind::SourceSpan && edit.sourceSpan.isValid()) {
        return {edit.sourceSpan.start, edit.sourceSpan.end};
    }

    int renderedStart = edit.renderedStart;
    int renderedEnd = edit.renderedEnd;
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }
    if (renderedStart < 0 || renderedEnd < 0) {
        return {-1, -1};
    }

    if (std::optional<SourceSpan> span = sourceMap.editableSourceSpanForRenderedRange(renderedStart, renderedEnd)) {
        return {span->start, span->end};
    }
    if (std::optional<int> offset = sourceMap.editableSourceInsertionPoint(renderedStart)) {
        return {*offset, *offset};
    }
    return {-1, -1};
}

EditTransactionKind transactionKindForPlan(const RenderedEditPlan& plan)
{
    if (plan.kind == RenderedEditPlan::Kind::DemoteListItem) {
        return EditTransactionKind::ListIndent;
    }
    if (plan.kind == RenderedEditPlan::Kind::PromoteListItem) {
        return EditTransactionKind::ListOutdent;
    }
    return EditTransactionKind::RenderedEdit;
}

EditIntent intentForEdit(const RenderedEdit& edit, const RenderedEditPlan& plan)
{
    switch (edit.operation) {
    case RenderedEditOperation::InsertText:
    case RenderedEditOperation::ReplaceSelection:
        return edit.replacement.size() > 1 ? EditIntent::Paste : EditIntent::Typing;
    case RenderedEditOperation::Backspace:
    case RenderedEditOperation::Delete:
        return EditIntent::Delete;
    case RenderedEditOperation::Enter:
        if (plan.kind == RenderedEditPlan::Kind::MergeParagraphs
            || plan.kind == RenderedEditPlan::Kind::MergeTextBlocks
            || plan.kind == RenderedEditPlan::Kind::MergeListItems
            || plan.kind == RenderedEditPlan::Kind::ExitEmptyListItem) {
            return EditIntent::MergeBlock;
        }
        return EditIntent::SplitBlock;
    case RenderedEditOperation::Paste:
        return EditIntent::Paste;
    case RenderedEditOperation::Indent:
        return EditIntent::Indent;
    case RenderedEditOperation::Outdent:
        return EditIntent::Outdent;
    }
    return EditIntent::Unknown;
}

QVector<MarkdownNodeId> normalizedAffectedNodeIds(const RenderedEditPlan& plan)
{
    QVector<MarkdownNodeId> nodeIds = plan.nodeIds;
    if (nodeIds.isEmpty() && plan.primaryNodeId != 0) {
        nodeIds.append(plan.primaryNodeId);
    }
    if (plan.secondaryNodeId != 0 && !nodeIds.contains(plan.secondaryNodeId)) {
        nodeIds.append(plan.secondaryNodeId);
    }
    return nodeIds;
}

QVector<MarkdownNodeId> ancestorIdsForNode(const MarkdownDocument& document, const MarkdownNode& node)
{
    QVector<MarkdownNodeId> ancestorIds;
    MarkdownNodeId currentId = node.parent;
    while (currentId != 0) {
        const MarkdownNode* ancestor = document.nodeById(currentId);
        if (!ancestor) {
            break;
        }
        ancestorIds.append(ancestor->id);
        currentId = ancestor->parent;
    }
    return ancestorIds;
}

void fillSiblingContext(const MarkdownDocument& document, const MarkdownNode& node, NodeSnapshot& snapshot)
{
    const MarkdownNode* parent = document.nodeById(node.parent);
    if (!parent) {
        return;
    }

    const int childIndex = parent->children.indexOf(node.id);
    if (childIndex < 0) {
        return;
    }
    if (childIndex > 0) {
        snapshot.previousSiblingId = parent->children.at(childIndex - 1);
    }
    if (childIndex + 1 < parent->children.size()) {
        snapshot.nextSiblingId = parent->children.at(childIndex + 1);
    }
}

NodeSnapshot snapshotForNode(const MarkdownDocument& document,
                             const MarkdownNode& node,
                             std::optional<MarkdownSerializedNodeSpan> serializedSpan = {})
{
    NodeSnapshot snapshot;
    snapshot.nodeId = node.id;
    snapshot.type = node.type;
    snapshot.source = serializedSpan ? serializedSpan->source : node.source;
    snapshot.content = serializedSpan ? serializedSpan->content : node.content;
    snapshot.parentId = node.parent;
    fillSiblingContext(document, node, snapshot);
    snapshot.ancestorIds = ancestorIdsForNode(document, node);
    snapshot.childIds = node.children;
    snapshot.literal = node.literal;
    return snapshot;
}

QVector<NodeSnapshot> snapshotsForNodes(const MarkdownDocument& document,
                                        const QVector<MarkdownNodeId>& nodeIds,
                                        const std::optional<MarkdownSerializationResult>& serialized = {})
{
    QVector<NodeSnapshot> snapshots;
    for (MarkdownNodeId nodeId : nodeIds) {
        const MarkdownNode* node = document.nodeById(nodeId);
        if (!node) {
            continue;
        }

        std::optional<MarkdownSerializedNodeSpan> serializedSpan;
        if (serialized) {
            const auto it = serialized->nodeSpans.constFind(nodeId);
            if (it != serialized->nodeSpans.constEnd()) {
                serializedSpan = *it;
            }
        }
        snapshots.append(snapshotForNode(document, *node, serializedSpan));
    }
    return snapshots;
}

const NodeSnapshot* snapshotById(const QVector<NodeSnapshot>& snapshots, MarkdownNodeId nodeId)
{
    for (const NodeSnapshot& snapshot : snapshots) {
        if (snapshot.nodeId == nodeId) {
            return &snapshot;
        }
    }
    return nullptr;
}

bool nodeStructureChanged(const NodeSnapshot& before, const NodeSnapshot& after)
{
    return before.parentId != after.parentId
        || before.previousSiblingId != after.previousSiblingId
        || before.nextSiblingId != after.nextSiblingId
        || before.ancestorIds != after.ancestorIds;
}

QVector<EditOperation> operationsForSnapshots(const QVector<MarkdownNodeId>& nodeIds,
                                              const QVector<NodeSnapshot>& beforeNodes,
                                              const QVector<NodeSnapshot>& afterNodes)
{
    QVector<EditOperation> operations;
    for (MarkdownNodeId nodeId : nodeIds) {
        const NodeSnapshot* before = snapshotById(beforeNodes, nodeId);
        const NodeSnapshot* after = snapshotById(afterNodes, nodeId);
        if (before && after) {
            if (before->literal != after->literal) {
                operations.append({EditOperationKind::UpdateLiteral, nodeId, *before, *after});
            }
            if (nodeStructureChanged(*before, *after)) {
                operations.append({EditOperationKind::MoveNode, nodeId, *before, *after});
            }
        } else if (before) {
            operations.append({EditOperationKind::RemoveNode, nodeId, *before, {}});
        } else if (after) {
            operations.append({EditOperationKind::InsertNode, nodeId, {}, *after});
        }
    }
    return operations;
}

MarkdownCommandResult applyLegacyInlineCommand(const QString& markdown,
                                               SourceSelection selection,
                                               MarkdownEditEngine::InlineCommand command)
{
    return command ? command(markdown, selection) : MarkdownCommandResult{false, markdown, selection, QStringLiteral("Missing markdown command.")};
}

MarkdownCommandResult applyLegacyListCommand(const QString& markdown,
                                             SourceSelection selection,
                                             MarkdownCommand::ListType type)
{
    return MarkdownCommand::applyList(markdown, selection, type);
}

MarkdownCommandResult applyLegacyQuoteCommand(const QString& markdown,
                                              SourceSelection selection)
{
    return MarkdownCommand::applyQuote(markdown, selection);
}

template <typename StructuredResult>
MarkdownCommandResult markdownCommandResultFromStructuredEdit(const MarkdownDocument& original,
                                                             SourceSelection selection,
                                                             const StructuredResult& transformed,
                                                             const QString& unresolvedAnchorError,
                                                             EditIntent intent = EditIntent::Unknown)
{
    MarkdownSerializer serializer;
    const MarkdownSerializationResult serialized = serializer.serializeDocumentWithSourceMap(transformed.document);
    const std::optional<int> anchorOffset = serialized.sourceOffsetForNodeOffset(transformed.anchorNodeId,
                                                                                 transformed.anchorOffsetInNode);
    if (!anchorOffset) {
        return {false, original.source(), selection, unresolvedAnchorError};
    }

    const int cursorOffset = qBound(0, *anchorOffset, serialized.markdown.size());
    MarkdownCommandResult result;
    result.changed = serialized.markdown != original.source();
    result.markdown = serialized.markdown;
    result.selection = {cursorOffset, cursorOffset};

    if (result.changed && !transformed.affectedNodeIds.isEmpty()) {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.intent = intent;
        transaction.beforeSelection = selection;
        transaction.afterSelection = result.selection;
        transaction.affectedNodeIds = transformed.affectedNodeIds;
        transaction.beforeNodes = snapshotsForNodes(original, transaction.affectedNodeIds);
        transaction.afterNodes = snapshotsForNodes(transformed.document, transaction.affectedNodeIds, serialized);
        transaction.operations = operationsForSnapshots(transaction.affectedNodeIds, transaction.beforeNodes, transaction.afterNodes);
        transaction.beforeMarkdown = original.source();
        transaction.afterMarkdown = serialized.markdown;
        transaction.label = QStringLiteral("Structured Edit");
        result.transactionValidation = EditTransactionValidator::validate(transaction);
        result.transaction = std::move(transaction);
    }

    return result;
}

PatchResult tryApplyPlannedEdit(const MarkdownDocument& document,
                                const RenderSourceMap& sourceMap,
                                const QVector<MarkdownBlock>& blocks,
                                const RenderedEdit& edit)
{
    const std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(document, sourceMap, blocks, edit);
    if (!plan) {
        return {};
    }
    if (isNoOpPlan(*plan)) {
        return {true, false, document.source(), plan->cursorSourceOffset, {}};
    }
    if (plan->kind == RenderedEditPlan::Kind::ReplaceSourceSpan) {
        if (!plan->sourceSpan.isValid()) {
            return {};
        }

        QString markdown = document.source();
        const int start = qBound(0, plan->sourceSpan.start, markdown.size());
        const int end = qBound(start, plan->sourceSpan.end, markdown.size());
        markdown.replace(start, end - start, plan->literal);
        const int cursorSourceOffset = start + static_cast<int>(plan->literal.size());
        return {true,
                markdown != document.source(),
                markdown,
                cursorSourceOffset,
                {},
                SourceSelection{cursorSourceOffset, cursorSourceOffset}};
    }

    MarkdownSerializer serializer;
    const QString serializedSource = serializer.serializeDocument(document);
    if (serializedSource != document.source()
        && serializedSource != normalizeSerializableMarkdown(document.source())) {
        return {};
    }

    const std::optional<DocumentTransformResult> patched = applyPlan(document, *plan);
    if (!patched) {
        return {};
    }

    const MarkdownSerializationResult serialized = serializer.serializeDocumentWithSourceMap(patched->document);
    const QString markdown = serialized.markdown;
    const std::optional<int> anchorCursor = serialized.sourceOffsetForNodeOffset(patched->anchorNodeId,
                                                                                 patched->anchorOffsetInNode);
    if (!anchorCursor) {
        const MarkdownNode* root = patched->document.nodeById(patched->document.rootId());
        if (markdown.isEmpty() && root && root->children.isEmpty()) {
            return {true, markdown != document.source(), markdown, 0, {}};
        }
        return {};
    }
    const int cursorSourceOffset = qBound(0, *anchorCursor, markdown.size());
    PatchResult result{true, markdown != document.source(), markdown, cursorSourceOffset, {}};
    if (!plan->nodeIds.isEmpty()
        && (plan->kind == RenderedEditPlan::Kind::DemoteListItem || plan->kind == RenderedEditPlan::Kind::PromoteListItem)) {
        result.sourceSelection = sourceSelectionForListItems(serialized, patched->document, plan->nodeIds);
    }

    EditTransaction transaction;
    transaction.kind = transactionKindForPlan(*plan);
    transaction.intent = intentForEdit(edit, *plan);
    transaction.beforeSelection = sourceSelectionForRenderedEdit(sourceMap, edit);
    transaction.afterSelection = result.sourceSelection.value_or(SourceSelection{cursorSourceOffset, cursorSourceOffset});
    transaction.affectedNodeIds = patched->affectedNodeIds.isEmpty()
        ? normalizedAffectedNodeIds(*plan)
        : patched->affectedNodeIds;
    transaction.beforeNodes = snapshotsForNodes(document, transaction.affectedNodeIds);
    transaction.afterNodes = snapshotsForNodes(patched->document, transaction.affectedNodeIds, serialized);
    transaction.operations = operationsForSnapshots(transaction.affectedNodeIds, transaction.beforeNodes, transaction.afterNodes);
    transaction.beforeMarkdown = document.source();
    transaction.afterMarkdown = markdown;
    transaction.label = QStringLiteral("Rendered Edit");
    result.transactionValidation = EditTransactionValidator::validate(transaction);
    result.transaction = std::move(transaction);
    return result;
}

} // namespace

PatchResult MarkdownEditEngine::applyRenderedEdit(const MarkdownDocument& document,
                                                  const RenderSourceMap& sourceMap,
                                                  const QVector<MarkdownBlock>& blocks,
                                                  const RenderedEdit& edit)
{
    PatchResult result = tryApplyPlannedEdit(document, sourceMap, blocks, edit);
    if (result.ok) {
        return result;
    }
    return {false, false, document.source(), -1, QStringLiteral("No structured rendered edit plan available.")};
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommand(const QString& markdown,
                                                             SourceSelection selection,
                                                             InlineCommand command)
{
    return applyLegacyInlineCommand(markdown, selection, command);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineStyleCommand(const MarkdownDocument& document,
                                                                  SourceSelection selection,
                                                                  StyleCommandHandler::InlineStyle style,
                                                                  InlineCommand fallback)
{
    if (std::optional<StyleCommandHandler::StyleCommandResult> transformed =
            StyleCommandHandler::toggleInlineStyleWithSelection(document, selection, style)) {
        return markdownCommandResultFromStructuredEdit(
            document,
            selection,
            *transformed,
            QStringLiteral("Structured inline style edit produced an unresolved cursor anchor."),
            EditIntent::FormatToggle);
    }

    return applyLegacyInlineCommand(document.source(), selection, fallback);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(const MarkdownDocument& document,
                                                                                  const RenderedCommandTarget& target,
                                                                                  StyleCommandHandler::InlineStyle style,
                                                                                  InlineCommand fallback)
{
    return applyInlineStyleCommand(
        document,
        EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), target, true),
        style,
        fallback);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(const MarkdownDocument& document,
                                                                                  const QVector<MarkdownBlock>& blocks,
                                                                                  const RenderedCommandTarget& target,
                                                                                  StyleCommandHandler::InlineStyle style,
                                                                                  InlineCommand fallback)
{
    return applyInlineStyleCommand(
        document,
        EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), blocks, target, true),
        style,
        fallback);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                             const RenderedCommandTarget& target,
                                                                             InlineCommand command)
{
    return applyInlineCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, true), command);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                             const QVector<MarkdownBlock>& blocks,
                                                                             const RenderedCommandTarget& target,
                                                                             InlineCommand command)
{
    return applyInlineCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, true), command);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommand(const QString& markdown,
                                                           SourceSelection selection,
                                                           MarkdownCommand::ListType type)
{
    return applyLegacyListCommand(markdown, selection, type);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommand(const MarkdownDocument& document,
                                                           SourceSelection selection,
                                                           MarkdownCommand::ListType type)
{
    const std::optional<ListCommandHandler::ListCommandResult> transformed =
        ListCommandHandler::applyList(document, selection, type);
    if (!transformed) {
        return applyLegacyListCommand(document.source(), selection, type);
    }

    return markdownCommandResultFromStructuredEdit(
        document,
        selection,
        *transformed,
        QStringLiteral("Structured list command produced an unresolved cursor anchor."),
        EditIntent::ListToggle);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommandToRenderedTarget(const QString& markdown,
                                                                           const RenderedCommandTarget& target,
                                                                           MarkdownCommand::ListType type)
{
    return applyListCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, false), type);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommandToRenderedTarget(const QString& markdown,
                                                                           const QVector<MarkdownBlock>& blocks,
                                                                           const RenderedCommandTarget& target,
                                                                           MarkdownCommand::ListType type)
{
    return applyListCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, false), type);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommandToRenderedTarget(const MarkdownDocument& document,
                                                                           const QVector<MarkdownBlock>& blocks,
                                                                           const RenderedCommandTarget& target,
                                                                           MarkdownCommand::ListType type)
{
    return applyListCommand(document,
                            EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), blocks, target, false),
                            type);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommand(const QString& markdown,
                                                              SourceSelection selection,
                                                              int level)
{
    return MarkdownCommand::applyHeading(markdown, selection, level);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommand(const MarkdownDocument& document,
                                                              SourceSelection selection,
                                                              int level)
{
    const std::optional<HeadingCommandHandler::HeadingCommandResult> transformed =
        HeadingCommandHandler::applyHeading(document, selection, level);
    if (!transformed) {
        return MarkdownCommand::applyHeading(document.source(), selection, level);
    }

    return markdownCommandResultFromStructuredEdit(
        document,
        selection,
        *transformed,
        QStringLiteral("Structured heading command produced an unresolved cursor anchor."),
        EditIntent::HeadingChange);
}

MarkdownCommandResult MarkdownEditEngine::applyParagraphCommand(const MarkdownDocument& document,
                                                                SourceSelection selection)
{
    const std::optional<HeadingCommandHandler::HeadingCommandResult> transformed =
        HeadingCommandHandler::applyParagraph(document, selection);
    if (!transformed) {
        return MarkdownCommand::applyParagraph(document.source(), selection);
    }

    return markdownCommandResultFromStructuredEdit(
        document,
        selection,
        *transformed,
        QStringLiteral("Structured paragraph command produced an unresolved cursor anchor."),
        EditIntent::HeadingChange);
}

MarkdownCommandResult MarkdownEditEngine::applyParagraphCommandToRenderedTarget(const MarkdownDocument& document,
                                                                                const QVector<MarkdownBlock>& blocks,
                                                                                const RenderedCommandTarget& target)
{
    return applyParagraphCommand(document,
                                 EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), blocks, target, false));
}

MarkdownCommandResult MarkdownEditEngine::applyQuoteCommand(const MarkdownDocument& document,
                                                            SourceSelection selection)
{
    const std::optional<QuoteCommandHandler::QuoteCommandResult> transformed =
        QuoteCommandHandler::applyQuote(document, selection);
    if (!transformed) {
        return applyLegacyQuoteCommand(document.source(), selection);
    }

    return markdownCommandResultFromStructuredEdit(
        document,
        selection,
        *transformed,
        QStringLiteral("Structured quote command produced an unresolved cursor anchor."),
        EditIntent::QuoteToggle);
}

MarkdownCommandResult MarkdownEditEngine::applyQuoteCommandToRenderedTarget(const MarkdownDocument& document,
                                                                            const QVector<MarkdownBlock>& blocks,
                                                                            const RenderedCommandTarget& target)
{
    return applyQuoteCommand(document,
                             EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), blocks, target, false));
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                              const RenderedCommandTarget& target,
                                                                              int level)
{
    return applyHeadingCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, false), level);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                              const QVector<MarkdownBlock>& blocks,
                                                                              const RenderedCommandTarget& target,
                                                                              int level)
{
    return applyHeadingCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, false), level);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommandToRenderedTarget(const MarkdownDocument& document,
                                                                              const QVector<MarkdownBlock>& blocks,
                                                                              const RenderedCommandTarget& target,
                                                                              int level)
{
    return applyHeadingCommand(document,
                               EditorSelectionMapper::sourceSelectionForRenderedTarget(document.source(), blocks, target, false),
                               level);
}

} // namespace Muffin
