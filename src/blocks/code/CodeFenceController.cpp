#include "blocks/code/CodeFenceController.h"

#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"

#include <QSettings>

namespace muffin {

namespace {

// markdown/codeIndent: combo INDEX {0,1,2} -> space count {2,4,8}. Default index 1 = 4 spaces.
int codeIndentUnit() {
  static const int units[] = {2, 4, 8};
  const int idx = qBound(0, QSettings().value(QStringLiteral("markdown/codeIndent"), 1).toInt(), 2);
  return units[idx];
}

LiteralBlockSpec codeSpec() {
  return LiteralBlockSpec{
      BlockType::CodeFence,
      HitTestResult::Zone::Code,
      QStringLiteral("No code fence is active."),
      QStringLiteral("Edit Code Fence"),
      QStringLiteral("Backspace Code Fence"),
      QStringLiteral("Delete Code Fence Text"),
      QStringLiteral("Delete Code Fence Selection"),
      QStringLiteral("Set Code Fence Content"),
      QStringLiteral("\t")};
}

}  // namespace

CodeFenceController::CodeFenceController(QObject* parent) : QObject(parent), literal_(codeSpec()) {
  literal_.setRejectedHandler([this](QString reason) { emit codeCommandRejected(std::move(reason)); });
}

void CodeFenceController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
  literal_.setContext(ctx);
}

NodeId CodeFenceController::currentCodeFenceId() const {
  return literal_.currentBlockId();
}

bool CodeFenceController::isEditing() const {
  return literal_.isEditing();
}

bool CodeFenceController::enterEditMode() {
  return literal_.enterEditMode();
}

bool CodeFenceController::exitEditMode() {
  return literal_.exitEditMode();
}

bool CodeFenceController::insertText(QString text) {
  return literal_.insertText(std::move(text));
}

bool CodeFenceController::dedentSelection() {
  // Shift+Tab path (and the "Dedent Selection" menu): dedent the selected lines, or — when there
  // is no selection — the line holding the caret. Returns false only outside a code fence.
  return adjustIndent(false, IndentScope::SelectionOrLine);
}

bool CodeFenceController::deleteBackward() {
  return literal_.deleteBackward();
}

bool CodeFenceController::deleteForward() {
  return literal_.deleteForward();
}

bool CodeFenceController::deleteSelection() {
  return literal_.deleteSelection();
}

bool CodeFenceController::setLanguage(QString language) {
  const MarkdownNode* activeCode = literal_.currentBlock();
  return activeCode ? setLanguageForCodeFence(activeCode->id(), std::move(language)) : false;
}

bool CodeFenceController::setLanguageFor(NodeId codeId, QString language) {
  return setLanguageForCodeFence(codeId, std::move(language));
}

bool CodeFenceController::setContent(QString content) {
  return literal_.setContent(std::move(content));
}

QString CodeFenceController::currentContent() const {
  const MarkdownNode* active = literal_.currentBlock();
  return active ? active->literal() : QString();
}

bool CodeFenceController::indentSelection() {
  // Tab-with-selection and the "Indent Selection" menu: indent the selected lines. Returns false
  // when there is no selection so the caller inserts a single indent unit at the caret instead.
  return adjustIndent(true, IndentScope::Selection);
}

bool CodeFenceController::indentWholeBlock() {
  return adjustIndent(true, IndentScope::WholeBlock);
}

bool CodeFenceController::dedentWholeBlock() {
  return adjustIndent(false, IndentScope::WholeBlock);
}

bool CodeFenceController::adjustIndent(bool indent, IndentScope scope) {
  const QString label = indent ? QStringLiteral("Indent Code") : QStringLiteral("Dedent Code");
  return literal_.mutateCurrentBlock(label, EditTransaction::Kind::ReplaceDocumentText,
      [this, indent, scope](MarkdownNode& node, qsizetype& offset) {
        QString value = node.literal();
        const qsizetype unit = codeIndentUnit();
        const qsizetype caret = qBound<qsizetype>(0, offset, value.size());

        // Resolve the first in-scope line start and the exclusive bound for gathering further lines,
        // per scope. Selection requires a non-empty selection (returns false so callers fall back to
        // inserting a tab); SelectionOrLine falls back to the caret's line; WholeBlock spans all.
        qsizetype firstStart = 0;
        qsizetype gatherLimit = value.size();
        if (scope != IndentScope::WholeBlock) {
          qsizetype selStart = 0;
          qsizetype selEnd = value.size();
          if (literal_.currentSelectionRange(selStart, selEnd)) {
            firstStart = selStart;
            while (firstStart > 0 && value.at(firstStart - 1) != QLatin1Char('\n')) {
              --firstStart;
            }
            // A line counts while its start is strictly before selEnd, so a selection ending exactly
            // at a line's first column leaves that line untouched (typical editor rule).
            gatherLimit = selEnd;
          } else {
            if (scope == IndentScope::Selection) {
              return false;
            }
            // SelectionOrLine with no selection: transform only the line holding the caret.
            firstStart = caret;
            while (firstStart > 0 && value.at(firstStart - 1) != QLatin1Char('\n')) {
              --firstStart;
            }
            gatherLimit = firstStart;  // no further line can start at or below firstStart
          }
        }

        QVector<qsizetype> lineStarts;
        lineStarts.append(firstStart);
        qsizetype scan = firstStart;
        while (true) {
          const int newline = value.indexOf(QLatin1Char('\n'), scan);
          if (newline < 0) {
            break;
          }
          scan = newline + 1;
          if (scan >= gatherLimit || scan >= value.size()) {
            break;  // past the scope, or a trailing newline with no further content line
          }
          lineStarts.append(scan);
        }

        // Transform last-line-first so earlier offsets stay valid, tracking how far the caret
        // shifts so it lands on the same column of its line.
        qsizetype caretShift = 0;
        for (int i = lineStarts.size() - 1; i >= 0; --i) {
          const qsizetype start = lineStarts[i];
          if (indent) {
            value.insert(start, QString(unit, QLatin1Char(' ')));
            if (caret >= start) {
              caretShift += unit;
            }
          } else {
            qsizetype spaces = 0;
            while (start + spaces < value.size() && value.at(start + spaces) == QLatin1Char(' ') && spaces < unit) {
              ++spaces;
            }
            if (spaces > 0) {
              value.remove(start, spaces);
              if (caret > start) {
                caretShift -= qMin(spaces, caret - start);
              }
            }
          }
        }

        node.setLiteral(value);
        offset = qBound<qsizetype>(0, caret + caretShift, value.size());
        return true;
      });
}

QString CodeFenceController::tabText() const {
  // Honor markdown/codeIndent (spaces) rather than the spec's fallback "\t".
  return QString(codeIndentUnit(), QLatin1Char(' '));
}

bool CodeFenceController::hasPendingTrailingNewline() const {
  return literal_.hasPendingTrailingNewline();
}

void CodeFenceController::clearPendingTrailingNewline() {
  literal_.clearPendingTrailingNewline();
}

bool CodeFenceController::setLanguageForCodeFence(NodeId requestedCodeId, QString language) {
  if (!ctx_.hasSession() || !ctx_.hasSelection()) {
    return false;
  }

  MarkdownNode* activeCode = literal_.blockById(requestedCodeId);
  if (!activeCode) {
    return false;
  }
  language = language.trimmed();
  const QString beforeLanguage = activeCode->codeLanguage();
  if (beforeLanguage == language) {
    return true;
  }

  const bool wasEditing = literal_.isEditing();
  NodeId codeId = activeCode->id();
  const NodeId originalCodeId = codeId;
  const int codeIndex = literal_.blockIndexFor(codeId);
  const CursorPosition beforeCursor = ctx_.selection->hasCursor() ? ctx_.selection->cursorPosition() : literal_.cursorFor(codeId, 0);
  const int beforeCursorCodeIndex = literal_.blockIndexFor(beforeCursor.blockId);
  const bool cursorWasInTarget = beforeCursor.blockId == codeId || (wasEditing && beforeCursor.blockId == literal_.currentBlockId());
  auto afterNode = activeCode->clone(CloneMode::PreserveIds);
  afterNode->setCodeLanguage(language);

  if (!ctx_.session->applyNodeSnapshot(codeId, BlockType::CodeFence, codeIndex, *afterNode, true)) {
    return false;
  }
  if (MarkdownNode* reparsed = literal_.blockByIndex(codeIndex)) {
    codeId = reparsed->id();
    if (wasEditing) {
      literal_.setEditingBlock(codeId, codeIndex);
    }
  }

  CursorPosition nextCursor;
  if (cursorWasInTarget) {
    nextCursor = literal_.cursorFor(codeId, beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedCursorCode = literal_.blockByIndex(beforeCursorCodeIndex)) {
    nextCursor = literal_.cursorFor(reparsedCursorCode->id(), beforeCursor.text.textOffset);
  } else if (MarkdownNode* reparsedCursorNode = ctx_.session->document().node(beforeCursor.blockId)) {
    nextCursor = beforeCursor;
    nextCursor.blockId = reparsedCursorNode->id();
    nextCursor.text.nodeId = reparsedCursorNode->id();
  }
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack && nextCursor.isValid()) {
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Set Code Fence Language"),
        SetNodeAttrCommand{
            originalCodeId,
            BlockType::CodeFence,
            codeIndex,
            NodeAttribute::CodeLanguage,
            beforeLanguage,
            language,
            beforeCursor,
            nextCursor,
            QVector<NodeId>{originalCodeId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(codeId);
  }
  return true;
}

}  // namespace muffin
