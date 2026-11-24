#include "editor/InputController.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/EditTransaction.h"
#include "editor/BrushQueue.h"
#include "editor/CursorMapper.h"
#include "editor/SelectionController.h"
#include "editor/TextEditApplier.h"
#include "editor/TextBlockCommandBuilder.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(inputEditPerf, "muffin.perf", QtWarningMsg)

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, inputEditPerf()) {}
};

} // namespace

BlockEditContextResolver InputController::contextResolver() const {
  return BlockEditContextResolver(ctx_.session, ctx_.selection);
}

bool InputController::selectionSourceRange(qsizetype& start, qsizetype& end) const {
  return contextResolver().selectionSourceRange(start, end);
}

bool InputController::blockSelectionSourceRange(qsizetype& start, qsizetype& end) const {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
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
  if (!resolver.blockSourceRange(*anchorNode, anchorStart, anchorEnd) ||
      !resolver.blockSourceRange(*focusNode, focusStart, focusEnd)) {
    return false;
  }

  if (anchorStart < focusStart ||
      (anchorStart == focusStart && range.anchor.text.textOffset <= range.focus.text.textOffset)) {
    start = anchorStart +
            (anchorNode == focusNode ? qBound<qsizetype>(0, range.anchor.text.textOffset, anchorEnd - anchorStart) : 0);
    end = focusEnd;
    if (anchorNode == focusNode) {
      end = anchorStart + qBound<qsizetype>(0, range.focus.text.textOffset, anchorEnd - anchorStart);
    }
  } else {
    start = focusStart +
            (anchorNode == focusNode ? qBound<qsizetype>(0, range.focus.text.textOffset, focusEnd - focusStart) : 0);
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
    emit unsupportedEditRequested(
        QStringLiteral("Only plain paragraph, heading, and list item text is editable in this M4 slice."));
    return false;
  }

  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildTextEdit(context, operation, std::move(text)));
}

bool InputController::applyTextCommand(const TextBlockCommandBuilder::Command& command) {
  if (!ctx_.hasSession() || !ctx_.session->isEditingAllowed() || !command.valid) {
    return false;
  }
  if (!command.handled) {
    return true;
  }

  if (command.hasLocalEdit()) {
    applyLocalEdit(command.kind, command.label, command.sourceStart, command.removedLength, command.insertedText,
                   command.preferredCursor, command.fallbackSourceOffset, command.nodeHints,
                   command.preferLaterEmptyAtOffset, command.structureEdit);
  }
  return true;
}

void InputController::performLocalEdit(EditTransaction::Kind kind, const QString& label, qsizetype sourceStart,
                                       qsizetype removedLength, QString insertedText, CursorPosition preferredCursor,
                                       qsizetype fallbackSourceOffset, QVector<LocalEditNodeHint> nodeHints,
                                       bool preferLaterEmptyAtOffset, bool structureEdit) {
  applyLocalEdit(kind, label, sourceStart, removedLength, std::move(insertedText), std::move(preferredCursor),
                 fallbackSourceOffset, std::move(nodeHints), preferLaterEmptyAtOffset, structureEdit);
}

void InputController::applyLocalEdit(EditTransaction::Kind kind, const QString& label, qsizetype sourceStart,
                                     qsizetype removedLength, QString insertedText, CursorPosition preferredCursor,
                                     qsizetype fallbackSourceOffset, QVector<LocalEditNodeHint> nodeHints,
                                     bool preferLaterEmptyAtOffset, bool structureEdit) {
  PerfTimer perf("input.applyLocalEdit");
  const EditOutcome outcome =
      applyEdit(ctx_,
                {.sourceStart = sourceStart,
                 .removedLength = removedLength,
                 .insertedText = std::move(insertedText),
                 .nodeHints = std::move(nodeHints)},
                {.kind = kind,
                 .label = label,
                 .cursor = std::move(preferredCursor),
                 .fallbackSourceOffset = fallbackSourceOffset,
                 .preferLaterEmptyAtOffset = preferLaterEmptyAtOffset,
                 .structureEdit = structureEdit,
                 .snapshotUndo = structureEdit,
                 .layoutImmediately = true});
  // A structure edit (e.g. committing "```"/"$$" with Enter) can leave the caret inside a newly
  // created literal block. Activate that block's inline editor so the caret paints as an editing
  // caret and subsequent keystrokes route into it rather than being rejected. syncLiteralEditMode
  // is a no-op when the caret is on a plain text block, so this is safe for ordinary edits.
  if (outcome.cursor.isValid()) {
    PerfTimer syncLiteralTimer("input.applyLocalEdit.syncLiteral");
    syncLiteralEditMode(outcome.cursor.blockId);
  }
}

} // namespace muffin
