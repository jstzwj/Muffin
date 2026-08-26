#include "editor/InputController.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "document/BlockPredicates.h"
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/NodeNavigation.h"
#include "document/SourceRangeUtil.h"
#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorKeyRouting.h"
#include "editor/EditorView.h"
#include "editor/EmojiCompleter.h"
#include "editor/EmojiProvider.h"
#include "editor/SelectionController.h"
#include "editor/SmartPunctuation.h"
#include "editor/TextBlockCommandBuilder.h"
#include "editor/WordBoundary.h"
#include "edit/UndoStack.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"
#include "blocks/table/TableController.h"
#include "editor/EditorViewGeometry.h"
#include "render/BlockLayout.h"

#include <QEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QInputMethodEvent>
#include <QPoint>
#include <QScrollBar>
#include <QSettings>
#include <QTextBoundaryFinder>

#include <cmath>
#include <limits>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(inputPerf, "muffin.perf", QtWarningMsg)

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, inputPerf()) {}
};

QString plainTextForNode(const MarkdownNode& node) {
  QString text;
  switch (node.type()) {
    case BlockType::List:
    case BlockType::ListItem:
      for (const auto& child : node.children()) {
        const QString childText = plainTextForNode(*child);
        if (childText.isEmpty()) {
          continue;
        }
        if (!text.isEmpty()) {
          text += QLatin1Char('\n');
        }
        text += childText;
      }
      return text;
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::TableCell:
      return InlineProjection::plainTextForInlines(node.inlines());
    default:
      return node.literal();
  }
}

// Snap a UTF-16 offset to the nearest grapheme-cluster boundary in the step direction, so the caret
// never lands between a surrogate pair or inside a base+combining-mark cluster (emoji, CJK ext B,
// accented input). Qt's Grapheme finder handles those common cases; for pure-ASCII text every offset
// is already a boundary, so this is a no-op there (existing caret tests are unaffected).
qsizetype snapOffsetToGrapheme(const QString& text, qsizetype offset, int direction) {
  if (offset <= 0 || offset >= text.size()) {
    return offset;
  }
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
  finder.setPosition(static_cast<int>(offset));
  if (finder.isAtBoundary()) {
    return offset;
  }
  const int snapped = direction > 0 ? finder.toNextBoundary() : finder.toPreviousBoundary();
  return snapped >= 0 ? static_cast<qsizetype>(snapped) : offset;
}

bool isDeadKey(int key) {
  return (key >= Qt::Key_Dead_Grave && key <= Qt::Key_Dead_Currency) ||
         (key >= Qt::Key_Dead_a && key <= Qt::Key_Dead_Greek) ||
         (key >= Qt::Key_Dead_Lowline && key <= Qt::Key_Dead_Longsolidusoverlay);
}

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

InputController::InputController(QObject* parent) : QObject(parent) {}

void InputController::setContext(const EditorContext& ctx) {
  EditorView* const oldView = ctx_.view;
  ctx_ = ctx;
  if (oldView != ctx_.view) {
    if (oldView) {
      oldView->removeEventFilter(this);
      oldView->viewport()->removeEventFilter(this);
    }
    if (ctx_.view) {
      ctx_.view->installEventFilter(this);
      ctx_.view->viewport()->installEventFilter(this);
    }
  }
}

void InputController::setTableController(TableController* tableController) {
  tableController_ = tableController;
}

void InputController::setCodeFenceController(CodeFenceController* codeFenceController) {
  codeFenceController_ = codeFenceController;
}

bool InputController::eventFilter(QObject* watched, QEvent* event) {
  if (watched == ctx_.view || (ctx_.view && watched == ctx_.view->viewport())) {
    if (event->type() == QEvent::ShortcutOverride) {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      if (keyEvent->key() == Qt::Key_A && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
        keyEvent->accept();
        return true;
      }
      if (keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
          editor_keys::isEditorOwnedCtrlKey(keyEvent->key(), keyEvent->modifiers().testFlag(Qt::ShiftModifier))) {
        keyEvent->accept();
        return true;
      }
      if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
        keyEvent->accept();
      }
      return false;
    }
    if (event->type() == QEvent::KeyPress) {
      // While an async open parse is in flight (parseBusy) document_ is the stale pre-open text:
      // swallow the keystroke entirely so it can't drive a local edit, which would land on that
      // stale text and supersede (discard) the worker's parsed result for the file being opened.
      // The view shows a loading hint meanwhile; editing resumes once parseBusy(false) fires.
      if (ctx_.session && ctx_.session->isAsyncParseInProgress()) {
        return true;  // swallow — no edit, no fallback reparse
      }
      return handleKeyPress(static_cast<QKeyEvent*>(event));
    }
    if (event->type() == QEvent::MouseButtonPress) {
      // A click that moves the caret invalidates the active ":shortcode" trigger:
      // maybeUpdateEmojiPopup only runs on typing paths, so without this the popup
      // stayed up with a stale emojiColonStart_ and accepting an entry replaced the
      // whole span between the shortcode's colon and the new caret position.
      hideEmojiPopup();
      return false;  // let the click place the caret normally
    }
  }
  return QObject::eventFilter(watched, event);
}

bool InputController::insertText(QString text) {
  clearVerticalNavigationX();
  if (ctx_.session && ctx_.session->isAsyncParseInProgress()) {
    return false;  // document_ holds stale pre-open text during async parse — drop input (same gate as KeyPress)
  }
  if (ctx_.hasSession() && ctx_.session->markdownText().isEmpty()) {
    return insertIntoEmptyDocument(std::move(text));
  }
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(std::move(text));
  }
  // A caret resting on a non-text block (a thematic break) has no inline text to type into — insert
  // a fresh paragraph after it carrying the typed text. Routed BEFORE smart-punct / auto-pair so the
  // character lands verbatim (no prose context to convert, and no pair logic to misfire on the rule).
  if (caretRestsOnNonTextBlock()) {
    hideEmojiPopup();
    return insertBlockAfterCurrentBlock(std::move(text));
  }
  // Smart punctuation (markdown/*): turn quotes into curly/smart forms and collapse "--"/"---" into
  // en/em dashes. Only in prose (the literal path above already returned for code/math blocks) and
  // only with a collapsed selection — the selection path is owned by auto-pair/wrap & replace.
  // Quote conversion is a pure text transform; because the smart chars aren't in the pair table,
  // tryAutoPairOrWrap below then declines and the converted text inserts normally.
  const bool smartPunctSkipSelection =
      ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed();
  if (!smartPunctSkipSelection) {
    if (text.size() == 1 && (trySmartDashes(text.at(0)) || trySmartEllipsis(text.at(0)))) {
      maybeUpdateEmojiPopup();
      return true;
    }
    text = maybeConvertSmartPunctuation(std::move(text));
  }
  // Auto-pairing only fires for a single typed character (multi-char IME commits and drag-drop
  // blobs like "![alt](path)" pass straight through) and never inside a table cell, where the
  // table controller owns text entry.
  if (text.size() == 1 && !(tableController_ && tableController_->currentCell().isValid()) &&
      tryAutoPairOrWrap(text.at(0))) {
    maybeUpdateEmojiPopup();
    return true;
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    const bool replaced = replaceSelection(std::move(text), EditTransaction::Kind::InsertText, QStringLiteral("Replace Selection"));
    if (replaced) {
      maybeUpdateEmojiPopup();
    }
    return replaced;
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    hideEmojiPopup();
    return tableController_->insertText(std::move(text));
  }
  if (ctx_.selection && ctx_.selection->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    hideEmojiPopup();
    return insertBlockAfterCurrentBlock(std::move(text));
  }
  if (tryInsertOptionalDefinitionTitle(text)) {
    hideEmojiPopup();
    return true;
  }
  const bool inserted = text.isEmpty() ? false : editParagraph(TextBlockCommandBuilder::Operation::InsertText, std::move(text));
  if (inserted) {
    maybeUpdateEmojiPopup();
  }
  return inserted;
}

bool InputController::insertParagraphBreak() {
  clearVerticalNavigationX();
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(QStringLiteral("\n"));
  }
  // A whole-selected non-text leaf (currently a thematic break) is removed on Enter — Typora-style
  // select-to-delete. Backspace/Forward-Delete already route through deleteSelection →
  // tryRemoveExactWholeBlockSelection; Enter lands here, so handle it. Scoped to non-text leaves so a
  // whole-selected text block keeps its normal Enter behaviour.
  if (ctx_.selection && ctx_.hasSession() && !ctx_.selection->selection().isCollapsed()) {
    MarkdownNode* selNode = ctx_.session->document().node(ctx_.selection->selection().focus.blockId);
    if (selNode != nullptr && isNonTextLeafBlock(*selNode) &&
        tryRemoveExactWholeBlockSelection(EditTransaction::Kind::DeleteText, QStringLiteral("Delete Rule (Enter)"))) {
      return true;
    }
  }
  // A caret resting on a non-text block (a thematic break) has no inline text to split, so Enter
  // inserts a fresh empty paragraph after it. Virtual empty paragraphs are allowed in the rule's gap
  // (the byteEnd clamp + endLine gap logic keep their count stable across the slice re-parse), so
  // this drops the caret into a real, editable empty paragraph instead of being a silent no-op.
  if (caretRestsOnNonTextBlock()) {
    return insertBlockAfterCurrentBlock();
  }
  if (ctx_.selection && ctx_.selection->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    return insertBlockAfterCurrentBlock();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Enter);
}

bool InputController::insertBlockAfterCurrentBlock(QString text) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node) {
    return false;
  }
  const SourceRange range = node->sourceRange();
  if (range.byteEnd < range.byteStart || range.byteEnd > ctx_.session->markdownText().size()) {
    return false;
  }

  // Literal blocks (front matter / code fence / math / html) already end on a fence boundary, so a
  // single newline starts a fresh block. Content blocks (paragraph, heading, list, thematic break…)
  // need a blank line (\n\n) to form a separate paragraph instead of a soft line break.
  const bool isLiteralBlock = node->type() == BlockType::FrontMatter || node->type() == BlockType::CodeFence ||
                              node->type() == BlockType::MathBlock || node->type() == BlockType::HtmlBlock;

  const PieceTable& markdown = ctx_.session->markdownText();
  qsizetype insertOffset = range.byteEnd;
  if (insertOffset < markdown.size() && markdown.at(insertOffset) == QLatin1Char('\r')) {
    ++insertOffset;
  }
  if (insertOffset < markdown.size() && markdown.at(insertOffset) == QLatin1Char('\n')) {
    ++insertOffset;
  }

  // When typing into the afterBlock caret of a block that has a FOLLOWING top-level block, insert
  // the new paragraph just before that following block (text + separator). The node's own separator
  // already precedes it, and the trailing separator keeps the typed text from merging into the next
  // block — without this, "type after a mid-document rule" produced "rule\n\nTbeta". Empty Enter and
  // the trailing case (no following block) keep the original insert-at-byteEnd behaviour below.
  MarkdownNode* following = nullptr;
  if (!text.isEmpty() && node->parent()) {
    // "the block after this one" is a TOP-LEVEL sibling. A nested caret (list item / quote
    // paragraph) would otherwise find its sibling INSIDE the same container and insert there,
    // merging into the wrong block. Climb to the Document child, then look for its following
    // top-level sibling.
    MarkdownNode* scope = node;
    while (scope->parent() != nullptr && scope->parent()->type() != BlockType::Document) {
      scope = scope->parent();
    }
    if (scope->parent() != nullptr) {
      const auto& siblings = scope->parent()->children();
      for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings.at(i).get() == scope) {
          if (i + 1 < siblings.size()) {
            following = siblings.at(i + 1).get();
          }
          break;
        }
      }
    }
  }

  qsizetype caretOffset;
  QString insertedText;
  // Only a REAL following block (one with content, not a zero-width virtual empty paragraph)
  // forces the insert-before-it path. A trailing VEP is just the caret target, not a block to stay
  // separate from — treating it as `following` merged the typed text into this node's last line.
  if (following && following->sourceRange().byteEnd > following->sourceRange().byteStart) {
    insertOffset = following->sourceRange().byteStart;
    insertedText = text + (isLiteralBlock ? QStringLiteral("\n") : QStringLiteral("\n\n"));
    caretOffset = insertOffset + text.size();  // inside the new paragraph, before its trailing separator
  } else {
    if (text.isEmpty()) {
      insertedText = QStringLiteral("\n\n");
      // Materialize the layout-only BlockAfter caret. For ordinary text/container blocks, the new
      // trailing empty paragraph is represented by the blank line AFTER the inserted separator, so the
      // caret must resolve at insertOffset + size; leaving it at insertOffset hits the previous block's
      // inclusive end boundary (e.g. a trailing heading) and visually snaps back into that block.
      //
      // Non-text leaves are different: a thematic break's editable target lives at the START of the
      // blank-line run below the rule. Pointing past the "\n\n" there lands in a gap no block covers,
      // so keep the established rule-specific offset and let preferLaterEmptyAtOffset resolve that VEP.
      caretOffset = (!isLiteralBlock && isNonTextLeafBlock(*node)) ? insertOffset : (insertOffset + insertedText.size());
    } else {
      insertedText = isLiteralBlock ? QStringLiteral("\n%1").arg(text) : QStringLiteral("\n\n%1").arg(text);
      caretOffset = insertOffset + insertedText.size();
    }
  }
  applyLocalEdit(
      EditTransaction::Kind::SplitParagraph,
      text.isEmpty() ? QStringLiteral("Insert Paragraph After") : QStringLiteral("Insert Text After Block"),
      insertOffset,
      0,
      insertedText,
      CursorPosition(),
      caretOffset,
      QVector<LocalEditNodeHint>{LocalEditNodeHint{node->id(), range.byteStart, node->type()}},
      true,
      false);  // structureEdit=false: this is a plain "\n\n…" text insert, so a TextDeltaCommand
               // undo suffices. structureEdit=true forced the O(document) snapshot-undo path
               // (full-doc toString ×3 + collectPendingMarkerOffsets ×2 ≈ 7s on an 18MB doc) on
               // every "type after a block" keystroke.
  return true;
}

bool InputController::isNonTextLeafBlock(MarkdownNode& node) const {
  // No inline-editable text anywhere in the subtree AND not a literal block (which edits through its
  // own controller). Currently only a thematic break satisfies this; expressed structurally so a
  // future editor-less non-text leaf is covered without touching the call sites. NOTE: this is
  // narrower than the cursorForSourceOffset check (!firstEditableDescendant) — that one intentionally
  // ALSO covers literal-block gap offsets, which resolve to a block-after caret; here we must EXCLUDE
  // literal blocks because typing/Enter on them is owned by their literal editor or insert-after path.
  return !isLiteralBlockType(node.type()) && contextResolver().firstEditableDescendant(node) == nullptr;
}

bool InputController::caretRestsOnNonTextBlock() const {
  if (!ctx_.hasSession() || !ctx_.selection || !ctx_.selection->hasCursor()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  return node != nullptr && isNonTextLeafBlock(*node);
}

bool InputController::deleteBackward() {
  clearVerticalNavigationX();
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return deleteBackwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  // The caret can sit on a thematic break itself — either landed directly on the rule (arrow-key
  // navigation does not skip non-editable blocks) or in its virtual trailing area (afterBlock).
  // The rule carries no editable text, so editParagraph rejects it and backspace would be a silent
  // no-op (or, for the trailing caret, merely collapse the caret). Eat the divider instead.
  if (tryRemoveThematicBreak(/*forward=*/false)) {
    return true;
  }
  // The virtual trailing paragraph carries no text. A backspace from it pulls the caret up to
  // the end of the last block's content; the next backspace deletes from there.
  // Without this, backspace on the trailing area is a silent no-op — and for a document whose
  // last block is a list it even routes to "unsupported edit", since the list node is not an
  // editable text block.
  if (ctx_.selection && ctx_.selection->hasCursor() && ctx_.selection->cursorPosition().afterBlock) {
    return collapseTrailingCaretToEndOfLastBlock();
  }
  if (tryRemoveEmptyDefinitionBlock(EditTransaction::Kind::DeleteText, QStringLiteral("Backspace Empty Definition"))) {
    return true;
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->deleteBackward();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Backspace);
}

bool InputController::deleteForward() {
  clearVerticalNavigationX();
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return deleteForwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  // Symmetric to the backspace case: Delete from a caret resting on a thematic break (directly or
  // in its trailing area) removes the divider.
  if (tryRemoveThematicBreak(/*forward=*/true)) {
    return true;
  }
  if (tryRemoveEmptyDefinitionBlock(EditTransaction::Kind::DeleteText, QStringLiteral("Delete Empty Definition"))) {
    return true;
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->deleteForward();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Delete);
}

bool InputController::indentListItem() {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildIndentListItem(context));
}

bool InputController::outdentListItem() {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }
  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildOutdentListItem(context));
}

bool InputController::deleteSelection() {
  if (hasActiveLiteralEditor()) {
    return deleteSelectionInActiveLiteral();
  }
  return replaceSelection(QString(), EditTransaction::Kind::DeleteText, QStringLiteral("Delete Selection"));
}

bool InputController::hasEditableSelection() const {
  qsizetype start = 0;
  qsizetype end = 0;
  return selectionSourceRange(start, end) || blockSelectionSourceRange(start, end);
}

bool InputController::replaceSelection(QString text, EditTransaction::Kind kind, QString label) {
  if (text.isEmpty() && kind == EditTransaction::Kind::DeleteText && tryRemoveExactWholeBlockSelection(kind, label)) {
    return true;
  }

  const qsizetype insertedLength = text.size();
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  qsizetype start = 0;
  qsizetype end = 0;
  if (resolver.selectionContext(context, start, end)) {
    const qsizetype sourceStart = context.contentRange.byteStart + start;
    QVector<LocalEditNodeHint> nodeHints;
    if (context.node) {
      nodeHints.push_back(LocalEditNodeHint{
          context.node->id(),
          context.blockRange.byteStart >= 0 ? context.blockRange.byteStart : context.contentRange.byteStart,
          context.node->type()});
    }
    applyLocalEdit(
        kind,
        label,
        sourceStart,
        end - start,
        std::move(text),
        CursorPosition(),
        sourceStart + insertedLength,
        std::move(nodeHints));
    return true;
  }

  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (!selectionSourceRange(sourceStart, sourceEnd)) {
    emit unsupportedEditRequested(QStringLiteral("Only editable text selection is supported in this M4 slice."));
    return false;
  }

  QVector<LocalEditNodeHint> nodeHints;
  const SelectionRange range = ctx_.selection->selection();
  if (range.anchor.blockId.isValid()) {
    nodeHints.push_back(LocalEditNodeHint{range.anchor.blockId, sourceStart, BlockType::Unknown});
  }
  if (range.focus.blockId.isValid() && range.focus.blockId != range.anchor.blockId) {
    nodeHints.push_back(LocalEditNodeHint{range.focus.blockId, sourceEnd, BlockType::Unknown});
  }
  applyLocalEdit(
      kind,
      label,
      sourceStart,
      sourceEnd - sourceStart,
      std::move(text),
      CursorPosition(),
      sourceStart + insertedLength,
      std::move(nodeHints));
  return true;
}

int InputController::topLevelBlockIndex(const MarkdownNode& node) const {
  const auto& blocks = ctx_.session->document().root().children();
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == &node) { return i; }
  }
  return -1;
}




bool InputController::handleInputMethod(QInputMethodEvent* event) {
  if (!event || event->commitString().isEmpty()) {
    return false;
  }
  return insertText(event->commitString());
}

bool InputController::hasActiveLiteralEditor() const {
  for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
    if (it.value() && it.value()->isEditing()) {
      return true;
    }
  }
  return codeFenceController_ && codeFenceController_->isEditing();
}

LiteralBlockController* InputController::activeLiteralEditor() const {
  for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
    if (it.value() && it.value()->isEditing()) {
      return it.value();
    }
  }
  return nullptr;
}

void InputController::syncLiteralEditMode(NodeId newBlockId) {
  if (!ctx_.hasSession() || !ctx_.hasSelection()) {
    return;
  }
  MarkdownNode* node = ctx_.session->document().node(newBlockId);
  if (!node) {
    return;
  }

  const BlockType type = node->type();
  const bool isLiteral = isLiteralBlockType(type);

  const auto exitAllLiteralEditors = [this]() {
    for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
      if (it.value()) {
        it.value()->exitEditMode();
      }
    }
    if (codeFenceController_) {
      codeFenceController_->exitEditMode();
    }
  };

  if (!isLiteral) {
    if (hasActiveLiteralEditor()) {
      exitAllLiteralEditors();
    }
    return;
  }

  bool alreadyEditingTarget = false;
  if (type == BlockType::CodeFence) {
    alreadyEditingTarget = codeFenceController_ && codeFenceController_->isEditing() && codeFenceController_->currentCodeFenceId() == newBlockId;
  } else if (LiteralBlockController* ctrl = ctx_.literalEditors.value(static_cast<int>(type))) {
    alreadyEditingTarget = ctrl->isEditing() && ctrl->currentBlockId() == newBlockId;
  }
  if (alreadyEditingTarget) {
    return;
  }

  const CursorPosition savedCursor = ctx_.selection->cursorPosition();
  exitAllLiteralEditors();

  bool entered = false;
  if (type == BlockType::CodeFence) {
    entered = codeFenceController_ && codeFenceController_->enterEditMode();
  } else if (LiteralBlockController* ctrl = ctx_.literalEditors.value(static_cast<int>(type))) {
    entered = ctrl->enterEditMode();
  }
  if (entered) {
    ctx_.selection->setCursorPosition(savedCursor);
  }
}

void InputController::reconcileLiteralEditorForCursor() {
  // Drop a literal editor that no longer matches the caret. Structure edits and undo can move the
  // caret off the block being edited (e.g. committing "```" then undoing restores the paragraph)
  // without going through syncLiteralEditMode, leaving a stale editor active. Without this, the
  // next keystroke would route into a now-absent block and silently no-op. Entering a new editor
  // is intentionally NOT done here — that is the caller's responsibility (syncLiteralEditMode).
  if (!hasActiveLiteralEditor() || !ctx_.hasSession() || !ctx_.hasCursor()) {
    return;
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || !isLiteralBlockType(node->type())) {
    exitActiveLiteralEditor();
  }
}

bool InputController::insertTextIntoActiveLiteral(QString text) {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->insertText(std::move(text));
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->insertText(std::move(text));
  }
  return false;
}

bool InputController::tryDedentActiveCodeFence() {
  if (!codeFenceController_ || !codeFenceController_->isEditing()) {
    return false;
  }
  return codeFenceController_->dedentSelection();
}

bool InputController::tryInsertOptionalDefinitionTitle(QString text) {
  if (text.isEmpty() || !ctx_.hasSession() || !ctx_.hasCursor() || !ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const CursorPosition cursor = ctx_.selection->cursorPosition();
  if (ctx_.selection->currentHit().definitionField != HitTestResult::DefinitionField::Title) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(cursor.blockId);
  if (!node || node->type() != BlockType::LinkDefinition) {
    return false;
  }

  const DefinitionBlock definition = node->definition();
  if (!definition.title.isEmpty() || !definition.titleRange.isValid() ||
      cursor.text.sourceOffset != definition.titleRange.start) {
    return false;
  }

  const QString inserted = definition.titleQuoted ? text : QStringLiteral("  \"%1\"").arg(text);
  const qsizetype titleSourceStart = definition.titleQuoted ? definition.titleRange.start : definition.titleRange.start + 3;
  CursorPosition preferredCursor;
  preferredCursor.blockId = node->id();
  preferredCursor.text.nodeId = node->id();
  preferredCursor.text.sourceOffset = titleSourceStart + text.size();
  preferredCursor.text.textOffset = preferredCursor.text.sourceOffset - definition.markerRange.start;
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Link Definition Title"),
      definition.titleRange.start,
      0,
      inserted,
      preferredCursor,
      preferredCursor.text.sourceOffset,
      QVector<LocalEditNodeHint>{LocalEditNodeHint{node->id(), node->sourceRange().byteStart, node->type()}});
  return true;
}





bool InputController::deleteBackwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Backspace Empty Block"))) {
    return true;
  }
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteBackward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteBackward();
  }
  return false;
}

bool InputController::deleteForwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Delete Empty Block"))) {
    return true;
  }
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteForward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteForward();
  }
  return false;
}

bool InputController::deleteSelectionInActiveLiteral() {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteSelection();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteSelection();
  }
  return false;
}

bool InputController::exitActiveLiteralEditor() {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->exitEditMode();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->exitEditMode();
  }
  return false;
}

QString InputController::activeLiteralTabText() const {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->tabText();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->tabText();
  }
  return QString();
}

bool InputController::handleKeyPress(QKeyEvent* event) {
  PerfTimer perf("input.keypress");
  if (event && event->key() == Qt::Key_A && event->modifiers().testFlag(Qt::ControlModifier) &&
      !event->modifiers().testFlag(Qt::AltModifier)) {
    emit selectAllRequested();
    return true;
  }

  // Ctrl-combos the editor owns (word/paragraph/page navigation, word delete) are dispatched
  // below; every other Ctrl combo keeps falling through so menu shortcuts (Ctrl+S, Ctrl+F, ...)
  // and widget actions (Ctrl+Shift+Backspace = delete table row) win. Alt is never ours.
  const bool ctrlOwned = event->modifiers().testFlag(Qt::ControlModifier) &&
      editor_keys::isEditorOwnedCtrlKey(event->key(), event->modifiers().testFlag(Qt::ShiftModifier));
  if (!ctx_.hasSession() || !ctx_.hasSelection() || !ctx_.view ||
      (event->modifiers().testFlag(Qt::ControlModifier) && !ctrlOwned) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  // While the emoji completion popup is open, arrows/Enter/Tab/Esc drive it; any other key hides
  // it and then falls through so the keystroke keeps editing normally.
  if (emojiCompleter_ && emojiCompleter_->isVisible()) {
    switch (event->key()) {
      case Qt::Key_Down:
        emojiCompleter_->moveSelection(1);
        return true;
      case Qt::Key_Up:
        emojiCompleter_->moveSelection(-1);
        return true;
      case Qt::Key_Return:
      case Qt::Key_Enter:
      case Qt::Key_Tab:
        emojiCompleter_->acceptCurrent();
        return true;
      case Qt::Key_Escape:
        hideEmojiPopup();
        return true;
      default:
        hideEmojiPopup();
        break;
    }
  }

  if (!ctx_.selection->hasCursor()) {
    if (!ctx_.session->markdownText().isEmpty()) {
      return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      return insertIntoEmptyDocument(QStringLiteral("\n"));
    }
    return insertIntoEmptyDocument(printableText(event));
  }

  // Phantom trailing line in an indented code block. cmark cannot persist a trailing empty line,
  // so Enter at the end of an indented block is held only in the node's literal as a trailing '\n'
  // (created by LiteralBlockController::insertText). While that phantom is present this block owns
  // the per-key semantics; everything else falls through to normal dispatch:
  //   - printable / Tab: commit — the text lands on the phantom line (becomes real code)
  //   - Backspace:        undo the phantom without touching the source
  //   - Enter:            a second Enter on the empty line leaves the block to a new paragraph
  //   - modifier-only:    leave the phantom intact
  //   - any other key:    discard the phantom first, then proceed normally
  if (codeFenceController_ && codeFenceController_->hasPendingTrailingNewline()) {
    const int key = event->key();
    const auto isModifierKey = [](int k) {
      return k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta ||
             k == Qt::Key_AltGr || k == Qt::Key_CapsLock || k == Qt::Key_NumLock || k == Qt::Key_ScrollLock;
    };
    switch (key) {
      case Qt::Key_Backspace:
        codeFenceController_->clearPendingTrailingNewline();
        return true;
      case Qt::Key_Return:
      case Qt::Key_Enter:
        // Indented code cannot hold a second trailing empty line, and Enter-on-empty is the
        // standard "leave the block" gesture. exitEditMode() clears the phantom as its choke point,
        // so the cursor stays usable for the paragraph insert below.
        codeFenceController_->exitEditMode();
        return insertBlockAfterCurrentBlock();
      case Qt::Key_Tab:
      case Qt::Key_Backtab:
        break;  // commit: the Tab text lands on the phantom line via the switch below
      default:
        if (!printableText(event).isEmpty()) {
          break;  // commit: the character lands on the phantom line via insertText below
        }
        if (isModifierKey(key)) {
          break;  // modifier-only press (e.g. Shift): leave the phantom intact
        }
        codeFenceController_->clearPendingTrailingNewline();
        break;
    }
  }

  switch (event->key()) {
    case Qt::Key_Tab:
      if (hasActiveLiteralEditor()) {
        if (event->modifiers().testFlag(Qt::ShiftModifier) && tryDedentActiveCodeFence()) {
          return true;
        }
        // Tab with a selection in a code fence indents the selected lines (symmetric to
        // Shift+Tab); a bare Tab inserts a single indent unit at the caret.
        if (codeFenceController_ && codeFenceController_->isEditing() && codeFenceController_->indentSelection()) {
          return true;
        }
        return insertTextIntoActiveLiteral(activeLiteralTabText());
      }
      if (tableController_ && tableController_->currentCell().isValid()) {
        // Tab in a cell navigates cells (row-major, wrapping; last cell appends a row) instead of
        // inserting a zero-width space.
        return moveTableCell(event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
      }
      if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        return outdentListItem();
      }
      if (shouldIndentListItemFromKeyboard()) {
        return indentListItem();
      }
      return insertText(QStringLiteral("\u200b"));
    case Qt::Key_Backtab:
      if (hasActiveLiteralEditor()) {
        if (tryDedentActiveCodeFence()) {
          return true;
        }
        return insertTextIntoActiveLiteral(activeLiteralTabText());
      }
      if (tableController_ && tableController_->currentCell().isValid()) {
        return moveTableCell(-1);
      }
      return outdentListItem();
    case Qt::Key_Backspace:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return deleteWordBackward();
      }
      return deleteBackward();
    case Qt::Key_Delete:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return deleteWordForward();
      }
      return deleteForward();
    case Qt::Key_Left:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return moveCursorWord(-1, event->modifiers().testFlag(Qt::ShiftModifier));
      }
      return moveCursorHorizontal(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Right:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return moveCursorWord(1, event->modifiers().testFlag(Qt::ShiftModifier));
      }
      return moveCursorHorizontal(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Up:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return moveBlockVertical(-1, event->modifiers().testFlag(Qt::ShiftModifier));
      }
      return moveCursorVertical(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Down:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        return moveBlockVertical(1, event->modifiers().testFlag(Qt::ShiftModifier));
      }
      return moveCursorVertical(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_PageUp:
      return moveCursorPage(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_PageDown:
      return moveCursorPage(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Home:
      return moveJump(event->modifiers().testFlag(Qt::ControlModifier) ? JumpTarget::DocumentStart : JumpTarget::LineStart,
                      event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_End:
      return moveJump(event->modifiers().testFlag(Qt::ControlModifier) ? JumpTarget::DocumentEnd : JumpTarget::LineEnd,
                      event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Escape:
      return exitActiveLiteralEditor();
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return insertParagraphBreak();
    default: {
      const QString text = printableText(event);
      return insertText(text);
    }
  }
}

bool InputController::insertIntoEmptyDocument(QString text) {
  if (!ctx_.hasSession() || !ctx_.session->markdownText().isEmpty() || text.isEmpty()) {
    return false;
  }
  const qsizetype insertedLength = text.size();
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Text"),
      0,
      0,
      std::move(text),
      CursorPosition(),
      insertedLength,
      {},
      false,
      true);
  return true;
}

bool InputController::shouldIndentListItemFromKeyboard() const {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem || !ctx_.selection || !ctx_.selection->hasCursor()) {
    return false;
  }

  const MarkdownNode* previous = context.node->previousSibling();
  if (!previous || previous->type() != BlockType::ListItem) {
    return false;
  }

  const CursorPosition cursor = ctx_.selection->cursorPosition();
  if (cursor.text.inMeta) {
    return true;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver.listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  const qsizetype sourceOffset = cursor.text.sourceOffset >= 0 ? cursor.text.sourceOffset : context.cursorSourceOffset;
  if (sourceOffset >= 0) {
    return sourceOffset <= contentStart + 1;
  }
  return context.cursorTextOffset <= 1;
}

CursorPosition InputController::cursorFor(NodeId blockId, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  return cursor;
}

CursorPosition InputController::cursorForNode(MarkdownNode& node, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = node.id();
  cursor.text.nodeId = node.id();
  cursor.text.textOffset = qBound<qsizetype>(0, offset, selectableTextLength(node));
  if (node.type() == BlockType::Table) {
    for (const auto& row : node.children()) {
      for (const auto& cell : row->children()) {
        cursor.text.nodeId = cell->id();
        return cursor;
      }
    }
  }
  return cursor;
}

CursorPosition InputController::cursorForSourceOffset(qsizetype sourceOffset, bool preferLaterEmptyAtOffset) const {
  CursorPosition cursor;
  if (!ctx_.hasSession()) {
    return cursor;
  }

  BlockEditContextResolver resolver = contextResolver();
  MarkdownNode* host = ctx_.session->document().topLevelBlockAtOffset(sourceOffset);
  if (!host) {
    return cursor;
  }

  // topLevelBlockAtOffset returns the earliest containing block at a shared boundary. Preserve
  // preferLaterEmptyAtOffset by considering only the immediately following top-level blocks whose
  // inclusive source ranges contain the same offset. In normal text there is exactly one candidate.
  QVector<MarkdownNode*> candidates;
  candidates.push_back(host);
  for (MarkdownNode* next = host->nextSibling(); next && next->sourceRange().containsByte(sourceOffset);
       next = next->nextSibling()) {
    candidates.push_back(next);
  }

  // A source offset that lands inside a literal block (code/math/HTML/front matter) resolves to
  // that block. nodeAtContentSourceOffset only matches inline-editable text, so without this a
  // freshly committed "```"/"$$" block would leave the post-edit caret unresolvable.
  for (MarkdownNode* candidate : candidates) {
    qsizetype literalContentStart = -1;
    if (MarkdownNode* literal = resolver.literalBlockAtSourceOffset(*candidate, sourceOffset, literalContentStart)) {
      cursor.blockId = literal->id();
      cursor.text.nodeId = literal->id();
      cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - literalContentStart, literal->literal().size());
      cursor.text.sourceOffset = sourceOffset;
      return cursor;
    }
  }

  // If the offset is on a top-level block whose subtree hosts NO inline-editable text, resolve it
  // straight to a block-after caret instead of walking the tree. This is intentionally BROADER than
  // isNonTextLeafBlock (caretRestsOnNonTextBlock): it ALSO covers literal blocks (code/math/html/
  // front matter) whose offset sits outside their literal content region — those have no inline text
  // to land on either, so a block-after caret is the right resolution (offsets INSIDE a literal were
  // already returned by literalBlockAtSourceOffset above). Containers (lists/tables/quotes) have
  // editable descendants, so firstEditableDescendant is non-null for them and this is skipped —
  // leaving list-exit / table-caret resolution on its existing path (which relies on
  // cursorForSourceOffset returning invalid there).
  if (!resolver.firstEditableDescendant(*host)) {
    return cursorForBlockAfter(*host, sourceOffset);
  }

  MarkdownNode* node = nullptr;
  for (MarkdownNode* candidate : candidates) {
    if (MarkdownNode* found = paragraphAtSourceOffset(*candidate, sourceOffset, preferLaterEmptyAtOffset)) {
      node = found;
      if (!preferLaterEmptyAtOffset) {
        break;
      }
    }
  }
  if (!node) {
    return cursor;
  }
  return cursorForSourceInNode(*node, sourceOffset);
}

CursorPosition InputController::cursorForSourceInNode(MarkdownNode& node, qsizetype sourceOffset) const {
  BlockEditContext context;
  if (!contextResolver().fill(node, context)) {
    return {};
  }
  const qsizetype localSourceOffset = qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
  qsizetype visibleOffset = -1;
  if (context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
    CursorPosition cursorForVisible = cursorFor(node.id(), visibleOffset);
    cursorForVisible.text.sourceOffset = sourceOffset;
    return cursorForVisible;
  }
  CursorPosition fallbackCursor = cursorFor(node.id(), qBound<qsizetype>(0, localSourceOffset, context.visibleText.size()));
  fallbackCursor.text.sourceOffset = sourceOffset;
  return fallbackCursor;
}

CursorPosition InputController::cursorAfterEdit(CursorPosition preferredCursor, qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset) const {
  if (ctx_.hasSession() && preferredCursor.isValid()) {
    if (preferredCursor.text.sourceOffset >= 0) {
      CursorPosition sourceCursor = cursorForSourceOffset(preferredCursor.text.sourceOffset, preferLaterEmptyAtOffset);
      if (sourceCursor.isValid()) {
        return sourceCursor;
      }
    }
    if (MarkdownNode* node = ctx_.session->document().node(preferredCursor.blockId)) {
      return cursorFor(node->id(), preferredCursor.text.textOffset);
    }
  }
  return cursorForSourceOffset(fallbackSourceOffset, preferLaterEmptyAtOffset);
}

CursorPosition InputController::cursorForBlockAfter(const MarkdownNode& host, qsizetype sourceOffset) const {
  CursorPosition cursor;
  cursor.blockId = host.id();
  cursor.text.nodeId = host.id();
  cursor.text.sourceOffset = sourceOffset;
  cursor.afterBlock = true;
  return cursor;
}

MarkdownNode* InputController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset) const {
  return contextResolver().nodeAtContentSourceOffset(node, sourceOffset, preferLaterEmptyAtOffset);
}

MarkdownNode* InputController::selectableBlockByDirection(NodeId current, int direction) const {
  if (!ctx_.hasSession()) {
    return nullptr;
  }
  const auto& blocks = ctx_.session->document().root().children();
  for (qsizetype i = 0; i < blocks.size(); ++i) {
    if (blocks.at(i)->id() != current) {
      continue;
    }
    const qsizetype next = i + direction;
    if (next >= 0 && next < blocks.size()) {
      return blocks.at(next).get();
    }
    return nullptr;
  }
  return nullptr;
}

MarkdownNode* InputController::neighborBlockInDocumentDirection(const MarkdownNode& node, int direction) const {
  MarkdownNode* mut = const_cast<MarkdownNode*>(&node);
  if (direction > 0) {
    // A list item's nested content (a sublist, or further paragraphs in a loose item) follows the
    // item's own text in document order, so descend into the children after the primary paragraph
    // before climbing to the next sibling. selectableBlockByDirection only walked top-level blocks,
    // so Down from a parent item used to skip its sublist and jump to the next sibling.
    if (node.type() == BlockType::ListItem) {
      const MarkdownNode* const primary = primaryParagraph(node);
      BlockEditContextResolver resolver = contextResolver();
      for (const auto& child : node.children()) {
        if (primary != nullptr && child.get() == primary) {
          continue;  // the item's own paragraph is where the caret already is
        }
        if (resolver.firstEditableDescendant(*child) != nullptr) {
          return child.get();  // raw child (e.g. a nested List); visualEdgeHitForBlock descends
        }
      }
    }
    return nextSiblingAcrossContainers(*mut);
  }
  // Backward: climb to the previous sibling across containers, then descend to the LAST editable
  // block in its subtree (so Up from a block that follows a nested list lands on the last nested
  // item, not the parent). A list item's primary paragraph is an AST child of the ListItem, but the
  // caret/layout live on the ListItem, so when the previous sibling IS that paragraph, return the
  // ListItem itself (its own line precedes the nested content the caret came from).
  if (MarkdownNode* prev = previousSiblingAcrossContainers(*mut)) {
    if (prev->type() == BlockType::Paragraph && prev->parent() != nullptr &&
        prev->parent()->type() == BlockType::ListItem) {
      return prev->parent();
    }
    return deepestLastEditableInSubtree(*prev);
  }
  return nullptr;
}

MarkdownNode* InputController::deepestLastEditableInSubtree(MarkdownNode& node) const {
  if (node.type() == BlockType::ListItem) {
    const MarkdownNode* const primary = primaryParagraph(node);
    auto& children = node.children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      if (primary != nullptr && it->get() == primary) {
        continue;
      }
      if (MarkdownNode* e = deepestLastEditableInSubtree(*it->get())) {
        return e;
      }
    }
    return &node;  // a leaf list item (no nested content)
  }
  if (!isEditableTextBlock(node.type())) {
    // Container (List / BlockQuote): descend into the last child's subtree.
    auto& children = node.children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      if (MarkdownNode* e = deepestLastEditableInSubtree(*it->get())) {
        return e;
      }
    }
    return nullptr;
  }
  return &node;  // editable leaf (Paragraph / Heading / ...)
}

qsizetype InputController::selectableTextLength(const MarkdownNode& node) const {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return InlineProjection::plainTextForInlines(node.inlines()).size();
    case BlockType::ListItem:
      return plainTextForNode(node).size();
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return node.literal().size();
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return definitionSelectableLength(node);
    case BlockType::Table:
      return 1;
    default:
      return 0;
  }
}

QString InputController::selectableTextOf(const MarkdownNode& node) const {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return InlineProjection::plainTextForInlines(node.inlines());
    case BlockType::ListItem:
      return plainTextForNode(node);
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return node.literal();
    default:
      return {};  // Table / link / footnote defs: caret stepping is rare here — skip grapheme snap
  }
}

HitTestResult InputController::richHitForCursor(CursorPosition cursor) const {
  if (ctx_.view && cursor.isValid()) {
    HitTestResult hit = ctx_.view->hitForCursorPosition(cursor);
    if (hit.isValid()) {
      return hit;
    }
  }
  HitTestResult hit;
  hit.blockId = cursor.blockId;
  hit.textNodeId = cursor.text.nodeId;
  hit.textOffset = cursor.text.textOffset;
  hit.sourceOffset = cursor.text.sourceOffset;
  hit.zone = cursor.afterBlock ? HitTestResult::Zone::BlockAfter : HitTestResult::Zone::None;
  return hit;
}

HitTestResult InputController::visualEdgeHitForBlock(NodeId blockId, int direction, qreal documentX) const {
  HitTestResult result;
  if (!ctx_.hasSession() || !ctx_.view || !blockId.isValid()) {
    return result;
  }

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) {
    return result;
  }

  if (node->type() == BlockType::Table) {
    const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(node->id());
    if (!tableLayout || tableLayout->tableRows().empty()) {
      return result;
    }
    const int rowIndex = direction > 0 ? 0 : static_cast<int>(tableLayout->tableRows().size()) - 1;
    const auto& row = tableLayout->tableRows().at(static_cast<size_t>(rowIndex));
    if (row.cells.empty()) {
      return result;
    }
    int columnIndex = 0;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (int i = 0; i < static_cast<int>(row.cells.size()); ++i) {
      const qreal clampedX = qBound(row.cells.at(static_cast<size_t>(i)).rect.left(), documentX,
                                    row.cells.at(static_cast<size_t>(i)).rect.right());
      const qreal distance = qAbs(clampedX - documentX);
      if (distance < bestDistance) {
        bestDistance = distance;
        columnIndex = i;
      }
    }
    const auto& cell = row.cells.at(static_cast<size_t>(columnIndex));
    const int line = direction > 0 ? 0 : qMax(0, cell.text.visualLineCount() - 1);
    const QPointF origin = editor_geometry::tableCellTextOrigin(cell, ctx_.view->theme());
    const qsizetype localSource = cell.text.sourceOffsetAtVisualLineX(line, documentX - origin.x());
    result.zone = HitTestResult::Zone::TableCell;
    result.blockId = node->id();
    result.textNodeId = cell.nodeId;
    result.tableRow = rowIndex;
    result.tableColumn = columnIndex;
    result.sourceOffset = cell.contentSourceStart >= 0 ? cell.contentSourceStart + localSource : localSource;
    result.textOffset = cell.text.textOffsetAtVisualLineX(line, documentX - origin.x());
    result.blockRect = tableLayout->rect();
    result.cursorRect = cell.text.cursorRectForSourceOffset(localSource).translated(origin);
    return result;
  }

  BlockEditContextResolver resolver = contextResolver();
  MarkdownNode* editable = direction > 0 ? resolver.firstEditableDescendant(*node) : resolver.lastEditableDescendant(*node);
  if (!editable) {
    return result;
  }

  const BlockLayout* block = ctx_.view->blockLayoutForNode(editable->id());
  if (!block || !block->inlineLayout()) {
    const CursorPosition cursor = cursorForNode(*editable, direction > 0 ? 0 : selectableTextLength(*editable));
    return richHitForCursor(cursor);
  }

  const InlineLayout* inlineLayout = block->inlineLayout();
  const int line = direction > 0 ? 0 : qMax(0, inlineLayout->visualLineCount() - 1);
  const QPointF origin = block->inlineTextOrigin(ctx_.view->theme());
  const qsizetype localSource = inlineLayout->sourceOffsetAtVisualLineX(line, documentX - origin.x());
  const CursorPosition cursor = cursorForSourceInNode(*editable, block->contentSourceStart() + localSource);
  return richHitForCursor(cursor);
}

bool InputController::moveTableCellHorizontal(int direction, bool extendSelection) {
  if (!tableController_ || !ctx_.hasSession() || !ctx_.view || direction == 0) {
    return false;
  }
  const TableLocation location = tableController_->currentCell();
  if (!location.isValid() || !location.tableId.isValid()) {
    return false;
  }
  const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(location.tableId);
  if (!tableLayout || location.row < 0 || location.row >= static_cast<int>(tableLayout->tableRows().size())) {
    return false;
  }
  const auto& row = tableLayout->tableRows().at(static_cast<size_t>(location.row));
  if (location.column < 0 || location.column >= static_cast<int>(row.cells.size())) {
    return false;
  }

  MarkdownNode* table = ctx_.session->document().node(location.tableId);
  MarkdownNode* cellNode = ctx_.session->document().node(row.cells.at(static_cast<size_t>(location.column)).nodeId);
  if (!table || !cellNode) {
    return false;
  }

  BlockEditContext context;
  if (!contextResolver().fill(*cellNode, context)) {
    return false;
  }
  const CursorPosition current = ctx_.selection->cursorPosition();
  qsizetype currentSource = current.text.sourceOffset;
  if (currentSource < context.contentRange.byteStart || currentSource > context.contentRange.byteEnd) {
    qsizetype local = -1;
    if (!context.inlineProjection.sourceOffsetForVisibleOffset(current.text.textOffset, local)) {
      local = qBound<qsizetype>(0, current.text.textOffset, context.contentText.size());
    }
    currentSource = context.contentRange.byteStart + local;
  }

  qsizetype nextSource = currentSource + direction;
  int targetRow = location.row;
  int targetColumn = location.column;
  bool targetEnd = false;
  if (nextSource < context.contentRange.byteStart) {
    if (targetColumn > 0) {
      --targetColumn;
      targetEnd = true;
    } else if (targetRow > 0) {
      --targetRow;
      targetColumn = static_cast<int>(tableLayout->tableRows().at(static_cast<size_t>(targetRow)).cells.size()) - 1;
      targetEnd = true;
    } else if (MarkdownNode* previous = neighborBlockInDocumentDirection(*table, -1)) {
      setHitOrExtend(visualEdgeHitForBlock(previous->id(), -1, ctx_.view->effectiveCursorRect().left()), extendSelection);
      return true;
    } else {
      nextSource = context.contentRange.byteStart;
    }
  } else if (nextSource > context.contentRange.byteEnd) {
    if (targetColumn + 1 < static_cast<int>(row.cells.size())) {
      ++targetColumn;
    } else if (targetRow + 1 < static_cast<int>(tableLayout->tableRows().size())) {
      ++targetRow;
      targetColumn = 0;
    } else if (MarkdownNode* next = neighborBlockInDocumentDirection(*table, 1)) {
      setHitOrExtend(visualEdgeHitForBlock(next->id(), 1, ctx_.view->effectiveCursorRect().left()), extendSelection);
      return true;
    } else {
      nextSource = context.contentRange.byteEnd;
    }
  } else {
    const qsizetype localNext = nextSource - context.contentRange.byteStart;
    qsizetype tokenStart = 0, tokenEnd = 0;
    if (context.inlineProjection.foldedSpanInterior(localNext, tokenStart, tokenEnd)) {
      nextSource = context.contentRange.byteStart + (direction > 0 ? tokenEnd : tokenStart);
    }
  }

  if (targetRow != location.row || targetColumn != location.column) {
    const auto& targetRowLayout = tableLayout->tableRows().at(static_cast<size_t>(targetRow));
    if (targetColumn < 0 || targetColumn >= static_cast<int>(targetRowLayout.cells.size())) {
      return false;
    }
    const auto& targetCell = targetRowLayout.cells.at(static_cast<size_t>(targetColumn));
    MarkdownNode* targetNode = ctx_.session->document().node(targetCell.nodeId);
    if (!targetNode) {
      return false;
    }
    BlockEditContext targetContext;
    if (!contextResolver().fill(*targetNode, targetContext)) {
      return false;
    }
    nextSource = targetEnd ? targetContext.contentRange.byteEnd : targetContext.contentRange.byteStart;
  }

  const auto& finalRow = tableLayout->tableRows().at(static_cast<size_t>(targetRow));
  const auto& finalCell = finalRow.cells.at(static_cast<size_t>(targetColumn));
  MarkdownNode* finalNode = ctx_.session->document().node(finalCell.nodeId);
  BlockEditContext finalContext;
  if (!finalNode || !contextResolver().fill(*finalNode, finalContext)) {
    return false;
  }
  qsizetype visibleOffset = -1;
  const qsizetype localSource = qBound<qsizetype>(0, nextSource - finalContext.contentRange.byteStart, finalContext.contentText.size());
  if (!finalContext.inlineProjection.visibleOffsetForSourceOffset(localSource, visibleOffset)) {
    visibleOffset = localSource;
  }

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = table->id();
  hit.textNodeId = finalCell.nodeId;
  hit.tableRow = targetRow;
  hit.tableColumn = targetColumn;
  hit.sourceOffset = nextSource;
  hit.textOffset = visibleOffset;
  hit.blockRect = tableLayout->rect();
  const QPointF origin = editor_geometry::tableCellTextOrigin(finalCell, ctx_.view->theme());
  hit.cursorRect = finalCell.text.cursorRectForSourceOffset(localSource).translated(origin);
  setHitOrExtend(hit, extendSelection);
  return true;
}

bool InputController::moveTableCellVertical(int direction, bool extendSelection, qreal documentX) {
  if (!tableController_ || !ctx_.hasSession() || !ctx_.view || direction == 0) {
    return false;
  }
  const TableLocation location = tableController_->currentCell();
  if (!location.isValid() || !location.tableId.isValid()) {
    return false;
  }
  const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(location.tableId);
  if (!tableLayout || location.row < 0 || location.row >= static_cast<int>(tableLayout->tableRows().size())) {
    return false;
  }
  const int targetRow = location.row + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(tableLayout->tableRows().size())) {
    if (MarkdownNode* table = ctx_.session->document().node(location.tableId)) {
      if (MarkdownNode* target = neighborBlockInDocumentDirection(*table, direction)) {
        setHitOrExtend(visualEdgeHitForBlock(target->id(), direction, documentX), extendSelection);
        return true;
      }
    }
    return false;
  }

  const auto& currentRow = tableLayout->tableRows().at(static_cast<size_t>(location.row));
  if (location.column < 0 || location.column >= static_cast<int>(currentRow.cells.size())) {
    return false;
  }
  const auto& currentCell = currentRow.cells.at(static_cast<size_t>(location.column));
  int currentLine = 0;
  const CursorPosition current = ctx_.selection->cursorPosition();
  if (current.text.sourceOffset >= 0 && currentCell.contentSourceStart >= 0) {
    currentLine = currentCell.text.visualLineIndexForSourceOffset(current.text.sourceOffset - currentCell.contentSourceStart);
  }
  if (currentLine < 0) {
    currentLine = currentCell.text.visualLineIndexForTextOffset(current.text.textOffset);
  }
  currentLine = qMax(0, currentLine);

  const auto& row = tableLayout->tableRows().at(static_cast<size_t>(targetRow));
  if (row.cells.empty()) {
    return false;
  }
  const int targetColumn = qBound(0, location.column, static_cast<int>(row.cells.size()) - 1);
  const auto& targetCell = row.cells.at(static_cast<size_t>(targetColumn));
  const int lineCount = qMax(1, targetCell.text.visualLineCount());
  const int targetLine = qBound(0, currentLine, lineCount - 1);
  const QPointF origin = editor_geometry::tableCellTextOrigin(targetCell, ctx_.view->theme());
  const qreal localX = documentX - origin.x();
  const qsizetype localSource = targetCell.text.sourceOffsetAtVisualLineX(targetLine, localX);

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = location.tableId;
  hit.textNodeId = targetCell.nodeId;
  hit.tableRow = targetRow;
  hit.tableColumn = targetColumn;
  hit.sourceOffset = targetCell.contentSourceStart >= 0 ? targetCell.contentSourceStart + localSource : localSource;
  hit.textOffset = targetCell.text.textOffsetAtVisualLineX(targetLine, localX);
  hit.blockRect = tableLayout->rect();
  hit.cursorRect = targetCell.text.cursorRectForSourceOffset(localSource).translated(origin);
  setHitOrExtend(hit, extendSelection);
  return true;
}

// Place the caret at the start of a table cell, building the same rich hit the cell navigation
// paths produce. Returns false when the cell does not resolve (stale layout, out of range).
bool InputController::landOnTableCell(const NodeId& tableId, int row, int column) {
  if (!ctx_.hasSession() || !ctx_.view) {
    return false;
  }
  const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(tableId);
  if (!tableLayout || row < 0 || row >= static_cast<int>(tableLayout->tableRows().size())) {
    return false;
  }
  const auto& rowCells = tableLayout->tableRows().at(static_cast<size_t>(row)).cells;
  if (column < 0 || column >= static_cast<int>(rowCells.size())) {
    return false;
  }
  const auto& cell = rowCells.at(static_cast<size_t>(column));
  MarkdownNode* cellNode = ctx_.session->document().node(cell.nodeId);
  if (!cellNode) {
    return false;
  }
  BlockEditContext context;
  if (!contextResolver().fill(*cellNode, context)) {
    return false;
  }

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::TableCell;
  hit.blockId = tableId;
  hit.textNodeId = cell.nodeId;
  hit.tableRow = row;
  hit.tableColumn = column;
  hit.sourceOffset = context.contentRange.byteStart;
  hit.textOffset = 0;
  hit.blockRect = tableLayout->rect();
  const QPointF origin = editor_geometry::tableCellTextOrigin(cell, ctx_.view->theme());
  hit.cursorRect = cell.text.cursorRectForSourceOffset(0).translated(origin);
  setHitOrExtend(hit, false);
  return true;
}

// Tab/Backtab inside a table: move to the next/previous cell in row-major order, wrapping across
// rows. Tab from the last cell appends a row (Typora behaviour); Backtab from (0,0) leaves the
// table for the previous block.
bool InputController::moveTableCell(int direction) {
  if (!tableController_ || !ctx_.hasSession() || !ctx_.view || direction == 0) {
    return false;
  }
  const TableLocation location = tableController_->currentCell();
  if (!location.isValid() || !location.tableId.isValid()) {
    return false;
  }
  const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(location.tableId);
  const auto cellsInRow = [tableLayout](int row) -> int {
    if (!tableLayout || row < 0 || row >= static_cast<int>(tableLayout->tableRows().size())) {
      return -1;
    }
    return static_cast<int>(tableLayout->tableRows().at(static_cast<size_t>(row)).cells.size());
  };

  int targetRow = location.row;
  int targetColumn = location.column;

  if (direction > 0) {
    const int width = cellsInRow(targetRow);
    if (width > 0 && targetColumn + 1 < width) {
      ++targetColumn;
    } else {
      ++targetRow;
      targetColumn = 0;
      if (cellsInRow(targetRow) < 0) {
        // Past the last row: Tab appends a row and lands on its first cell.
        if (!tableController_->insertRowAfter()) {
          return false;
        }
        const TableLocation updated = tableController_->currentCell();
        if (!updated.isValid() || !updated.tableId.isValid()) {
          return false;
        }
        return landOnTableCell(updated.tableId, updated.row, 0);
      }
    }
  } else {
    if (targetColumn > 0) {
      --targetColumn;
    } else if (targetRow > 0) {
      --targetRow;
      targetColumn = qMax(0, cellsInRow(targetRow) - 1);
    } else {
      // (0,0): leave the table for the previous block.
      MarkdownNode* table = ctx_.session->document().node(location.tableId);
      if (!table) {
        return false;
      }
      if (MarkdownNode* previous = neighborBlockInDocumentDirection(*table, -1)) {
        setHitOrExtend(
            visualEdgeHitForBlock(previous->id(), -1, ctx_.view->effectiveCursorRect().left()), false);
        return true;
      }
      return false;
    }
  }
  return landOnTableCell(location.tableId, targetRow, targetColumn);
}

bool InputController::moveCursorHorizontal(int direction, bool extendSelection) {
  clearVerticalNavigationX();
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  if (moveTableCellHorizontal(direction, extendSelection)) {
    return true;
  }

  CursorPosition current = ctx_.selection->cursorPosition();
  MarkdownNode* node = ctx_.session->document().node(current.blockId);
  if (!node) {
    return false;
  }

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (resolver.fill(*node, context)) {
    const qsizetype currentSourceOffset =
        current.text.sourceOffset >= context.contentRange.byteStart && current.text.sourceOffset <= context.contentRange.byteEnd
            ? current.text.sourceOffset
            : context.contentRange.byteStart + qBound<qsizetype>(0, current.text.textOffset, context.contentText.size());
    qsizetype nextSourceOffset = currentSourceOffset + direction;
    if (nextSourceOffset < context.contentRange.byteStart) {
      if (MarkdownNode* previous = selectableBlockByDirection(current.blockId, -1)) {
        setCursorOrExtend(cursorForNode(*previous, selectableTextLength(*previous)), extendSelection);
        return true;
      }
      nextSourceOffset = context.contentRange.byteStart;
    } else if (nextSourceOffset > context.contentRange.byteEnd) {
      if (MarkdownNode* next = selectableBlockByDirection(current.blockId, 1)) {
        setCursorOrExtend(cursorForNode(*next, 0), extendSelection);
        return true;
      }
      nextSourceOffset = context.contentRange.byteEnd;
    }
    // Render-level smart punct: never stop mid-token (e.g. between the two dashes of a folded
    // en-dash). If the next offset lands strictly inside a folded token, jump to its boundary.
    {
      const qsizetype localNext = nextSourceOffset - context.contentRange.byteStart;
      qsizetype tokenStart = 0, tokenEnd = 0;
      if (context.inlineProjection.foldedSpanInterior(localNext, tokenStart, tokenEnd)) {
        nextSourceOffset = context.contentRange.byteStart + (direction > 0 ? tokenEnd : tokenStart);
      }
    }
    {
      // Grapheme step: never land the caret between a surrogate pair or inside a base+combining cluster.
      const qsizetype localNext = nextSourceOffset - context.contentRange.byteStart;
      nextSourceOffset = context.contentRange.byteStart +
          snapOffsetToGrapheme(context.contentText, localNext, direction);
    }
    setCursorOrExtend(cursorForSourceOffset(nextSourceOffset), extendSelection);
    return true;
  }

  const qsizetype length = selectableTextLength(*node);
  qsizetype nextOffset = current.text.textOffset + direction;
  if (nextOffset < 0) {
    if (MarkdownNode* previous = selectableBlockByDirection(current.blockId, -1)) {
      setCursorOrExtend(cursorForNode(*previous, selectableTextLength(*previous)), extendSelection);
      return true;
    }
    nextOffset = 0;
  } else if (nextOffset > length) {
    if (MarkdownNode* next = selectableBlockByDirection(current.blockId, 1)) {
      setCursorOrExtend(cursorForNode(*next, 0), extendSelection);
      return true;
    }
    nextOffset = length;
  }
  nextOffset = snapOffsetToGrapheme(selectableTextOf(*node), nextOffset, direction);

  setCursorOrExtend(cursorForNode(*node, nextOffset), extendSelection);
  return true;
}

MarkdownNode* InputController::wordEditNode(const CursorPosition& current) const {
  if (!ctx_.hasSession()) {
    return nullptr;
  }
  if (tableController_) {
    const TableLocation location = tableController_->currentCell();
    if (location.isValid() && location.tableId.isValid() && ctx_.view) {
      const BlockLayout* tableLayout = ctx_.view->blockLayoutForNode(location.tableId);
      if (tableLayout && location.row >= 0 && location.row < static_cast<int>(tableLayout->tableRows().size())) {
        const auto& row = tableLayout->tableRows().at(static_cast<size_t>(location.row));
        if (location.column >= 0 && location.column < static_cast<int>(row.cells.size())) {
          return ctx_.session->document().node(row.cells.at(static_cast<size_t>(location.column)).nodeId);
        }
      }
    }
  }
  return ctx_.session->document().node(current.blockId);
}

bool InputController::moveCursorWord(int direction, bool extendSelection) {
  clearVerticalNavigationX();
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  const CursorPosition current = ctx_.selection->cursorPosition();
  MarkdownNode* node = wordEditNode(current);
  if (!node) {
    return false;
  }

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.fill(*node, context)) {
    return false;
  }
  const qsizetype currentSourceOffset =
      current.text.sourceOffset >= context.contentRange.byteStart && current.text.sourceOffset <= context.contentRange.byteEnd
          ? current.text.sourceOffset
          : context.contentRange.byteStart + qBound<qsizetype>(0, current.text.textOffset, context.contentText.size());
  // Word semantics run on the source-space content text (same text the source-mode editor scans,
  // so both modes agree). Render-only folds are ASCII punctuation in source, so no folded-span
  // handling is needed at word granularity.
  const qsizetype local = qBound<qsizetype>(0, currentSourceOffset - context.contentRange.byteStart, context.contentText.size());
  const qsizetype nextLocal = direction < 0
      ? words::previousWordOffset(QStringView(context.contentText), local)
      : words::nextWordOffset(QStringView(context.contentText), local);

  if (nextLocal == local) {
    // No word boundary in that direction within this block — cross to the neighbouring block
    // (or stay put at the document edge, still consuming the key).
    if (MarkdownNode* neighbor = selectableBlockByDirection(current.blockId, direction)) {
      setCursorOrExtend(cursorForNode(*neighbor, direction > 0 ? 0 : selectableTextLength(*neighbor)), extendSelection);
    }
    return true;
  }

  const qsizetype nextSourceOffset = context.contentRange.byteStart +
      snapOffsetToGrapheme(context.contentText, nextLocal, direction);
  setCursorOrExtend(cursorForSourceInNode(*node, nextSourceOffset), extendSelection);
  return true;
}

bool InputController::deleteWordBackward() {
  if (hasActiveLiteralEditor()) {
    return deleteBackward();  // literal blocks keep character semantics for now
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  return deleteWordInDirection(-1) || deleteBackward();
}

bool InputController::deleteWordForward() {
  if (hasActiveLiteralEditor()) {
    return deleteForward();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  return deleteWordInDirection(1) || deleteForward();
}

bool InputController::deleteWordInDirection(int direction) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  const CursorPosition current = ctx_.selection->cursorPosition();
  MarkdownNode* node = wordEditNode(current);
  if (!node) {
    return false;
  }

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.fill(*node, context)) {
    return false;
  }
  const qsizetype currentSourceOffset =
      current.text.sourceOffset >= context.contentRange.byteStart && current.text.sourceOffset <= context.contentRange.byteEnd
          ? current.text.sourceOffset
          : context.contentRange.byteStart + qBound<qsizetype>(0, current.text.textOffset, context.contentText.size());
  const qsizetype local = qBound<qsizetype>(0, currentSourceOffset - context.contentRange.byteStart, context.contentText.size());
  const qsizetype stop = direction < 0
      ? words::previousWordOffset(QStringView(context.contentText), local)
      : words::nextWordOffset(QStringView(context.contentText), local);
  if (stop == local) {
    return false;  // block edge: caller falls back to character semantics (merge across blocks)
  }

  const qsizetype start = direction < 0 ? context.contentRange.byteStart + stop : currentSourceOffset;
  const qsizetype end = direction < 0 ? currentSourceOffset : context.contentRange.byteStart + stop;
  const CursorPosition preferred = cursorForSourceInNode(*node, start);
  applyLocalEdit(
      EditTransaction::Kind::DeleteText,
      QStringLiteral("Delete Word"),
      start,
      end - start,
      QString(),
      preferred,
      start,
      QVector<LocalEditNodeHint>{LocalEditNodeHint{node->id(), context.contentRange.byteStart, node->type()}});
  return true;
}

bool InputController::moveCursorVertical(int direction, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }

  const CursorPosition current = ctx_.selection->cursorPosition();
  if (hasVerticalNavigationX_ && (verticalNavigationCursor_.blockId != current.blockId ||
                                  verticalNavigationCursor_.text.nodeId != current.text.nodeId ||
                                  verticalNavigationCursor_.text.textOffset != current.text.textOffset ||
                                  verticalNavigationCursor_.text.sourceOffset != current.text.sourceOffset ||
                                  verticalNavigationCursor_.afterBlock != current.afterBlock)) {
    clearVerticalNavigationX();
  }

  MarkdownNode* node = ctx_.session->document().node(current.blockId);
  if (!node) {
    return false;
  }
  const BlockLayout* block = ctx_.view ? ctx_.view->blockLayoutForNode(current.blockId) : nullptr;

  if (!hasVerticalNavigationX_) {
    QRectF cursorRect;
    if (block && isLiteralBlockType(node->type())) {
      // Literal blocks use the UN-SCROLLED content cursor rect so the preserved column is the real
      // character column. effectiveCursorRect subtracts the horizontal-scroll offset for scrollable
      // code fences, which would drift the goal column on each press.
      cursorRect = block->literalVisualCursorRect(current.text.textOffset, ctx_.view->theme());
    } else if (ctx_.view) {
      cursorRect = ctx_.view->effectiveCursorRect();
      if (cursorRect.isEmpty()) {
        cursorRect = richHitForCursor(current).cursorRect;
      }
    }
    verticalNavigationX_ = cursorRect.isEmpty() ? 0.0 : cursorRect.left();
    hasVerticalNavigationX_ = true;
  }

  if (moveTableCellVertical(direction, extendSelection, verticalNavigationX_)) {
    verticalNavigationCursor_ = ctx_.selection->cursorPosition();
    return true;
  }

  if (block && isLiteralBlockType(node->type()) && hasActiveLiteralEditor()) {
    if (moveLiteralVertical(*block, *node, direction, extendSelection)) {
      verticalNavigationCursor_ = ctx_.selection->cursorPosition();
      return true;
    }
  }

  if (block && block->inlineLayout()) {
    const InlineLayout* inlineLayout = block->inlineLayout();
    int line = -1;
    if (current.text.sourceOffset >= 0 && block->contentSourceStart() >= 0) {
      line = inlineLayout->visualLineIndexForSourceOffset(current.text.sourceOffset - block->contentSourceStart());
    }
    if (line < 0) {
      line = inlineLayout->visualLineIndexForTextOffset(current.text.textOffset);
    }
    const int targetLine = line + direction;
    if (targetLine >= 0 && targetLine < inlineLayout->visualLineCount()) {
      const QPointF origin = block->inlineTextOrigin(ctx_.view->theme());
      const qreal localX = verticalNavigationX_ - origin.x();
      const CursorPosition target = block->contentSourceStart() >= 0
          ? cursorForSourceInNode(*node, block->contentSourceStart() + inlineLayout->sourceOffsetAtVisualLineX(targetLine, localX))
          : cursorForNode(*node, inlineLayout->textOffsetAtVisualLineX(targetLine, localX));
      setHitOrExtend(richHitForCursor(target), extendSelection);
      verticalNavigationCursor_ = ctx_.selection->cursorPosition();
      return true;
    }
  }

  MarkdownNode* target = neighborBlockInDocumentDirection(*node, direction);
  if (!target) {
    return false;
  }
  if (ctx_.view) {
    const HitTestResult hit = visualEdgeHitForBlock(target->id(), direction, verticalNavigationX_);
    if (hit.isValid()) {
      setHitOrExtend(hit, extendSelection);
      verticalNavigationCursor_ = ctx_.selection->cursorPosition();
      return true;
    }
  }
  const qsizetype offset = direction > 0 ? 0 : selectableTextLength(*target);
  setCursorOrExtend(cursorForNode(*target, offset), extendSelection);
  verticalNavigationCursor_ = ctx_.selection->cursorPosition();
  return true;
}

bool InputController::moveBlockVertical(int direction, bool extendSelection) {
  clearVerticalNavigationX();
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  // In a table cell, "paragraph" navigation degrades to row navigation (same as plain arrows)
  // so Ctrl+Up/Down never jumps the caret unexpectedly out of the table.
  if (tableController_ && tableController_->currentCell().isValid() && ctx_.view) {
    const QRectF caret = ctx_.view->effectiveCursorRect();
    if (moveTableCellVertical(direction, extendSelection, caret.isEmpty() ? 0.0 : caret.left())) {
      return true;
    }
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node) {
    return false;
  }
  MarkdownNode* target = neighborBlockInDocumentDirection(*node, direction);
  if (!target) {
    return true;  // document edge: stay put, but the key is still ours
  }
  // Word-like paragraph navigation: land at the target block's start in either direction.
  setCursorOrExtend(cursorForNode(*target, 0), extendSelection);
  return true;
}

// PageUp/PageDown: move the caret one viewport page (less a line of overlap) and scroll by the
// same delta so the caret keeps its screen position. Caret moves in this editor never auto-scroll,
// so the page handler owns both halves explicitly.
bool InputController::moveCursorPage(int direction, bool extendSelection) {
  clearVerticalNavigationX();
  if (!ctx_.hasSession() || !ctx_.hasCursor() || !ctx_.view || direction == 0) {
    return false;
  }

  QScrollBar* bar = ctx_.view->verticalScrollBar();
  const QRectF caret = ctx_.view->effectiveCursorRect();
  const qreal lineHeight = qMax<qreal>(1.0, caret.height());
  const qreal viewportHeight = static_cast<qreal>(ctx_.view->viewport()->height());
  // One page of caret travel, keeping one line of context at the boundary.
  const qreal delta = qBound<qreal>(lineHeight, viewportHeight - lineHeight, viewportHeight);
  const qreal maxY = qMax<qreal>(0.0, ctx_.view->layoutTotalHeight() - 1.0);
  const qreal docY = qBound<qreal>(0.0, caret.center().y() + direction * delta, maxY);
  const qreal newScroll =
      qBound<qreal>(static_cast<qreal>(bar->minimum()), static_cast<qreal>(bar->value()) + direction * delta,
                    static_cast<qreal>(bar->maximum()));

  // Scroll first, then resolve the caret: hitTest interprets viewport coordinates against the
  // CURRENT scroll position, so the hit must be taken in the already-scrolled frame.
  bar->setValue(qRound(newScroll));
  const HitTestResult hit = ctx_.view->hitTest(QPointF(caret.left(), docY - newScroll));
  if (hit.isValid()) {
    setHitOrExtend(hit, extendSelection);
  }
  return true;
}

bool InputController::moveLiteralVertical(const BlockLayout& block, MarkdownNode& node, int direction, bool extendSelection) {
  if (!ctx_.view || !ctx_.hasSession()) {
    return false;
  }
  const RenderTheme& theme = ctx_.view->theme();
  const CursorPosition current = ctx_.selection->cursorPosition();

  const int count = block.literalVisualLineCount(theme);
  if (count <= 0) {
    return false;
  }
  const int line = block.literalVisualLineIndexForOffset(current.text.textOffset, theme);
  const int targetLine = line + direction;
  if (targetLine < 0 || targetLine >= count) {
    return false;  // past the first/last visual line — caller falls through to leave the block
  }

  // Resolve the literal's ABSOLUTE content start (BlockLayout::contentSourceStart() is not populated
  // for literal blocks) and build the caret directly: a literal caret is just nodeId + offset into
  // node.literal(), so there is no document-wide offset walk as cursorForSourceOffset would do.
  const qsizetype literalStartSource = contextResolver().literalContentStartOffset(node);
  const QRectF contentRect = block.literalContentRect(theme);
  const qreal localX = verticalNavigationX_ - contentRect.left();
  const qsizetype targetLocal =
      qBound<qsizetype>(0, block.literalOffsetAtVisualLineX(targetLine, localX, theme), node.literal().size());
  CursorPosition target = cursorFor(node.id(), targetLocal);
  target.text.sourceOffset = literalStartSource + targetLocal;
  setHitOrExtend(richHitForCursor(target), extendSelection);
  return true;
}

bool InputController::moveJump(JumpTarget target, bool extendSelection) {
  clearVerticalNavigationX();
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }
  switch (target) {
    case JumpTarget::LineStart:
    case JumpTarget::LineEnd: {
      MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
      if (!node) {
        return false;
      }
      // Home/End navigate within the CURRENT LINE. For literal blocks (code/math/html/front
      // matter) the selectable text spans many physical lines, so jump to the current line's
      // boundaries (the previous/next '\n'); for single-line blocks (paragraph/heading) there
      // are no internal newlines and this reduces to block start/end — the previous behaviour.
      const BlockType t = node->type();
      const bool isLiteral = (t == BlockType::CodeFence || t == BlockType::MathBlock ||
                              t == BlockType::HtmlBlock || t == BlockType::FrontMatter);
      qsizetype offset;
      if (isLiteral) {
        const QString text = node->literal();
        const qsizetype cur = qBound<qsizetype>(0, ctx_.selection->cursorPosition().text.textOffset, text.size());
        if (target == JumpTarget::LineStart) {
          offset = cur <= 0 ? 0 : (text.lastIndexOf(QLatin1Char('\n'), cur - 1) + 1);
        } else {
          const qsizetype nl = text.indexOf(QLatin1Char('\n'), cur);
          offset = nl < 0 ? text.size() : nl;
        }
      } else {
        offset = target == JumpTarget::LineStart ? 0 : selectableTextLength(*node);
      }
      setCursorOrExtend(cursorForNode(*node, offset), extendSelection);
      return true;
    }
    case JumpTarget::DocumentStart:
    case JumpTarget::DocumentEnd: {
      MarkdownNode& root = ctx_.session->document().root();
      MarkdownNode* edge = nullptr;
      if (target == JumpTarget::DocumentStart) {
        for (const auto& child : root.children()) {
          if (child->type() != BlockType::Unknown) {
            edge = child.get();
            break;
          }
        }
      } else {
        for (auto it = root.children().rbegin(); it != root.children().rend(); ++it) {
          if ((*it)->type() != BlockType::Unknown) {
            edge = it->get();
            break;
          }
        }
      }
      if (!edge) {
        return false;
      }
      const qsizetype offset = target == JumpTarget::DocumentStart ? 0 : selectableTextLength(*edge);
      setCursorOrExtend(cursorForNode(*edge, offset), extendSelection);
      return true;
    }
  }
  return false;
}

bool InputController::selectNextOccurrence() {
  if (!ctx_.hasSession()) {
    return false;
  }
  qsizetype start = -1;
  qsizetype end = -1;
  if (!selectionSourceRange(start, end) || end <= start) {
    return false;  // nothing selected — caller expands to the current word first
  }
  const QString markdown = ctx_.session->markdownText().toString();
  const QString needle = markdown.mid(start, end - start);
  if (needle.isEmpty()) {
    return false;
  }
  qsizetype idx = markdown.indexOf(needle, end);
  if (idx < 0) {
    idx = markdown.indexOf(needle, 0);  // wrap around
  }
  if (idx < 0) {
    return false;
  }
  CursorPosition anchor = cursorForSourceOffset(idx);
  CursorPosition focus = cursorForSourceOffset(idx + needle.size());
  if (!anchor.isValid() || !focus.isValid()) {
    return false;
  }
  SelectionRange range;
  range.anchor = anchor;
  range.focus = focus;
  ctx_.selection->setSelection(range);
  return true;
}

bool InputController::selectSourceRange(qsizetype start, qsizetype end) {
  if (!ctx_.hasSession() || !ctx_.selection || start < 0 || end < start
      || end > ctx_.session->markdownText().size()) {
    return false;
  }
  CursorPosition anchor = cursorForSourceOffset(start);
  CursorPosition focus = cursorForSourceOffset(end);
  if (!anchor.isValid() || !focus.isValid()) { return false; }
  SelectionRange range;
  range.anchor = anchor;
  range.focus = focus;
  ctx_.selection->setSelection(range);
  return true;
}

void InputController::setCursorOrExtend(CursorPosition cursor, bool extendSelection) {
  if (!ctx_.selection || !cursor.isValid()) {
    return;
  }
  if (!extendSelection) {
    ctx_.selection->setCursorPosition(cursor);
    syncLiteralEditMode(cursor.blockId);
    return;
  }
  SelectionRange range = ctx_.selection->selection();
  if (!range.anchor.isValid()) {
    range.anchor = ctx_.selection->cursorPosition();
  }
  range.focus = cursor;
  ctx_.selection->setSelection(range);
}

void InputController::setHitOrExtend(HitTestResult hit, bool extendSelection) {
  if (!ctx_.selection || !hit.isValid()) {
    return;
  }
  const CursorPosition cursor = hit.cursorPosition();
  if (!cursor.isValid()) {
    return;
  }
  if (!extendSelection) {
    ctx_.selection->setHitResult(hit);
    syncLiteralEditMode(cursor.blockId);
    return;
  }
  SelectionRange range = ctx_.selection->selection();
  if (!range.anchor.isValid()) {
    range.anchor = ctx_.selection->cursorPosition();
  }
  range.focus = cursor;
  ctx_.selection->setSelection(range, hit);
}

void InputController::clearVerticalNavigationX() {
  hasVerticalNavigationX_ = false;
  verticalNavigationX_ = 0.0;
  verticalNavigationCursor_ = CursorPosition();
}

QString InputController::printableText(QKeyEvent* event) const {
  if (isDeadKey(event->key())) {
    return {};
  }
  const QString text = event->text();
  if (text.isEmpty()) {
    return {};
  }
  const QChar ch = text.at(0);
  if (ch.isNull() || ch.category() == QChar::Other_Control) {
    return {};
  }
  return text;
}

}  // namespace muffin
