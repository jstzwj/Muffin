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
  return literal_.mutateCurrentBlock(QStringLiteral("Dedent Code Fence Selection"),
                                     EditTransaction::Kind::DeleteText,
                                     [this](MarkdownNode& node, qsizetype& offset) {
                                       qsizetype selStart = 0;
                                       qsizetype selEnd = 0;
                                       if (!literal_.currentSelectionRange(selStart, selEnd)) {
                                         return false;  // collapsed caret: caller inserts a tab instead
                                       }
                                       QString value = node.literal();
                                       selStart = qBound<qsizetype>(0, selStart, value.size());
                                       selEnd = qBound<qsizetype>(selStart, selEnd, value.size());
                                       if (selStart == selEnd) {
                                         return false;
                                       }
                                       const qsizetype unit = codeIndentUnit();
                                       // Gather the start offset of each line the selection covers
                                       // (a line counts if its start is strictly before selEnd, so a
                                       // selection ending exactly at a line's first column leaves
                                       // that line untouched — matching typical code editors).
                                       QVector<qsizetype> lineStarts;
                                       qsizetype lineStart = selStart;
                                       while (lineStart > 0 && value.at(lineStart - 1) != QLatin1Char('\n')) {
                                         --lineStart;
                                       }
                                       lineStarts.append(lineStart);
                                       while (true) {
                                         const int newline = value.indexOf(QLatin1Char('\n'), lineStart);
                                         if (newline < 0) {
                                           break;
                                         }
                                         lineStart = newline + 1;
                                         if (lineStart >= selEnd) {
                                           break;
                                         }
                                         lineStarts.append(lineStart);
                                       }
                                       // Strip leading spaces last-line-first so earlier offsets stay valid.
                                       qsizetype firstLineRemoved = 0;
                                       for (int i = lineStarts.size() - 1; i >= 0; --i) {
                                         const qsizetype start = lineStarts[i];
                                         qsizetype spaces = 0;
                                         while (start + spaces < value.size() &&
                                                value.at(start + spaces) == QLatin1Char(' ') &&
                                                spaces < unit) {
                                           ++spaces;
                                         }
                                         if (spaces > 0) {
                                           value.remove(start, spaces);
                                         }
                                         if (i == 0) {
                                           firstLineRemoved = spaces;
                                         }
                                       }
                                       node.setLiteral(value);
                                       offset = qBound<qsizetype>(0, selStart - firstLineRemoved, value.size());
                                       return true;
                                     });
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
  return adjustIndent(true, false);
}

bool CodeFenceController::indentWholeBlock() {
  return adjustIndent(true, true);
}

bool CodeFenceController::dedentWholeBlock() {
  return adjustIndent(false, true);
}

bool CodeFenceController::adjustIndent(bool indent, bool wholeBlock) {
  const QString label = indent
                            ? (wholeBlock ? QStringLiteral("Indent Code Block") : QStringLiteral("Indent Code Selection"))
                            : (wholeBlock ? QStringLiteral("Dedent Code Block") : QStringLiteral("Dedent Code Selection"));
  return literal_.mutateCurrentBlock(label, EditTransaction::Kind::ReplaceDocumentText,
      [this, indent, wholeBlock](MarkdownNode& node, qsizetype& offset) {
        QString value = node.literal();
        const qsizetype unit = codeIndentUnit();

        // Selection scope requires a non-empty selection; whole-block scope spans every line.
        qsizetype selStart = 0;
        qsizetype selEnd = value.size();
        if (!wholeBlock) {
          if (!literal_.currentSelectionRange(selStart, selEnd)) {
            return false;
          }
          selStart = qBound<qsizetype>(0, selStart, value.size());
          selEnd = qBound<qsizetype>(selStart, selEnd, value.size());
          if (selStart == selEnd) {
            return false;
          }
        }

        const qsizetype caret = qBound<qsizetype>(0, offset, value.size());

        // Gather the start offset of each in-scope line. For a selection the first line start is
        // the beginning of the line holding selStart, and a line counts while its start is strictly
        // before selEnd (a selection ending exactly at a line's first column leaves that line
        // untouched), matching typical code editors.
        QVector<qsizetype> lineStarts;
        qsizetype firstStart = 0;
        if (!wholeBlock) {
          firstStart = selStart;
          while (firstStart > 0 && value.at(firstStart - 1) != QLatin1Char('\n')) {
            --firstStart;
          }
        }
        lineStarts.append(firstStart);
        qsizetype scan = firstStart;
        while (true) {
          const int newline = value.indexOf(QLatin1Char('\n'), scan);
          if (newline < 0) {
            break;
          }
          scan = newline + 1;
          if (!wholeBlock && scan >= selEnd) {
            break;
          }
          if (scan >= value.size()) {
            break;  // trailing newline: no further content line to transform
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
