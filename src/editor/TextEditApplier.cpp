#include "editor/TextEditApplier.h"

#include "diagnostics/ScopedPerfProbe.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/CursorMapper.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <QLoggingCategory>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(editPerf, "muffin.perf", QtWarningMsg)

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, editPerf()) {}
};

// Layouts are kept per top-level block, so that is what a node resolves to for a refresh.
NodeId layoutBlockOf(const MarkdownDocument& document, NodeId nodeId) {
  const MarkdownNode* node = document.node(nodeId);
  return node && node->parent() ? node->topLevelBlock()->id() : NodeId();
}

// The current selection with both ends anchored to source offsets, so that undo finds them again
// when the document was re-parsed and the nodes they named are gone.
SelectionRange anchoredSelection(const BlockEditContextResolver& resolver, SelectionController* selection) {
  if (!selection || !selection->hasCursor()) {
    return {};
  }
  SelectionRange range = selection->selection();
  const bool collapsed = range.isCollapsed();
  range.anchor = resolver.sourceAnchoredCursor(range.anchor);
  range.focus = collapsed ? range.anchor : resolver.sourceAnchoredCursor(range.focus);
  return range;
}

} // namespace

void requestEditRefresh(const EditorContext& ctx, TextEditResult result, QVector<NodeId> blocks) {
  if (!ctx.brushQueue) {
    return;
  }
  if (result == TextEditResult::Reparsed) {
    ctx.brushQueue->requestFullRefresh();
    return;
  }
  // An empty list, or one of ids the document no longer knows, asks for everything.
  ctx.brushQueue->requestBlocksRefresh(std::move(blocks));
}

EditOutcome applyEdit(EditorContext& ctx, TextEdit edit, const EditOptions& options) {
  PerfTimer perf("edit.apply");
  EditOutcome outcome;
  DocumentSession* session = ctx.session;
  if (!session || !session->isEditingAllowed() || edit.sourceStart < 0 || edit.removedLength < 0 ||
      edit.sourceStart + edit.removedLength > session->markdownText().size()) {
    return outcome;
  }

  const BlockEditContextResolver resolver = ctx.contextResolver();
  outcome.beforeSelection = anchoredSelection(resolver, ctx.selection);
  const SelectionRange& beforeSelection = outcome.beforeSelection;
  const CursorPosition& beforeCursor = beforeSelection.focus;
  QVector<NodeId> editedBlocks;
  for (const LocalEditNodeHint& hint : edit.nodeHints) {
    editedBlocks.push_back(hint.nodeId);
  }
  const QString insertedText = std::move(edit.insertedText);
  outcome.sourceStart = edit.sourceStart;
  outcome.removedLength = edit.removedLength;
  outcome.removedText = session->markdownText().mid(edit.sourceStart, edit.removedLength);

  // Undo of a structural edit, or of an edit made without a caret, restores a snapshot of the
  // document from before it. Its text has to be read now: after the edit it could only be
  // reconstructed, and reconstructing is wrong when the edit re-flowed separators. Reading the
  // whole text costs a pass over the document, so it is left out for ordinary typing, which
  // records just its text change.
  const bool snapshotExpected =
      ctx.undoStack && !options.undoRecord && (options.snapshotUndo || !beforeCursor.isValid());
  QString beforeText = snapshotExpected ? session->markdownText().toString() : QString();
  const QVector<qsizetype> markersBefore = session->pendingMarkerOffsets();

  const TextEditResult result =
      session->applyTextEdit(edit.sourceStart, edit.removedLength, insertedText, std::move(edit.nodeHints));
  if (result == TextEditResult::Rejected) {
    return outcome;
  }
  outcome.applied = true;
  outcome.reparsed = result == TextEditResult::Reparsed;

  if (ctx.selection) {
    PerfTimer caretTimer("edit.apply.caret");
    CursorPosition caret;
    if (options.selectionStart >= 0 && options.selectionEnd > options.selectionStart) {
      const CursorPosition start = cursorForSourceOffset(resolver, options.selectionStart);
      const CursorPosition end = cursorForSourceOffset(resolver, options.selectionEnd);
      if (start.isValid() && end.isValid()) {
        const bool backwards = options.selectionBackwards;
        const SelectionRange range = backwards ? SelectionRange{end, start} : SelectionRange{start, end};
        ctx.selection->setSelection(range);
        caret = range.focus;
      }
    }
    if (!caret.isValid()) {
      // The offset is given for the text as the edit leaves it.
      const qsizetype fallback = qBound<qsizetype>(0, options.fallbackSourceOffset, session->markdownText().size());
      caret = cursorAfterEdit(resolver, options.cursor, fallback, options.preferLaterEmptyAtOffset);
      ctx.selection->setCursorPosition(caret);
    }
    outcome.cursor = caret;
  }

  if (ctx.undoStack) {
    PerfTimer undoTimer("edit.apply.undo");
    EditTransaction transaction;
    if (options.undoRecord) {
      transaction = options.undoRecord(outcome);
    } else if (result == TextEditResult::AppliedLocally && !options.snapshotUndo && beforeCursor.isValid() &&
               outcome.cursor.isValid()) {
      transaction = EditTransaction(options.kind, options.label,
                                    TextDeltaCommand{TextDelta{edit.sourceStart, outcome.removedText, insertedText},
                                                     beforeCursor, outcome.cursor,
                                                     QVector<NodeId>{layoutBlockOf(session->document(),
                                                                                  outcome.cursor.blockId)}});
    } else {
      // The text before the edit is the text after it with the replacement undone: only the edit
      // itself changes the text, whichever way the session applied it.
      PerfTimer snapshotTimer("edit.apply.undoSnapshot");
      if (!snapshotExpected) {
        beforeText = session->markdownText().toString();
        beforeText.replace(edit.sourceStart, insertedText.size(), outcome.removedText);
      }
      transaction = EditTransaction(options.kind, options.label,
                                    DocumentSnapshot{beforeText, beforeCursor, markersBefore},
                                    DocumentSnapshot{session->markdownText().toString(), outcome.cursor,
                                                     session->pendingMarkerOffsets()});
    }
    transaction.setBeforeSelection(beforeSelection);
    ctx.undoStack->push(std::move(transaction));
  }

  if (ctx.brushQueue) {
    PerfTimer refreshTimer("edit.apply.refresh");
    BrushQueue& queue = *ctx.brushQueue;
    if (outcome.reparsed) {
      queue.requestFullRefresh();
    } else if (options.structureEdit || session->lastLocalEditChangedTopLevelStructure()) {
      // The blocks after an edited one move; laying them out on the queue's timer keeps their
      // positions and the repaint in one step.
      queue.requestTopLevelRangeRefresh(session->lastLocalTopLevelRangeChange());
    } else {
      editedBlocks.push_back(layoutBlockOf(session->document(), outcome.cursor.blockId));
      QVector<NodeId> unique;
      for (const NodeId& block : editedBlocks) {
        if (block.isValid() && !unique.contains(block)) {
          unique.push_back(block);
        }
      }
      if (unique.size() == 1) {
        queue.requestBlockRefresh(unique.first());
      } else {
        queue.requestBlocksRefresh(std::move(unique));
      }
      if (options.layoutImmediately) {
        queue.flush();
      }
    }
  }
  return outcome;
}

} // namespace muffin
