#include "Document.h"
#include "editor/RenderInvalidation.h"
#include "editor/StructuralUndoController.h"
#include "model/MarkdownMathSpanBuilder.h"
#include "model/MarkdownSerializer.h"
#include "model/MarkdownSourceSpanUpdater.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/MarkerFragmentIndex.h"
#include "renderer/SyntaxTokenIndex.h"
#include "renderer/TextDocumentPatchConsistency.h"
#include "theme/ThemeStylesheet.h"

#include <QDateTime>
#include <QUndoCommand>
#include <utility>

namespace Muffin {

namespace Internal {

class MarkdownEditCommand : public QUndoCommand {
public:
    MarkdownEditCommand(Document* document, QString before, QString after,
                        SourceSelection beforeSelection, SourceSelection afterSelection,
                        std::optional<EditTransaction> transaction,
                        std::optional<EditTransactionValidationResult> validation,
                        QString label)
        : QUndoCommand(std::move(label))
        , m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_beforeSelection(beforeSelection)
        , m_afterSelection(afterSelection)
        , m_transaction(std::move(transaction))
        , m_validation(std::move(validation))
        , m_intent(m_transaction ? m_transaction->intent : EditIntent::Unknown)
    {
    }

    void undo() override
    {
        if (tryApplyStructuralUndo()) {
            return;
        }
        m_document->setMarkdownInternal(m_before, m_beforeSelection);
    }
    void redo() override
    {
        if (tryApplyStructuralRedo()) {
            return;
        }
        m_document->setMarkdownInternal(m_after, m_afterSelection);
    }
    const std::optional<EditTransaction>& transaction() const { return m_transaction; }
    const std::optional<EditTransactionValidationResult>& validation() const { return m_validation; }
    bool canApplyStructurally() const
    {
        return m_transaction && StructuralUndoController::canApplyStructurally(*m_transaction, m_validation);
    }
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* command) override
    {
        const auto* other = dynamic_cast<const MarkdownEditCommand*>(command);
        if (!other || m_after != other->m_before) {
            return false;
        }

        if (m_intent == EditIntent::Typing || m_intent == EditIntent::Delete) {
            if (other->m_intent != m_intent) {
                return false;
            }
            if (m_lastEditTime.msecsTo(other->m_lastEditTime) > 1000
                || !isMergeableTextEdit(m_before, m_after, other->m_after)) {
                return false;
            }
        } else if (m_intent == EditIntent::Unknown) {
            if (text() != other->text()) {
                return false;
            }
            if (m_lastEditTime.msecsTo(other->m_lastEditTime) > 1000
                || !isMergeableTextEdit(m_before, m_after, other->m_after)) {
                return false;
            }
        } else {
            return false;
        }

        m_after = other->m_after;
        m_afterSelection = other->m_afterSelection;
        if (m_transaction) {
            m_transaction->afterMarkdown = m_after;
            m_transaction->afterSelection = m_afterSelection;
        }
        m_lastEditTime = other->m_lastEditTime;
        return true;
    }

private:
    bool tryApplyStructuralUndo() const
    {
        if (!m_transaction) {
            m_document->m_lastStructuralUndoDryRunResult.reset();
            m_document->m_lastStructuralUndoApplied = false;
            return false;
        }
        m_document->m_lastStructuralUndoApplied = false;
        const StructuralUndoDecision decision = StructuralUndoController::evaluate(m_document->markdownDocument(),
                                                                                  *m_transaction,
                                                                                  m_validation,
                                                                                  EditTransactionApplyDirection::Reverse,
                                                                                  m_before);
        m_document->m_lastStructuralUndoDryRunResult = decision.dryRunResult;
        if (!decision.shouldApply || !decision.dryRunResult) {
            return false;
        }
        const EditTransactionApplyResult& result = *decision.dryRunResult;
        m_document->setMarkdownDocumentInternal(result.document, result.markdown, m_beforeSelection);
        m_document->m_lastStructuralUndoApplied = true;
        return true;
    }

    bool tryApplyStructuralRedo() const
    {
        if (!m_transaction) {
            m_document->m_lastStructuralRedoDryRunResult.reset();
            m_document->m_lastStructuralRedoApplied = false;
            return false;
        }
        m_document->m_lastStructuralRedoApplied = false;
        const StructuralUndoDecision decision = StructuralUndoController::evaluate(m_document->markdownDocument(),
                                                                                  *m_transaction,
                                                                                  m_validation,
                                                                                  EditTransactionApplyDirection::Forward,
                                                                                  m_after);
        m_document->m_lastStructuralRedoDryRunResult = decision.dryRunResult;
        if (!decision.shouldApply || !decision.dryRunResult) {
            return false;
        }
        const EditTransactionApplyResult& result = *decision.dryRunResult;
        m_document->setMarkdownDocumentInternal(result.document, result.markdown, m_afterSelection);
        m_document->m_lastStructuralRedoApplied = true;
        return true;
    }

    static bool isMergeableTextEdit(const QString& before, const QString& middle, const QString& after)
    {
        const int firstDelta = qAbs(middle.size() - before.size());
        const int secondDelta = qAbs(after.size() - middle.size());
        return firstDelta <= 1 && secondDelta <= 1;
    }

    Document* m_document;
    QString m_before;
    QString m_after;
    SourceSelection m_beforeSelection;
    SourceSelection m_afterSelection;
    std::optional<EditTransaction> m_transaction;
    std::optional<EditTransactionValidationResult> m_validation;
    EditIntent m_intent = EditIntent::Unknown;
    QDateTime m_lastEditTime = QDateTime::currentDateTimeUtc();
};

} // namespace Internal

namespace {

SourceSpan changedSourceSpan(const QString& before, const QString& after)
{
    int prefixLength = 0;
    const int commonLimit = qMin(before.size(), after.size());
    while (prefixLength < commonLimit && before.at(prefixLength) == after.at(prefixLength)) {
        ++prefixLength;
    }

    int suffixLength = 0;
    while (suffixLength < before.size() - prefixLength
           && suffixLength < after.size() - prefixLength
           && before.at(before.size() - 1 - suffixLength) == after.at(after.size() - 1 - suffixLength)) {
        ++suffixLength;
    }

    return {prefixLength, static_cast<int>(before.size() - suffixLength)};
}

QString changedReplacementText(const QString& after, SourceSpan beforeSpan, int beforeSize)
{
    const int removedLength = beforeSpan.end - beforeSpan.start;
    const int insertedLength = after.size() - beforeSize + removedLength;
    if (insertedLength < 0) {
        return {};
    }
    return after.mid(beforeSpan.start, insertedLength);
}

StructuralEditStatus statusForDryRunResult(const std::optional<EditTransactionApplyResult>& result)
{
    if (!result) {
        return StructuralEditStatus::NotApplicable;
    }
    return result->ok ? StructuralEditStatus::Ready : StructuralEditStatus::DryRunFailed;
}

QStringList errorsForDryRunResult(const std::optional<EditTransactionApplyResult>& result)
{
    return result ? result->errors : QStringList{};
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString validationModeName(IncrementalRenderValidationMode mode)
{
    switch (mode) {
    case IncrementalRenderValidationMode::Auto:
        return QStringLiteral("Auto");
    case IncrementalRenderValidationMode::AlwaysFullCompare:
        return QStringLiteral("AlwaysFullCompare");
    case IncrementalRenderValidationMode::TrustLocalChecks:
        return QStringLiteral("TrustLocalChecks");
    }
    return QStringLiteral("Unknown");
}

QString renderKindName(RenderSpan::Kind kind)
{
    switch (kind) {
    case RenderSpan::Kind::Paragraph:
        return QStringLiteral("Paragraph");
    case RenderSpan::Kind::Heading:
        return QStringLiteral("Heading");
    case RenderSpan::Kind::List:
        return QStringLiteral("List");
    case RenderSpan::Kind::BlockQuote:
        return QStringLiteral("BlockQuote");
    case RenderSpan::Kind::CodeBlock:
        return QStringLiteral("CodeBlock");
    case RenderSpan::Kind::Table:
        return QStringLiteral("Table");
    case RenderSpan::Kind::HtmlBlock:
        return QStringLiteral("HtmlBlock");
    case RenderSpan::Kind::ThematicBreak:
        return QStringLiteral("ThematicBreak");
    case RenderSpan::Kind::FormulaInline:
        return QStringLiteral("FormulaInline");
    case RenderSpan::Kind::FormulaBlock:
        return QStringLiteral("FormulaBlock");
    case RenderSpan::Kind::Text:
        return QStringLiteral("Text");
    case RenderSpan::Kind::SoftBreak:
        return QStringLiteral("SoftBreak");
    case RenderSpan::Kind::LineBreak:
        return QStringLiteral("LineBreak");
    case RenderSpan::Kind::Emphasis:
        return QStringLiteral("Emphasis");
    case RenderSpan::Kind::Strong:
        return QStringLiteral("Strong");
    case RenderSpan::Kind::InlineCode:
        return QStringLiteral("InlineCode");
    case RenderSpan::Kind::Link:
        return QStringLiteral("Link");
    case RenderSpan::Kind::Image:
        return QStringLiteral("Image");
    case RenderSpan::Kind::MarkdownSyntax:
        return QStringLiteral("MarkdownSyntax");
    case RenderSpan::Kind::Unsupported:
        return QStringLiteral("Unsupported");
    }
    return QStringLiteral("Unknown");
}

QString structuralStatusName(StructuralEditStatus status)
{
    switch (status) {
    case StructuralEditStatus::NotApplicable:
        return QStringLiteral("NotApplicable");
    case StructuralEditStatus::Ready:
        return QStringLiteral("Ready");
    case StructuralEditStatus::DryRunFailed:
        return QStringLiteral("DryRunFailed");
    }
    return QStringLiteral("Unknown");
}

QString nodeIdsText(const QVector<MarkdownNodeId>& ids)
{
    QStringList parts;
    parts.reserve(ids.size());
    for (MarkdownNodeId id : ids) {
        parts.append(QString::number(id));
    }
    return parts.join(QChar(','));
}

bool sourceRangeStartsInside(const SourceRange& outer, const SourceRange& inner)
{
    if (outer.startLine <= 0 || inner.startLine <= 0) {
        return false;
    }
    if (inner.startLine < outer.startLine || inner.startLine > outer.endLine) {
        return false;
    }
    if (inner.startLine == outer.startLine && inner.startColumn < outer.startColumn) {
        return false;
    }
    if (inner.startLine == outer.endLine && inner.startColumn > outer.endColumn) {
        return false;
    }
    return true;
}

RenderSpan::Kind replacementKindForNode(const MarkdownNode& node)
{
    switch (node.type) {
    case MarkdownNodeType::BlockQuote:
        return RenderSpan::Kind::BlockQuote;
    case MarkdownNodeType::ListItem:
        return RenderSpan::Kind::List;
    case MarkdownNodeType::Paragraph:
        return RenderSpan::Kind::Paragraph;
    case MarkdownNodeType::Heading:
        return RenderSpan::Kind::Heading;
    case MarkdownNodeType::CodeBlock:
        return RenderSpan::Kind::CodeBlock;
    default:
        return RenderSpan::Kind::Unsupported;
    }
}

std::optional<PartialReplacementRange> aggregatedReplacementRangeForNode(const QVector<MarkdownBlock>& blocks,
                                                                         const MarkdownNode& node)
{
    int firstBlockIndex = -1;
    int lastBlockIndex = -1;
    for (int i = 0; i < blocks.size(); ++i) {
        if (!sourceRangeStartsInside(node.sourceRange, blocks.at(i).sourceRange)) {
            continue;
        }
        if (firstBlockIndex < 0) {
            firstBlockIndex = i;
        }
        lastBlockIndex = i;
    }
    if (firstBlockIndex < 0 || lastBlockIndex < firstBlockIndex) {
        return {};
    }

    const MarkdownBlock& firstBlock = blocks.at(firstBlockIndex);
    const MarkdownBlock& lastBlock = blocks.at(lastBlockIndex);
    PartialReplacementRange range;
    range.nodeId = node.id;
    range.renderedStart = firstBlock.effectiveReplacementRenderedStart();
    range.renderedEnd = lastBlock.effectiveReplacementRenderedEnd();
    range.contentRenderedStart = firstBlock.renderedStart;
    range.contentRenderedEnd = lastBlock.renderedEnd;
    range.source = node.source;
    range.sourceRange = node.sourceRange;
    range.kind = replacementKindForNode(node);
    return range;
}

QVector<PartialReplacementRange> replacementRangesForInvalidatedNodes(const QVector<MarkdownBlock>& blocks,
                                                                      const MarkdownDocument& document,
                                                                      const QVector<MarkdownNodeId>& nodeIds)
{
    QVector<PartialReplacementRange> ranges;
    for (MarkdownNodeId nodeId : nodeIds) {
        bool found = false;
        for (const MarkdownBlock& block : blocks) {
            if (block.nodeId != nodeId) {
                continue;
            }
            ranges.append({block.nodeId,
                           block.effectiveReplacementRenderedStart(),
                           block.effectiveReplacementRenderedEnd(),
                           block.renderedStart,
                           block.renderedEnd,
                           block.source,
                           block.sourceRange,
                           block.kind});
            found = true;
            break;
        }
        if (found) {
            continue;
        }
        const MarkdownNode* node = document.nodeById(nodeId);
        if (!node) {
            continue;
        }
        if (std::optional<PartialReplacementRange> aggregate = aggregatedReplacementRangeForNode(blocks, *node)) {
            ranges.append(*aggregate);
        }
    }
    return ranges;
}

} // namespace

Document::Document(QObject* parent)
    : QObject(parent)
    , m_textDocument(std::make_unique<QTextDocument>())
{
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, &Document::onRenderTimerElapsed);
}

bool Document::isModified() const {
    return m_textDocument && m_textDocument->isModified();
}

void Document::setMarkdown(const QString& markdown) {
    m_undoStack.clear();
    m_lastInvalidatedNodeIds.clear();
    m_lastBlockParseRange = {};
    m_lastBlockReparseShadowResult = {};
    resetPartialRenderObservations();
    m_lastStructuralUndoDryRunResult.reset();
    m_lastStructuralRedoDryRunResult.reset();
    m_lastStructuralUndoApplied = false;
    m_lastStructuralRedoApplied = false;
    setMarkdownInternal(markdown, {}, false, true);
    m_undoStack.setClean();
}

void Document::applyMarkdownEdit(const QString& markdown, int cursorSourceOffset, const QString& label)
{
    applyMarkdownEdit(markdown, SourceSelection{cursorSourceOffset, cursorSourceOffset}, label);
}

void Document::applyMarkdownEdit(const QString& markdown, SourceSelection selection, const QString& label)
{
    if (m_markdown == markdown) {
        return;
    }
    m_lastInvalidatedNodeIds.clear();
    m_lastBlockParseRange = {};
    m_lastBlockReparseShadowResult = {};
    resetPartialRenderObservations();
    m_lastStructuralUndoApplied = false;
    m_lastStructuralRedoApplied = false;
    m_undoStack.push(new Internal::MarkdownEditCommand(this, m_markdown, markdown, {-1, -1}, selection, {}, {}, label));
}

void Document::applyMarkdownEdit(EditTransaction transaction, const QString& label)
{
    applyMarkdownEdit(std::move(transaction), {}, label);
}

void Document::applyMarkdownEdit(EditTransaction transaction,
                                 std::optional<EditTransactionValidationResult> validation,
                                 const QString& label)
{
    if (m_markdown == transaction.afterMarkdown) {
        return;
    }

    if (transaction.beforeMarkdown.isEmpty()) {
        transaction.beforeMarkdown = m_markdown;
    }
    if (!transaction.afterSelection) {
        transaction.afterSelection = SourceSelection{-1, -1};
    }
    if (transaction.label.isEmpty()) {
        transaction.label = label;
    }

    m_lastInvalidatedNodeIds = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

    const QString before = transaction.beforeMarkdown;
    const QString after = transaction.afterMarkdown;
    const SourceSelection beforeSelection = transaction.beforeSelection;
    const SourceSelection afterSelection = *transaction.afterSelection;
    m_undoStack.push(new Internal::MarkdownEditCommand(this, before, after, beforeSelection, afterSelection,
                                                       std::move(transaction), std::move(validation), label));
}

std::optional<EditTransaction> Document::editTransactionAt(int undoIndex) const
{
    const auto* command = m_undoStack.command(undoIndex);
    const auto* editCommand = dynamic_cast<const Internal::MarkdownEditCommand*>(command);
    if (!editCommand) {
        return {};
    }
    return editCommand->transaction();
}

std::optional<EditTransactionValidationResult> Document::editTransactionValidationAt(int undoIndex) const
{
    const auto* command = m_undoStack.command(undoIndex);
    const auto* editCommand = dynamic_cast<const Internal::MarkdownEditCommand*>(command);
    if (!editCommand) {
        return {};
    }
    return editCommand->validation();
}

bool Document::canApplyEditStructurallyAt(int undoIndex) const
{
    const auto* command = m_undoStack.command(undoIndex);
    const auto* editCommand = dynamic_cast<const Internal::MarkdownEditCommand*>(command);
    return editCommand && editCommand->canApplyStructurally();
}

StructuralEditObservation Document::lastStructuralEditObservation() const
{
    StructuralEditObservation observation;
    observation.undoStatus = statusForDryRunResult(m_lastStructuralUndoDryRunResult);
    observation.redoStatus = statusForDryRunResult(m_lastStructuralRedoDryRunResult);
    observation.undoErrors = errorsForDryRunResult(m_lastStructuralUndoDryRunResult);
    observation.redoErrors = errorsForDryRunResult(m_lastStructuralRedoDryRunResult);
    return observation;
}

BlockLocalReparseObservation Document::lastBlockLocalReparseObservation() const
{
    BlockLocalReparseObservation observation;
    observation.planned = m_lastBlockParseRange.canReparseLocally();
    observation.shadowAttempted = m_lastBlockReparseShadowResult.attempted;
    observation.shadowOk = m_lastBlockReparseShadowResult.ok;
    observation.applied = m_lastBlockReparseShadowResult.appliedToDocument;
    observation.editedSource = m_lastBlockParseRange.editedSource;
    observation.expandedSource = m_lastBlockParseRange.expandedSource;
    observation.affectedBlockIds = m_lastBlockParseRange.affectedBlockIds;
    observation.errors = m_lastBlockReparseShadowResult.errors;

    if (!m_lastBlockParseRange.canReparseLocally()) {
        observation.fallbackReason = m_lastBlockParseRange.reason;
    } else if (m_lastBlockReparseShadowResult.attempted && !m_lastBlockReparseShadowResult.ok) {
        if (!m_lastBlockReparseShadowResult.reparse.ok) {
            observation.fallbackReason = QStringLiteral("Shadow reparse failed");
        } else if (!m_lastBlockReparseShadowResult.merge.ok) {
            observation.fallbackReason = QStringLiteral("Shadow merge failed");
        } else if (!m_lastBlockReparseShadowResult.consistency.ok) {
            observation.fallbackReason = QStringLiteral("Shadow consistency failed");
        } else {
            observation.fallbackReason = QStringLiteral("Shadow local reparse failed");
        }
    }

    return observation;
}

IncrementalRenderObservation Document::lastIncrementalRenderObservation() const
{
    IncrementalRenderObservation observation;
    observation.attempted = m_lastPartialPatchSimulationResult.attempted;
    observation.ok = m_lastPartialPatchSimulationResult.ok;
    observation.appliedToDocument = m_lastPartialPatchSimulationResult.appliedToDocument;
    observation.validationMode = m_lastIncrementalRenderValidationMode;
    observation.validationReason = m_lastIncrementalRenderValidationReason;
    observation.applyDecisionReason = m_lastPartialPatchApplyDecisionReason;
    observation.invalidatedNodeIds = m_lastRenderUpdateBatch.nodeIds;
    if (!m_lastPartialPatchPlan.steps.isEmpty()) {
        observation.patchKind = m_lastPartialPatchPlan.steps.first().kind;
    }
    observation.errors = m_lastPartialPatchSimulationResult.errors;
    observation.errors.append(m_lastPartialPatchPlan.errors);
    observation.errors.append(m_lastPartialPatchDryRun.errors);
    observation.errors.append(m_lastPartialRenderStateMergeResult.errors);
    return observation;
}

QString Document::lastRenderDiagnosticsText() const
{
    const BlockLocalReparseObservation block = lastBlockLocalReparseObservation();
    const IncrementalRenderObservation incremental = lastIncrementalRenderObservation();
    const StructuralEditObservation structural = lastStructuralEditObservation();

    QStringList lines;
    lines.append(QStringLiteral("[parse]"));
    lines.append(QStringLiteral("parseMode=%1").arg(m_parseMode == ParseMode::LocalReparse
        ? QStringLiteral("LocalReparse") : QStringLiteral("FullParse")));
    lines.append(QString());

    lines.append(QStringLiteral("[block-reparse]"));
    lines.append(QStringLiteral("planned=%1").arg(boolText(block.planned)));
    lines.append(QStringLiteral("shadowAttempted=%1").arg(boolText(block.shadowAttempted)));
    lines.append(QStringLiteral("shadowOk=%1").arg(boolText(block.shadowOk)));
    lines.append(QStringLiteral("applied=%1").arg(boolText(block.applied)));
    lines.append(QStringLiteral("fallbackReason=%1").arg(block.fallbackReason));
    lines.append(QStringLiteral("affectedBlockIds=%1").arg(nodeIdsText(block.affectedBlockIds)));
    lines.append(QStringLiteral("errors=%1").arg(block.errors.join(QStringLiteral("; "))));
    lines.append(QString());

    lines.append(QStringLiteral("[incremental-render]"));
    lines.append(QStringLiteral("attempted=%1").arg(boolText(incremental.attempted)));
    lines.append(QStringLiteral("ok=%1").arg(boolText(incremental.ok)));
    lines.append(QStringLiteral("appliedToDocument=%1").arg(boolText(incremental.appliedToDocument)));
    lines.append(QStringLiteral("validationMode=%1").arg(validationModeName(incremental.validationMode)));
    lines.append(QStringLiteral("validationReason=%1").arg(incremental.validationReason));
    lines.append(QStringLiteral("applyDecisionReason=%1").arg(incremental.applyDecisionReason));
    lines.append(QStringLiteral("patchKind=%1").arg(renderKindName(incremental.patchKind)));
    lines.append(QStringLiteral("invalidatedNodeIds=%1").arg(nodeIdsText(incremental.invalidatedNodeIds)));
    lines.append(QStringLiteral("errors=%1").arg(incremental.errors.join(QStringLiteral("; "))));
    lines.append(QString());

    lines.append(QStringLiteral("[structural-edit]"));
    lines.append(QStringLiteral("undoStatus=%1").arg(structuralStatusName(structural.undoStatus)));
    lines.append(QStringLiteral("redoStatus=%1").arg(structuralStatusName(structural.redoStatus)));
    lines.append(QStringLiteral("undoErrors=%1").arg(structural.undoErrors.join(QStringLiteral("; "))));
    lines.append(QStringLiteral("redoErrors=%1").arg(structural.redoErrors.join(QStringLiteral("; "))));
    return lines.join(QChar('\n'));
}

QString Document::lastRenderDiagnosticsSummary() const
{
    const BlockLocalReparseObservation block = lastBlockLocalReparseObservation();
    const IncrementalRenderObservation incremental = lastIncrementalRenderObservation();
    const QString parseLabel = m_parseMode == ParseMode::LocalReparse
        ? QStringLiteral("LP") : QStringLiteral("FP");

    if (!block.fallbackReason.isEmpty()) {
        return QStringLiteral("%1 | BR: %2").arg(parseLabel, block.fallbackReason);
    }
    if (!incremental.attempted) {
        return QStringLiteral("%1 | IR: none").arg(parseLabel);
    }

    const QString kind = renderKindName(incremental.patchKind);
    if (!incremental.applyDecisionReason.isEmpty()) {
        return QStringLiteral("%1 | IR: %2 %3").arg(parseLabel, kind, incremental.applyDecisionReason);
    }
    if (!incremental.validationReason.isEmpty()) {
        return QStringLiteral("%1 | IR: %2 %3").arg(parseLabel, kind, incremental.validationReason);
    }
    return QStringLiteral("%1 | IR: %2 %3").arg(parseLabel, kind, validationModeName(incremental.validationMode));
}

MarkerVisibilityState Document::markerVisibilityStateForCaret(const SelectionBookmark& caret) const
{
    MarkerVisibilityState state = MarkerVisibilityController::stateForCaret(caret, SyntaxTokenIndex(m_syntaxTokens));
    MarkerFragmentIndex fragmentIndex(m_fragments);
    for (const SyntaxMarkerPair& pair : state.visiblePairs) {
        state.visibleFragments.append(fragmentIndex.markerFragmentsForPair(pair));
    }
    return state;
}

MarkerVisibilityState Document::markerVisibilityStateForSelection(const SelectionRangeBookmark& selection) const
{
    MarkerVisibilityState state = MarkerVisibilityController::stateForSelection(selection, SyntaxTokenIndex(m_syntaxTokens));
    MarkerFragmentIndex fragmentIndex(m_fragments);
    for (const SyntaxMarkerPair& pair : state.visiblePairs) {
        state.visibleFragments.append(fragmentIndex.markerFragmentsForPair(pair));
    }
    return state;
}

MarkerVisibilityState Document::markerVisibilityStateForPinnedMarker(PinnedMarker marker) const
{
    MarkerVisibilityState state = MarkerVisibilityController::stateForPinnedMarker(marker, SyntaxTokenIndex(m_syntaxTokens));
    MarkerFragmentIndex fragmentIndex(m_fragments);
    for (const SyntaxMarkerPair& pair : state.visiblePairs) {
        state.visibleFragments.append(fragmentIndex.markerFragmentsForPair(pair));
    }
    return state;
}

void Document::setMarkdownInternal(const QString& markdown, SourceSelection selection, bool planBlockReparse, bool immediateRender)
{
    if (m_markdown == markdown) return;

    const QString previousMarkdown = m_markdown;
    const MarkdownDocument previousDocument = m_markdownDocument;
    const bool hasExplicitInvalidation = !m_lastInvalidatedNodeIds.isEmpty();
    m_lastBlockParseRange = {};
    m_lastBlockReparseShadowResult = {};
    m_parseMode = ParseMode::FullParse;
    if (planBlockReparse && !hasExplicitInvalidation && !m_markdownDocument.isEmpty()) {
        const SourceSpan changedSpan = changedSourceSpan(previousMarkdown, markdown);
        const QString replacement = changedReplacementText(markdown, changedSpan, previousMarkdown.size());
        m_lastBlockParseRange = BlockReparsePlanner::planForEdit(m_markdownDocument, changedSpan, replacement);
        if (m_lastBlockParseRange.canReparseLocally()) {
            m_lastInvalidatedNodeIds = m_lastBlockParseRange.affectedBlockIds;
        }
    }

    m_markdown = markdown;

    bool usedLocalReparse = false;
    if (m_lastBlockParseRange.canReparseLocally() && m_blockLocalReparseEnabled) {
        usedLocalReparse = tryLocalBlockReparse(previousDocument);
        if (usedLocalReparse) {
            m_lastParsedAstTree = AstTree{};
            m_markdownDocument = m_lastBlockReparseShadowResult.merge.document;
            MarkdownSerializationResult serialization;
            serialization.markdown = m_markdown;
            m_mathSpans = MarkdownMathSpanBuilder::build(m_markdownDocument, serialization);
            m_lastBlockReparseShadowResult.appliedToDocument = true;
            m_parseMode = ParseMode::LocalReparse;
        }
    }

    if (!usedLocalReparse) {
        CmarkParser parser;
        ParseResult parseResult = m_markdownDocument.isEmpty()
            ? parser.parseDocument(m_markdown)
            : parser.parseDocument(m_markdown, m_markdownDocument);
        m_lastParsedAstTree = std::move(parseResult.ast);
        m_markdownDocument = std::move(parseResult.document);
        m_mathSpans = std::move(parseResult.mathSpans);

        if (m_lastBlockParseRange.canReparseLocally()) {
            runBlockReparseShadowMerge(previousDocument);
            if (m_blockLocalReparseEnabled && m_lastBlockReparseShadowResult.ok) {
                m_lastParsedAstTree = AstTree{};
                m_markdownDocument = m_lastBlockReparseShadowResult.merge.document;
                m_lastBlockReparseShadowResult.appliedToDocument = true;
                m_parseMode = ParseMode::LocalReparse;
            }
        }
    }
    if (immediateRender) {
        render();
        emitDeferredSignals(selection);
    } else {
        scheduleRender(selection);
    }
}

void Document::setMarkdownDocumentInternal(MarkdownDocument document, const QString& markdown, SourceSelection selection)
{
    if (m_markdown == markdown) return;

    m_lastBlockParseRange = {};
    m_parseMode = ParseMode::FullParse;
    m_markdown = markdown;
    m_lastParsedAstTree = AstTree{};
    m_lastBlockReparseShadowResult = {};
    m_markdownDocument = std::move(document);
    MarkdownSerializer serializer;
    const MarkdownSerializationResult serialization = serializer.serializeDocumentWithSourceMap(m_markdownDocument);
    m_markdownDocument = MarkdownSourceSpanUpdater::applySerializedSourceSpans(std::move(m_markdownDocument), serialization);
    m_mathSpans = MarkdownMathSpanBuilder::build(m_markdownDocument, serialization);
    render();

    emit markdownChanged();
    if (selection.start >= 0 && selection.end >= 0) {
        if (selection.start > selection.end) {
            std::swap(selection.start, selection.end);
        }
        emit sourceSelectionRequested(selection);
        if (selection.start == selection.end) {
            emit cursorSourceOffsetRequested(selection.start);
        }
    }
}

void Document::setTheme(const Theme& theme) {
    m_theme = theme;
    render();
}

void Document::render() {
    ThemeStylesheet ss(m_theme);
    DocumentRenderer renderer(ss);
    const std::unique_ptr<QTextDocument> previousTextDocument(m_textDocument ? m_textDocument->clone() : nullptr);
    const RenderSourceMap previousSourceMap = m_sourceMap;
    const QVector<MarkdownBlock> previousBlocks = m_blocks;
    const QVector<SyntaxTokenSpan> previousSyntaxTokens = m_syntaxTokens;
    const QVector<RenderFragment> previousFragments = m_fragments;
    enqueueRenderUpdateForInvalidatedNodes();
    m_lastRenderUpdateBatch = m_renderUpdateQueue.drain();
    const bool hasRenderUpdates = !m_lastRenderUpdateBatch.nodeIds.isEmpty();

    std::optional<RenderResult> fullResult;
    if (!hasRenderUpdates
        || m_incrementalRenderValidationMode == IncrementalRenderValidationMode::AlwaysFullCompare) {
        fullResult = renderFullResult(renderer);
    }
    if (previousTextDocument && hasRenderUpdates
        && tryRenderIncremental(renderer,
                                *previousTextDocument,
                                previousSourceMap,
                                previousBlocks,
                                previousSyntaxTokens,
                                previousFragments,
                                m_lastRenderUpdateBatch,
                                fullResult ? &*fullResult : nullptr)) {
        m_textDocument->setModified(false);
        emit documentRendered();
        return;
    }

    if (!hasRenderUpdates) {
        resetPartialRenderObservations();
    }
    if (!fullResult) {
        fullResult = renderFullResult(renderer);
    }
    applyRenderResult(std::move(*fullResult));
    m_textDocument->setModified(false);

    emit documentRendered();
}

RenderResult Document::renderFullResult(DocumentRenderer& renderer)
{
    return renderer.render(m_markdownDocument, m_mathSpans);
}

bool Document::tryRenderIncremental(DocumentRenderer& renderer,
                                    const QTextDocument& previousTextDocument,
                                    const RenderSourceMap& previousSourceMap,
                                    const QVector<MarkdownBlock>& previousBlocks,
                                    const QVector<SyntaxTokenSpan>& previousSyntaxTokens,
                                    const QVector<RenderFragment>& previousFragments,
                                    const RenderUpdateBatch& updateBatch,
                                    const RenderResult* fullResult)
{
    m_lastPartialRenderResult = renderer.renderPartial(m_markdownDocument, updateBatch.nodeIds, m_mathSpans);
    m_lastPartialRenderResult.replacementRanges = replacementRangesForInvalidatedNodes(previousBlocks,
                                                                                       m_markdownDocument,
                                                                                       updateBatch.nodeIds);
    m_lastPartialPatchPlan = PartialDocumentPatchPlanner::plan(m_lastPartialRenderResult);
    m_lastPartialPatchDryRun = PartialDocumentPatchApplier::dryRun(m_lastPartialPatchPlan,
                                                                   previousTextDocument.characterCount());
    m_lastPartialRenderStateMergeResult = PartialRenderStateMerger::merge(previousSourceMap,
                                                                          previousBlocks,
                                                                          previousSyntaxTokens,
                                                                          previousFragments,
                                                                          m_lastPartialRenderResult,
                                                                          m_lastPartialPatchPlan);
    m_lastPartialPatchSimulationResult = {};
    m_lastPartialPatchSimulationResult.attempted = static_cast<bool>(m_lastPartialRenderResult.document);
    if (!m_lastPartialPatchSimulationResult.attempted) {
        return false;
    }

    PartialDocumentPatchApplyResult patchResult = PartialDocumentPatchApplier::applyRangeReplacement(
        previousTextDocument, *m_lastPartialRenderResult.document, m_lastPartialPatchPlan);
    if (!patchResult.ok) {
        m_lastPartialPatchSimulationResult.errors = patchResult.errors;
        return false;
    }
    if (!patchResult.document) {
        m_lastPartialPatchSimulationResult.errors.append(QStringLiteral("Partial patch simulation produced no document."));
        return false;
    }

    const IncrementalRenderValidationDecision validationDecision =
        IncrementalRenderValidationPolicy::evaluate(m_incrementalRenderValidationMode,
                                                    m_lastPartialPatchPlan,
                                                    m_lastPartialPatchDryRun,
                                                    m_lastPartialRenderStateMergeResult,
                                                    m_lastPartialRenderResult);
    m_lastIncrementalRenderValidationMode = validationDecision.mode;
    m_lastIncrementalRenderValidationReason = validationDecision.reason;

    TextDocumentPatchConsistencyResult consistency;
    consistency.ok = true;
    if (validationDecision.mode == IncrementalRenderValidationMode::AlwaysFullCompare) {
        std::optional<RenderResult> localFullResult;
        const RenderResult* comparisonFullResult = fullResult;
        if (!comparisonFullResult) {
            localFullResult = renderFullResult(renderer);
            comparisonFullResult = &*localFullResult;
        }
        if (!comparisonFullResult->document) {
            m_lastPartialPatchSimulationResult.errors.append(QStringLiteral("Full render comparison produced no document."));
            return false;
        }
        consistency = TextDocumentPatchConsistency::compare(*patchResult.document, *comparisonFullResult->document);
        m_lastPartialPatchSimulationResult.usedFullComparison = true;
    }
    if (!consistency.ok) {
        m_lastPartialPatchSimulationResult.errors = consistency.errors;
    } else {
        m_lastPartialPatchSimulationResult.ok = true;
    }

    const PartialPatchApplyDecision decision =
        PartialPatchApplyPolicy::decide(m_partialPatchRenderingEnabled,
                                        m_lastPartialPatchPlan,
                                        m_lastPartialPatchDryRun,
                                        consistency);
    m_lastPartialPatchApplyDecisionReason = decision.reason;
    if (!decision.shouldApply || !m_lastPartialRenderStateMergeResult.ok) {
        return false;
    }

    m_textDocument = std::move(patchResult.document);
    m_sourceMap = m_lastPartialRenderStateMergeResult.sourceMap;
    m_blocks = m_lastPartialRenderStateMergeResult.blocks;
    m_syntaxTokens = m_lastPartialRenderStateMergeResult.syntaxTokens;
    m_fragments = m_lastPartialRenderStateMergeResult.fragments;
    m_lastPartialPatchSimulationResult.appliedToDocument = true;
    return true;
}

void Document::applyRenderResult(RenderResult result)
{
    m_textDocument = std::move(result.document);
    m_sourceMap = std::move(result.sourceMap);
    m_blocks = std::move(result.blocks);
    m_syntaxTokens = std::move(result.syntaxTokens);
    m_fragments = std::move(result.fragments);
}

bool Document::tryLocalBlockReparse(const MarkdownDocument& previousDocument)
{
    m_lastBlockReparseShadowResult = {};
    m_lastBlockReparseShadowResult.attempted = true;
    m_lastBlockReparseShadowResult.range = m_lastBlockParseRange;

    m_lastBlockReparseShadowResult.reparse =
        BlockReparser::reparse(previousDocument, m_lastBlockParseRange, m_markdown);
    if (!m_lastBlockReparseShadowResult.reparse.ok) {
        m_lastBlockReparseShadowResult.errors.append(m_lastBlockReparseShadowResult.reparse.errors);
        return false;
    }

    m_lastBlockReparseShadowResult.merge =
        BlockDocumentMerger::mergeReparsedBlock(previousDocument, m_lastBlockReparseShadowResult.reparse);
    if (!m_lastBlockReparseShadowResult.merge.ok) {
        m_lastBlockReparseShadowResult.errors.append(m_lastBlockReparseShadowResult.merge.errors);
        return false;
    }

    m_lastBlockReparseShadowResult.ok = true;
    return true;
}

void Document::runBlockReparseShadowMerge(const MarkdownDocument& previousDocument)
{
    m_lastBlockReparseShadowResult = {};
    m_lastBlockReparseShadowResult.attempted = true;
    m_lastBlockReparseShadowResult.range = m_lastBlockParseRange;

    m_lastBlockReparseShadowResult.reparse =
        BlockReparser::reparse(previousDocument, m_lastBlockParseRange, m_markdown);
    if (!m_lastBlockReparseShadowResult.reparse.ok) {
        m_lastBlockReparseShadowResult.errors.append(m_lastBlockReparseShadowResult.reparse.errors);
        return;
    }

    m_lastBlockReparseShadowResult.merge =
        BlockDocumentMerger::mergeReparsedBlock(previousDocument, m_lastBlockReparseShadowResult.reparse);
    if (!m_lastBlockReparseShadowResult.merge.ok) {
        m_lastBlockReparseShadowResult.errors.append(m_lastBlockReparseShadowResult.merge.errors);
        return;
    }

    m_lastBlockReparseShadowResult.consistency =
        BlockDocumentMerger::compareMergedBlockToFullParse(m_lastBlockReparseShadowResult.merge.document,
                                                           m_markdownDocument,
                                                           m_lastBlockReparseShadowResult.merge.replacedNodeId,
                                                           m_lastBlockParseRange.expandedSource);
    if (!m_lastBlockReparseShadowResult.consistency.ok) {
        m_lastBlockReparseShadowResult.errors.append(m_lastBlockReparseShadowResult.consistency.errors);
        return;
    }

    m_lastBlockReparseShadowResult.ok = true;
}

void Document::enqueueRenderUpdateForInvalidatedNodes()
{
    if (m_lastInvalidatedNodeIds.isEmpty()) {
        return;
    }
    RenderUpdateRequest request;
    request.nodeIds = m_lastInvalidatedNodeIds;
    request.editedSource = m_lastBlockParseRange.editedSource;
    request.reason = QStringLiteral("invalidated-nodes");
    m_renderUpdateQueue.enqueue(std::move(request));
}

void Document::resetPartialRenderObservations()
{
    m_lastPartialRenderResult = {};
    m_lastPartialPatchPlan = {};
    m_lastPartialPatchDryRun = {};
    m_lastPartialRenderStateMergeResult = {};
    m_lastPartialPatchSimulationResult = {};
    m_lastIncrementalRenderValidationMode = IncrementalRenderValidationMode::Auto;
    m_lastIncrementalRenderValidationReason.clear();
    m_lastPartialPatchApplyDecisionReason.clear();
    m_lastRenderUpdateBatch = {};
    m_renderUpdateQueue.clear();
}

void Document::scheduleRender(SourceSelection deferredSelection)
{
    m_deferredSelection = deferredSelection;
    m_renderScheduled = true;
    if (m_renderTimerEnabled) {
        m_renderTimer.start();
    } else {
        m_renderScheduled = false;
        render();
        emitDeferredSignals(deferredSelection);
        m_deferredSelection = {-1, -1};
    }
}

void Document::flushRender()
{
    m_renderTimer.stop();
    if (!m_renderScheduled) {
        return;
    }
    m_renderScheduled = false;
    const SourceSelection selection = m_deferredSelection;
    m_deferredSelection = {-1, -1};
    render();
    emitDeferredSignals(selection);
}

void Document::onRenderTimerElapsed()
{
    m_renderScheduled = false;
    const SourceSelection selection = m_deferredSelection;
    m_deferredSelection = {-1, -1};
    render();
    emitDeferredSignals(selection);
}

void Document::emitDeferredSignals(SourceSelection selection)
{
    emit markdownChanged();
    if (selection.start >= 0 && selection.end >= 0) {
        if (selection.start > selection.end) {
            std::swap(selection.start, selection.end);
        }
        emit sourceSelectionRequested(selection);
        if (selection.start == selection.end) {
            emit cursorSourceOffsetRequested(selection.start);
        }
    }
}

} // namespace Muffin
