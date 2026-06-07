#include "editor/InputController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/EditTransaction.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"
#include "editor/TextBlockCommandBuilder.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(inputEditPerf, "muffin.perf", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(inputEditPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(inputEditPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

NodeId refreshNodeFor(DocumentSession* session, NodeId nodeId) {
  if (!session || !nodeId.isValid()) {
    return nodeId;
  }
  MarkdownNode* node = session->document().node(nodeId);
  if (!node) {
    return nodeId;
  }
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  return node->id();
}

}  // namespace

BlockEditContextResolver InputController::contextResolver() const {
  return BlockEditContextResolver(ctx_.session, ctx_.selection);
}

bool InputController::selectionSourceRange(qsizetype& start, qsizetype& end) const {
  return contextResolver().selectionSourceRange(start, end);
}

bool InputController::blockSelectionSourceRange(qsizetype& start, qsizetype& end) const {
  if (!ctx_.selection || !ctx_.selection->hasCursor() || !ctx_.session || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionRange range = ctx_.selection->selection();
  MarkdownNode* anchorNode = ctx_.session->document().node(range.anchor.blockId);
  MarkdownNode* focusNode = ctx_.session->document().node(range.focus.blockId);
  if (!anchorNode || !focusNode) {
    return false;
  }

  qsizetype anchorStart = -1;
  qsizetype anchorEnd = -1;
  qsizetype focusStart = -1;
  qsizetype focusEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*anchorNode, anchorStart, anchorEnd) || !resolver.blockSourceRange(*focusNode, focusStart, focusEnd)) {
    return false;
  }

  if (anchorStart < focusStart || (anchorStart == focusStart && range.anchor.text.textOffset <= range.focus.text.textOffset)) {
    start = anchorStart + (anchorNode == focusNode ? qBound<qsizetype>(0, range.anchor.text.textOffset, anchorEnd - anchorStart) : 0);
    end = focusEnd;
    if (anchorNode == focusNode) {
      end = anchorStart + qBound<qsizetype>(0, range.focus.text.textOffset, anchorEnd - anchorStart);
    }
  } else {
    start = focusStart + (anchorNode == focusNode ? qBound<qsizetype>(0, range.focus.text.textOffset, focusEnd - focusStart) : 0);
    end = anchorEnd;
    if (anchorNode == focusNode) {
      end = focusStart + qBound<qsizetype>(0, range.anchor.text.textOffset, focusEnd - focusStart);
    }
  }
  return start < end;
}

bool InputController::editParagraph(TextBlockCommandBuilder::Operation operation, QString text) {
  PerfTimer perf("input.editParagraph.buildCommand");
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context)) {
    emit unsupportedEditRequested(QStringLiteral("Only plain paragraph, heading, and list item text is editable in this M4 slice."));
    return false;
  }

  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildTextEdit(context, operation, std::move(text)));
}

bool InputController::applyTextCommand(const TextBlockCommandBuilder::Command& command) {
  if (!command.valid) {
    return false;
  }
  if (!command.handled) {
    return true;
  }

  if (command.hasLocalEdit()) {
    applyLocalEdit(
        command.kind,
        command.label,
        command.sourceStart,
        command.removedLength,
        command.insertedText,
        command.preferredCursor,
        command.fallbackSourceOffset,
        command.nodeHints,
        command.preferLaterEmptyAtOffset,
        command.structureEdit);
  }
  return true;
}

void InputController::performLocalEdit(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    CursorPosition preferredCursor,
    qsizetype fallbackSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool preferLaterEmptyAtOffset,
    bool structureEdit) {
  applyLocalEdit(kind, label, sourceStart, removedLength, std::move(insertedText), std::move(preferredCursor),
                 fallbackSourceOffset, std::move(nodeHints), preferLaterEmptyAtOffset, structureEdit);
}

void InputController::applyLocalEdit(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    CursorPosition preferredCursor,
    qsizetype fallbackSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool preferLaterEmptyAtOffset,
    bool structureEdit) {
  PerfTimer perf("input.applyLocalEdit");
  if (!ctx_.session || sourceStart < 0 || removedLength < 0) {
    return;
  }

  const CursorPosition beforeCursor = ctx_.selection && ctx_.selection->hasCursor() ? ctx_.selection->cursorPosition() : CursorPosition();
  const QString& currentText = ctx_.session->markdownText();
  if (sourceStart + removedLength > currentText.size()) {
    return;
  }

  const QString removedText = currentText.mid(sourceStart, removedLength);
  const bool snapshotUndoLikely = ctx_.undoStack && !beforeCursor.blockId.isValid();
  QString beforeText = snapshotUndoLikely ? QString(currentText) : QString();
  bool beforeTextCaptured = snapshotUndoLikely;
  const bool appliedLocally =
      ctx_.session->applyTextDelta(sourceStart, removedLength, insertedText, true, std::move(nodeHints));
  QString nextText;
  if (!appliedLocally) {
    if (!beforeTextCaptured) {
      beforeText = currentText;
      beforeTextCaptured = true;
    }
    nextText = beforeText;
    nextText.replace(sourceStart, removedLength, insertedText);
    ctx_.session->applyMarkdownText(nextText, true);
  }

  CursorPosition nextCursor = cursorAfterEdit(preferredCursor, fallbackSourceOffset, preferLaterEmptyAtOffset);
  if (ctx_.selection) {
    if (!nextCursor.isValid()) {
      nextCursor = cursorForSourceOffset(ctx_.session->markdownText().size());
    }
    ctx_.selection->setCursorPosition(nextCursor);
  }

  if (ctx_.undoStack) {
    const bool textDeltaUndoEligible = appliedLocally && !structureEdit && beforeCursor.isValid() && nextCursor.isValid();
    if (textDeltaUndoEligible) {
      QVector<NodeId> affectedNodes;
      affectedNodes.push_back(refreshNodeFor(ctx_.session, nextCursor.blockId));
      ctx_.undoStack->push(EditTransaction(
          kind,
          label,
          TextDeltaCommand{
              TextDelta{sourceStart, removedText, insertedText},
              beforeCursor,
              nextCursor,
              std::move(affectedNodes)}));
    } else {
      if (!beforeTextCaptured) {
        beforeText = ctx_.session->markdownText();
        beforeText.replace(sourceStart, insertedText.size(), removedText);
        beforeTextCaptured = true;
      }
      if (nextText.isEmpty()) {
        nextText = ctx_.session->markdownText();
      }
      ctx_.undoStack->push(EditTransaction(kind, label, {beforeText, beforeCursor}, {std::move(nextText), nextCursor}));
    }
  }
  if (ctx_.brushQueue) {
    if (!appliedLocally) {
      ctx_.brushQueue->requestFullRefresh();
    } else if (structureEdit || ctx_.session->lastLocalEditChangedTopLevelStructure()) {
      ctx_.brushQueue->requestTopLevelRangeRefresh(ctx_.session->lastLocalTopLevelRangeChange());
    } else {
      ctx_.brushQueue->requestBlockRefresh(refreshNodeFor(ctx_.session, nextCursor.blockId));
    }
  }
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    qsizetype nextSourceOffset) {
  applyEdit(kind, label, std::move(nextText), nextSourceOffset, false);
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    qsizetype nextSourceOffset,
    bool preferLaterEmptyAtOffset) {
  applyEdit(kind, label, std::move(nextText), CursorPosition(), nextSourceOffset, {}, preferLaterEmptyAtOffset);
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    CursorPosition preferredCursor,
    qsizetype fallbackSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool preferLaterEmptyAtOffset) {
  PerfTimer perf("input.applyEdit.diffFallback");
  const CursorPosition beforeCursor = ctx_.selection && ctx_.selection->hasCursor() ? ctx_.selection->cursorPosition() : CursorPosition();
  const QString beforeText = ctx_.session->markdownText();

  qsizetype prefix = 0;
  const qsizetype minSize = qMin(beforeText.size(), nextText.size());
  while (prefix < minSize && beforeText.at(prefix) == nextText.at(prefix)) {
    ++prefix;
  }
  qsizetype beforeSuffix = beforeText.size();
  qsizetype nextSuffix = nextText.size();
  while (beforeSuffix > prefix && nextSuffix > prefix && beforeText.at(beforeSuffix - 1) == nextText.at(nextSuffix - 1)) {
    --beforeSuffix;
    --nextSuffix;
  }

  const bool appliedLocally =
      ctx_.session->applyTextDelta(prefix, beforeSuffix - prefix, nextText.mid(prefix, nextSuffix - prefix), true, std::move(nodeHints));
  if (!appliedLocally) {
    ctx_.session->applyMarkdownText(nextText, true);
  }
  CursorPosition nextCursor = cursorAfterEdit(preferredCursor, fallbackSourceOffset, preferLaterEmptyAtOffset);
  if (ctx_.selection) {
    if (!nextCursor.isValid()) {
      nextCursor = cursorForSourceOffset(ctx_.session->markdownText().size());
    }
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    const QString removedText = beforeText.mid(prefix, beforeSuffix - prefix);
    const QString insertedText = nextText.mid(prefix, nextSuffix - prefix);
    const bool textDeltaUndoEligible = appliedLocally && beforeCursor.isValid() && nextCursor.isValid();
    if (textDeltaUndoEligible) {
      QVector<NodeId> affectedNodes;
      affectedNodes.push_back(refreshNodeFor(ctx_.session, nextCursor.blockId));
      ctx_.undoStack->push(EditTransaction(
          kind,
          label,
          TextDeltaCommand{
              TextDelta{prefix, removedText, insertedText},
              beforeCursor,
              nextCursor,
              std::move(affectedNodes)}));
    } else {
      ctx_.undoStack->push(EditTransaction(kind, label, {beforeText, beforeCursor}, {std::move(nextText), nextCursor}));
    }
  }
  if (ctx_.brushQueue) {
    if (!appliedLocally) {
      ctx_.brushQueue->requestFullRefresh();
    } else if (ctx_.session->lastLocalEditChangedTopLevelStructure()) {
      ctx_.brushQueue->requestTopLevelRangeRefresh(ctx_.session->lastLocalTopLevelRangeChange());
    } else {
      ctx_.brushQueue->requestBlockRefresh(refreshNodeFor(ctx_.session, nextCursor.blockId));
    }
  }
}

}  // namespace muffin
