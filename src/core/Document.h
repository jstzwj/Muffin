#pragma once
#include "parser/AstTree.h"
#include "editor/EditTransaction.h"
#include "editor/EditTransactionApplier.h"
#include "editor/EditTransactionValidator.h"
#include "editor/EditorSelectionMapper.h"
#include "editor/MarkerVisibilityController.h"
#include "parser/MathSpan.h"
#include "editor/MarkdownCommand.h"
#include "model/BlockDocumentMerger.h"
#include "model/MarkdownDocument.h"
#include "parser/BlockReparsePlanner.h"
#include "parser/BlockReparser.h"
#include "renderer/IncrementalRenderValidationPolicy.h"
#include "renderer/MarkdownBlock.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/PartialDocumentPatchApplier.h"
#include "renderer/PartialDocumentPatchPlanner.h"
#include "renderer/PartialPatchApplyPolicy.h"
#include "renderer/PartialRenderStateMerger.h"
#include "renderer/RenderSourceMap.h"
#include "renderer/RenderUpdateQueue.h"
#include "renderer/SyntaxTokenSpan.h"
#include "theme/Theme.h"
#include <QTextDocument>
#include <QTimer>
#include <QUndoStack>
#include <QString>
#include <memory>
#include <optional>

namespace Muffin {

namespace Internal {
class MarkdownEditCommand;
}

enum class StructuralEditStatus {
    NotApplicable,
    Ready,
    DryRunFailed
};

struct StructuralEditObservation {
    StructuralEditStatus undoStatus = StructuralEditStatus::NotApplicable;
    StructuralEditStatus redoStatus = StructuralEditStatus::NotApplicable;
    QStringList undoErrors;
    QStringList redoErrors;
};

struct PartialPatchSimulationResult {
    bool attempted = false;
    bool ok = false;
    bool appliedToDocument = false;
    bool usedFullComparison = false;
    QStringList errors;
};

struct IncrementalRenderObservation {
    bool attempted = false;
    bool ok = false;
    bool appliedToDocument = false;
    IncrementalRenderValidationMode validationMode = IncrementalRenderValidationMode::Auto;
    QString validationReason;
    QString applyDecisionReason;
    QVector<MarkdownNodeId> invalidatedNodeIds;
    RenderSpan::Kind patchKind = RenderSpan::Kind::Unsupported;
    QStringList errors;
};

struct BlockReparseShadowResult {
    bool attempted = false;
    bool ok = false;
    bool appliedToDocument = false;
    BlockParseRange range;
    BlockReparseResult reparse;
    BlockMergeResult merge;
    BlockMergeConsistencyResult consistency;
    QStringList errors;
};

enum class ParseMode {
    FullParse,
    LocalReparse,
};

struct BlockLocalReparseObservation {
    bool planned = false;
    bool shadowAttempted = false;
    bool shadowOk = false;
    bool applied = false;
    QString fallbackReason;
    QStringList errors;
    SourceSpan editedSource;
    SourceSpan expandedSource;
    QVector<MarkdownNodeId> affectedBlockIds;
};

class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    QString markdown() const { return m_markdown; }
    QTextDocument* textDocument() const { return m_textDocument.get(); }
    const MarkdownDocument& markdownDocument() const { return m_markdownDocument; }
    const QVector<MathSpan>& mathSpans() const { return m_mathSpans; }
    const RenderSourceMap& sourceMap() const { return m_sourceMap; }
    const QVector<MarkdownBlock>& blocks() const { return m_blocks; }
    const QVector<SyntaxTokenSpan>& syntaxTokens() const { return m_syntaxTokens; }
    const QVector<RenderFragment>& renderFragments() const { return m_fragments; }
    const QVector<MarkdownNodeId>& lastInvalidatedNodeIds() const { return m_lastInvalidatedNodeIds; }
    const RenderUpdateBatch& lastRenderUpdateBatch() const { return m_lastRenderUpdateBatch; }
    const BlockParseRange& lastBlockParseRange() const { return m_lastBlockParseRange; }
    const BlockReparseShadowResult& lastBlockReparseShadowResult() const { return m_lastBlockReparseShadowResult; }
    const PartialRenderResult& lastPartialRenderResult() const { return m_lastPartialRenderResult; }
    const PartialDocumentPatchPlan& lastPartialPatchPlan() const { return m_lastPartialPatchPlan; }
    const PartialDocumentPatchDryRun& lastPartialPatchDryRun() const { return m_lastPartialPatchDryRun; }
    const PartialRenderStateMergeResult& lastPartialRenderStateMergeResult() const { return m_lastPartialRenderStateMergeResult; }
    const PartialPatchSimulationResult& lastPartialPatchSimulationResult() const { return m_lastPartialPatchSimulationResult; }
    IncrementalRenderObservation lastIncrementalRenderObservation() const;
    QString lastRenderDiagnosticsText() const;
    QString lastRenderDiagnosticsSummary() const;
    const std::optional<EditTransactionApplyResult>& lastStructuralUndoDryRunResult() const { return m_lastStructuralUndoDryRunResult; }
    const std::optional<EditTransactionApplyResult>& lastStructuralRedoDryRunResult() const { return m_lastStructuralRedoDryRunResult; }
    bool lastStructuralUndoApplied() const { return m_lastStructuralUndoApplied; }
    bool lastStructuralRedoApplied() const { return m_lastStructuralRedoApplied; }
    StructuralEditObservation lastStructuralEditObservation() const;
    BlockLocalReparseObservation lastBlockLocalReparseObservation() const;
    ParseMode lastParseMode() const { return m_parseMode; }
    MarkerVisibilityState markerVisibilityStateForCaret(const SelectionBookmark& caret) const;
    MarkerVisibilityState markerVisibilityStateForSelection(const SelectionRangeBookmark& selection) const;
    MarkerVisibilityState markerVisibilityStateForPinnedMarker(PinnedMarker marker) const;

    void setMarkdown(const QString& markdown);
    void applyMarkdownEdit(const QString& markdown, int cursorSourceOffset, const QString& label);
    void applyMarkdownEdit(const QString& markdown, SourceSelection selection, const QString& label);
    void applyMarkdownEdit(EditTransaction transaction, const QString& label);
    void applyMarkdownEdit(EditTransaction transaction,
                           std::optional<EditTransactionValidationResult> validation,
                           const QString& label);
    QUndoStack* undoStack() { return &m_undoStack; }
    const QUndoStack* undoStack() const { return &m_undoStack; }
    std::optional<EditTransaction> editTransactionAt(int undoIndex) const;
    std::optional<EditTransactionValidationResult> editTransactionValidationAt(int undoIndex) const;
    bool canApplyEditStructurallyAt(int undoIndex) const;
    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }
    void setFilePath(const QString& path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }
    void setPartialPatchRenderingEnabledForTesting(bool enabled) { m_partialPatchRenderingEnabled = enabled; }
    void setIncrementalRenderValidationModeForTesting(IncrementalRenderValidationMode mode)
    {
        m_incrementalRenderValidationMode = mode;
    }
    void setBlockLocalReparseEnabledForTesting(bool enabled) { m_blockLocalReparseEnabled = enabled; }
    void setRenderTimerEnabled(bool enabled) { m_renderTimerEnabled = enabled; }

    bool isModified() const;

signals:
    void markdownChanged();
    void documentRendered();
    void cursorSourceOffsetRequested(int offset);
    void sourceSelectionRequested(SourceSelection selection);

private:
    friend class Internal::MarkdownEditCommand;

    void setMarkdownInternal(const QString& markdown, SourceSelection selection = {}, bool planBlockReparse = true, bool immediateRender = false);
    void setMarkdownDocumentInternal(MarkdownDocument document, const QString& markdown, SourceSelection selection = {});
    void render();
    RenderResult renderFullResult(DocumentRenderer& renderer);
    bool tryRenderIncremental(DocumentRenderer& renderer,
                              const QTextDocument& previousTextDocument,
                              const RenderSourceMap& previousSourceMap,
                              const QVector<MarkdownBlock>& previousBlocks,
                              const QVector<SyntaxTokenSpan>& previousSyntaxTokens,
                              const QVector<RenderFragment>& previousFragments,
                              const RenderUpdateBatch& updateBatch,
                              const RenderResult* fullResult);
    void applyRenderResult(RenderResult result);
    void resetPartialRenderObservations();
    bool tryLocalBlockReparse(const MarkdownDocument& previousDocument);
    void runBlockReparseShadowMerge(const MarkdownDocument& previousDocument);
    void enqueueRenderUpdateForInvalidatedNodes();
    void scheduleRender(SourceSelection deferredSelection);
    void flushRender();
    void onRenderTimerElapsed();
    void emitDeferredSignals(SourceSelection selection);

    QString m_markdown;
    // Parse-only legacy cache; the editing/rendering model is MarkdownDocument.
    AstTree m_lastParsedAstTree;
    MarkdownDocument m_markdownDocument;
    QVector<MathSpan> m_mathSpans;
    std::unique_ptr<QTextDocument> m_textDocument;
    RenderSourceMap m_sourceMap;
    QVector<MarkdownBlock> m_blocks;
    QVector<SyntaxTokenSpan> m_syntaxTokens;
    QVector<RenderFragment> m_fragments;
    QVector<MarkdownNodeId> m_lastInvalidatedNodeIds;
    RenderUpdateQueue m_renderUpdateQueue;
    RenderUpdateBatch m_lastRenderUpdateBatch;
    BlockParseRange m_lastBlockParseRange;
    BlockReparseShadowResult m_lastBlockReparseShadowResult;
    PartialRenderResult m_lastPartialRenderResult;
    PartialDocumentPatchPlan m_lastPartialPatchPlan;
    PartialDocumentPatchDryRun m_lastPartialPatchDryRun;
    PartialRenderStateMergeResult m_lastPartialRenderStateMergeResult;
    PartialPatchSimulationResult m_lastPartialPatchSimulationResult;
    IncrementalRenderValidationMode m_lastIncrementalRenderValidationMode = IncrementalRenderValidationMode::Auto;
    QString m_lastIncrementalRenderValidationReason;
    QString m_lastPartialPatchApplyDecisionReason;
    std::optional<EditTransactionApplyResult> m_lastStructuralUndoDryRunResult;
    std::optional<EditTransactionApplyResult> m_lastStructuralRedoDryRunResult;
    bool m_lastStructuralUndoApplied = false;
    bool m_lastStructuralRedoApplied = false;
    bool m_partialPatchRenderingEnabled = true;
    ParseMode m_parseMode = ParseMode::FullParse;
    bool m_blockLocalReparseEnabled = true;
    IncrementalRenderValidationMode m_incrementalRenderValidationMode = IncrementalRenderValidationMode::Auto;
    QUndoStack m_undoStack;
    QString m_filePath;
    Theme m_theme = Theme::preset(ThemePreset::Github);
    QTimer m_renderTimer;
    bool m_renderScheduled = false;
    bool m_renderTimerEnabled = false;
    SourceSelection m_deferredSelection = {-1, -1};
};

} // namespace Muffin
