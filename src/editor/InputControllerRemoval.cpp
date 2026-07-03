#include "editor/InputController.h"

#include "document/BlockPredicates.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "edit/EditTransaction.h"
#include "edit/UndoStack.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"

namespace muffin {

bool InputController::computeStandardRemovalRange(qsizetype blockStart, qsizetype blockEnd, int nodeIndex,
                                                   qsizetype& deleteStart, qsizetype& deleteEnd) const {
  const auto& blocks = ctx_.session->document().root().children();
  deleteStart = blockStart;
  deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  const qsizetype textSize = ctx_.session->markdownText().size();
  deleteStart = qBound<qsizetype>(0, deleteStart, textSize);
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, textSize);
  return deleteStart < deleteEnd;
}

bool InputController::removeTopLevelBlock(MarkdownNode& node, int nodeIndex, qsizetype blockStart,
                                          qsizetype deleteStart, qsizetype deleteEnd, EditTransaction::Kind kind,
                                          const QString& label, bool exitLiteralEditorFirst) {
  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node.clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node.id();
  const BlockType removedNodeType = node.type();

  // Exit edit mode before removing a literal block to clear stale editing state.
  if (exitLiteralEditorFirst) {
    if (codeFenceController_ && codeFenceController_->isEditing()) {
      codeFenceController_->exitEditMode();
    } else if (LiteralBlockController* math = ctx_.literalEditors.value(static_cast<int>(BlockType::MathBlock))) {
      if (math->isEditing()) { math->exitEditMode(); }
    }
  }

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, removedNodeType});
  if (!ctx_.session->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  } else {
    ctx_.selection->clear();
  }

  if (ctx_.undoStack) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    ctx_.undoStack->push(EditTransaction(
        kind, label,
        RemoveNodeCommand{removedNodeId, removedNodeType, nodeIndex,
                          TextDelta{deleteStart, removedText, QString()}, blockStart,
                          std::move(removedNode), beforeCursor, nextCursor, std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
    // Flush synchronously so the layout reflects this deletion before control returns to the
    // event loop (mirrors applyLocalEdit); otherwise BrushQueue defers via a 0-ms timer and the
    // view paints one frame of stale layout.
    ctx_.brushQueue->flush();
  }
  return true;
}

bool InputController::tryRemoveExactWholeBlockSelection(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionRange range = ctx_.selection->selection();
  if (range.anchor.blockId != range.focus.blockId) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(range.anchor.blockId);
  if (!node || !node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  qsizetype selectionStart = -1;
  qsizetype selectionEnd = -1;
  if (!blockSelectionSourceRange(selectionStart, selectionEnd) || selectionStart != blockStart || selectionEnd != blockEnd) {
    return false;
  }

  const int nodeIndex = topLevelBlockIndex(*node);
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = 0;
  qsizetype deleteEnd = 0;
  if (!computeStandardRemovalRange(blockStart, blockEnd, nodeIndex, deleteStart, deleteEnd)) {
    return false;
  }
  return removeTopLevelBlock(*node, nodeIndex, blockStart, deleteStart, deleteEnd, kind, label,
                             /*exitLiteralEditorFirst=*/false);
}

bool InputController::tryRemoveEmptyLiteralBlock(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  // Only applicable to code fence and math block
  NodeId blockId;
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    blockId = codeFenceController_->currentCodeFenceId();
  } else if (LiteralBlockController* math = ctx_.literalEditors.value(static_cast<int>(BlockType::MathBlock))) {
    if (math->isEditing()) {
      blockId = math->currentBlockId();
    }
  }
  if (!blockId.isValid()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node || !node->literal().isEmpty()) {
    return false;
  }
  if (!node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }
  const BlockType nodeType = node->type();
  if (nodeType != BlockType::CodeFence && nodeType != BlockType::MathBlock) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const int nodeIndex = topLevelBlockIndex(*node);
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = 0;
  qsizetype deleteEnd = 0;
  if (!computeStandardRemovalRange(blockStart, blockEnd, nodeIndex, deleteStart, deleteEnd)) {
    return false;
  }
  return removeTopLevelBlock(*node, nodeIndex, blockStart, deleteStart, deleteEnd, kind, label,
                             /*exitLiteralEditorFirst=*/true);
}

bool InputController::tryRemoveEmptyDefinitionBlock(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || (node->type() != BlockType::LinkDefinition && node->type() != BlockType::FootnoteDefinition)) {
    return false;
  }
  const DefinitionBlock definition = node->definition();
  const bool empty = node->type() == BlockType::LinkDefinition
                         ? definition.label.isEmpty() && definition.destination.isEmpty() && definition.title.isEmpty()
                         : definition.label.isEmpty() && definition.note.isEmpty();
  if (!empty || !node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  const int nodeIndex = topLevelBlockIndex(*node);
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else {
    for (int i = nodeIndex + 1; i < static_cast<int>(blocks.size()); ++i) {
      const qsizetype nextStart = blocks.at(static_cast<size_t>(i))->sourceRange().byteStart;
      if (nextStart > blockStart) {
        deleteEnd = nextStart;
        break;
      }
    }
  }
  if (nodeIndex > 0) {
    qsizetype previousEnd = 0;
    for (int i = nodeIndex - 1; i >= 0; --i) {
      const qsizetype candidateEnd = blocks.at(static_cast<size_t>(i))->sourceRange().byteEnd;
      if (candidateEnd <= blockStart) {
        previousEnd = candidateEnd;
        break;
      }
    }
    deleteStart = blockStart;
    while (deleteStart > previousEnd &&
           (ctx_.session->markdownText().at(deleteStart - 1) == QLatin1Char('\n') ||
            ctx_.session->markdownText().at(deleteStart - 1) == QLatin1Char('\r'))) {
      --deleteStart;
    }
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  return removeTopLevelBlock(*node, nodeIndex, blockStart, deleteStart, deleteEnd, kind, label,
                             /*exitLiteralEditorFirst=*/false);
}

bool InputController::tryRemoveThematicBreak(bool forward) {
  // Removes a top-level thematic break the caret currently rests on. The caret reaches a rule in
  // two ways: arrow-key navigation lands directly on it (selectableBlockByDirection does not skip
  // non-editable blocks), and a click in the virtual trailing area below a rule that ends the
  // document puts the caret there with afterBlock set. Either way editParagraph rejects the block
  // (a rule has no editable text), so without this the keystroke is a silent no-op.
  //
  // The removal goes through applyLocalEdit, which falls back to a full reparse when the
  // incremental slice picker cannot anchor the edit — and it cannot here, because
  // chooseTopLevelSlice deliberately skips non-editable top-level blocks (a thematic break has no
  // content to slice). The caret lands on the neighbour the gesture points at: backspace → the
  // previous block's content end, delete → the next block's start.
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || node->type() != BlockType::ThematicBreak) {
    return false;
  }
  if (!node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  int nodeIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == node) {
      nodeIndex = i;
      break;
    }
  }
  if (nodeIndex < 0) {
    return false;
  }

  // Span the rule plus exactly one adjacent separator so the surviving blocks stay distinct.
  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  // Neighbouring editable blocks, for caret placement. applyLocalEdit falls back to a full
  // reparse here (the incremental slice picker cannot anchor a non-editable block's removal), which
  // hands every node a fresh id — so the preferred caret must be expressed as a SOURCE offset that
  // still resolves on the post-edit document, not as a node id. The preceding block's content end
  // sits before the edit point and is stable; the following block, after removing
  // [deleteStart, deleteEnd), now begins exactly at deleteStart.
  MarkdownNode* beforeEditable =
      nodeIndex > 0 ? resolver.lastEditableDescendant(*blocks.at(static_cast<size_t>(nodeIndex - 1))) : nullptr;
  MarkdownNode* afterEditable = nodeIndex + 1 < static_cast<int>(blocks.size())
                                    ? resolver.firstEditableDescendant(*blocks.at(static_cast<size_t>(nodeIndex + 1)))
                                    : nullptr;
  BlockEditContext beforeCtx;
  const bool haveBefore = beforeEditable && resolver.fill(*beforeEditable, beforeCtx);
  const qsizetype beforeEnd = haveBefore ? beforeCtx.contentRange.byteEnd : qsizetype(-1);
  CursorPosition preferred;
  auto makePreferred = [&](MarkdownNode* target, qsizetype sourceOffset) {
    preferred.blockId = target->id();
    preferred.text.sourceOffset = sourceOffset;
  };
  if (forward) {
    if (afterEditable) {
      makePreferred(afterEditable, deleteStart);
    } else if (haveBefore) {
      makePreferred(beforeEditable, beforeEnd);
    }
  } else {
    if (haveBefore) {
      makePreferred(beforeEditable, beforeEnd);
    } else if (afterEditable) {
      makePreferred(afterEditable, deleteStart);
    }
  }

  applyLocalEdit(
      EditTransaction::Kind::DeleteText,
      forward ? QStringLiteral("Delete Thematic Break") : QStringLiteral("Remove Thematic Break"),
      deleteStart,
      deleteEnd - deleteStart,
      QString(),
      preferred,
      deleteStart,
      {LocalEditNodeHint{node->id(), blockStart, BlockType::ThematicBreak}},
      false,
      true);
  return true;
}

bool InputController::collapseTrailingCaretToEndOfLastBlock() {
  if (!ctx_.hasSession() || !ctx_.selection) {
    return false;
  }
  // Resolving the end-of-document source offset lands the caret on the deepest editable
  // block — a list item's content end, a paragraph's end, or a literal block's content end —
  // and clears the afterBlock flag so the caret paints inside real content.
  CursorPosition target = cursorForSourceOffset(ctx_.session->markdownText().size());
  if (!target.isValid()) {
    // No editable content reaches the document end — e.g. a table whose last cell ends before
    // the trailing newline, or "alpha\n\n---" where the last block is a non-editable break.
    // Retreat to the last editable block's content end instead.
    BlockEditContextResolver resolver = contextResolver();
    if (MarkdownNode* last = resolver.lastEditableDescendant(ctx_.session->document().root())) {
      BlockEditContext context;
      if (resolver.fill(*last, context)) {
        target = cursorForSourceOffset(context.contentRange.byteEnd);
      }
    }
  }
  if (!target.isValid()) {
    return false;  // no editable content anywhere (e.g. a lone thematic break)
  }
  ctx_.selection->setCursorPosition(target);
  return true;
}

}  // namespace muffin
