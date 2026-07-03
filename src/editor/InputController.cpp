#include "editor/InputController.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "document/BlockPredicates.h"
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/EmojiCompleter.h"
#include "editor/EmojiProvider.h"
#include "editor/SelectionController.h"
#include "editor/SmartPunctuation.h"
#include "editor/TextBlockCommandBuilder.h"
#include "edit/UndoStack.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"
#include "blocks/table/TableController.h"

#include <QEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QInputMethodEvent>
#include <QPoint>
#include <QSettings>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(inputPerf, "muffin.perf", QtWarningMsg)

// editor/matchBrackets (default on) and editor/matchMarkdown (default off) gate the auto-pair
// tables below. Read raw at call time so toggling the preference takes effect on the next keystroke.
bool matchBracketsEnabled() {
  return QSettings().value(QStringLiteral("editor/matchBrackets"), true).toBool();
}

bool matchMarkdownEnabled() {
  return QSettings().value(QStringLiteral("editor/matchMarkdown"), false).toBool();
}

// markdown/* smart-punctuation settings. Read raw at call time so toggling takes effect on the next
// keystroke, matching editor/matchBrackets above.
// markdown/convertOnInput: 0 = off, 1 = convert single chars while typing, 2 = also convert bulk text.
int smartPunctuationMode() {
  return qBound(0, QSettings().value(QStringLiteral("markdown/convertOnInput"), 0).toInt(), 2);
}

bool smartQuotesEnabled() {
  return QSettings().value(QStringLiteral("markdown/smartQuotes"), false).toBool();
}

bool smartDashesEnabled() {
  return QSettings().value(QStringLiteral("markdown/smartDashes"), false).toBool();
}

// Style 0 = curly (convert), 1 = straight (leave as-is).
int singleQuoteStyleIndex() {
  return qBound(0, QSettings().value(QStringLiteral("markdown/singleQuoteStyle"), 0).toInt(), 1);
}

int doubleQuoteStyleIndex() {
  return qBound(0, QSettings().value(QStringLiteral("markdown/doubleQuoteStyle"), 0).toInt(), 1);
}

// The opener -> closer pairs active under the current preferences.
struct PairedChar {
  QChar opener;
  QChar closer;
};
QVector<PairedChar> activePairedChars() {
  QVector<PairedChar> pairs;
  if (matchBracketsEnabled()) {
    pairs.append({QLatin1Char('('), QLatin1Char(')')});
    pairs.append({QLatin1Char('['), QLatin1Char(']')});
    pairs.append({QLatin1Char('{'), QLatin1Char('}')});
    pairs.append({QLatin1Char('"'), QLatin1Char('"')});
    pairs.append({QLatin1Char('\''), QLatin1Char('\'')});
  }
  if (matchMarkdownEnabled()) {
    pairs.append({QLatin1Char('*'), QLatin1Char('*')});
    pairs.append({QLatin1Char('_'), QLatin1Char('_')});
    pairs.append({QLatin1Char('`'), QLatin1Char('`')});
    pairs.append({QLatin1Char('~'), QLatin1Char('~')});
  }
  return pairs;
}

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
  }
  return QObject::eventFilter(watched, event);
}

bool InputController::insertText(QString text) {
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

bool InputController::tryAutoPairOrWrap(QChar ch) {
  const QVector<PairedChar> pairs = activePairedChars();

  QChar closerForOpener;
  bool isCloser = false;
  for (const PairedChar& p : pairs) {
    if (p.opener == ch) {
      closerForOpener = p.closer;
    }
    if (p.closer == ch) {
      isCloser = true;
    }
  }
  if (closerForOpener.isNull() && !isCloser) {
    return false;  // not a paired character under the active preferences
  }

  const bool hasCursor = ctx_.selection && ctx_.selection->hasCursor();
  const bool hasSelection = hasCursor && !ctx_.selection->selection().isCollapsed();

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  const bool resolved = resolver.current(context) && context.node;
  // The caret source offset comes from the resolved context (computed from the cursor's block and
  // textOffset) rather than cursorPosition().text.sourceOffset, which callers that place the cursor
  // programmatically may leave unset (-1).
  const qsizetype off = resolved ? context.cursorSourceOffset : -1;
  const PieceTable& markdown = ctx_.session ? ctx_.session->markdownText() : PieceTable::empty();

  // A character right after a backslash is an escape / LaTeX sequence (e.g. "\[", "\(", "\{"),
  // so auto-pairing it would corrupt the delimiter — insert it literally instead.
  if (off > 0 && off <= markdown.size() && markdown.at(off - 1) == QLatin1Char('\\')) {
    return false;
  }

  // Skip-over: the caret sits right before the matching close character, so stepping over it
  // (instead of inserting a duplicate) is what the user wants. Covers symmetric pairs (* ` " ')
  // and pure closers typed immediately after an auto-paired opener.
  if (!hasSelection && isCloser && off >= 0 && off < markdown.size() && markdown.at(off) == ch) {
    setCursorOrExtend(cursorForSourceOffset(off + 1), false);
    return true;
  }

  if (closerForOpener.isNull()) {
    return false;  // a pure closer without a skip-over inserts normally
  }

  // A single quote flanked by letters is a contraction (don't), not an opener — decline so the
  // lone quote inserts instead of a pairing that would turn "don't" into "don''t".
  if (ch == QLatin1Char('\'') && off > 0 && off < markdown.size() &&
      markdown.at(off - 1).isLetter() && markdown.at(off).isLetter()) {
    return false;
  }

  // Wrap an existing selection in opener + closer (single undo entry via replaceSelection).
  if (hasSelection) {
    qsizetype start = 0;
    qsizetype end = 0;
    if (selectionSourceRange(start, end) && end > start) {
      const QString wrapped = QString(ch) + markdown.mid(start, end - start) + QString(closerForOpener);
      replaceSelection(wrapped, EditTransaction::Kind::InsertText, QStringLiteral("Wrap Selection"));
      return true;
    }
    return false;
  }

  // Insert the empty pair with the caret between, mirroring the plain InsertText command's
  // sourceStart arithmetic so it composes identically with the projection layer.
  if (!resolved) {
    return false;
  }
  const qsizetype nextOffset = context.plainInlineEditable
      ? qBound<qsizetype>(0, context.cursorTextOffset, context.contentText.size())
      : qBound<qsizetype>(0, context.cursorSourceOffset - context.contentRange.byteStart, context.contentText.size());
  const qsizetype sourceStart = context.contentRange.byteStart + nextOffset;
  const QString pair = QString(ch) + QString(closerForOpener);
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Paired Symbol"),
      sourceStart,
      0,
      pair,
      CursorPosition(),
      sourceStart + 1,
      {},
      false,
      false);
  return true;
}

bool InputController::trySmartDashes(QChar ch) {
  if (ch != QLatin1Char('-') || smartPunctuationMode() == 0 || !smartDashesEnabled()) {
    return false;
  }
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node) {
    return false;
  }
  const PieceTable& md = ctx_.session ? ctx_.session->markdownText() : PieceTable::empty();
  const qsizetype off = context.cursorSourceOffset;
  if (off < 1 || off > md.size()) {
    return false;
  }
  // "---" → em-dash: two dashes already precede the caret; the typed dash is consumed into "—".
  // Honor a backslash escape on the first dash of the run (mirrors tryAutoPairOrWrap's guard).
  if (off >= 2 && md.at(off - 1) == QLatin1Char('-') && md.at(off - 2) == QLatin1Char('-') &&
      !(off >= 3 && md.at(off - 3) == QLatin1Char('\\'))) {
    applyLocalEdit(EditTransaction::Kind::InsertText, QStringLiteral("Smart Dash"),
                   off - 2, 2, smartpunct::kEmDash, CursorPosition(), off - 1, {}, false, false);
    return true;
  }
  // "--" → en-dash: one dash precedes the caret (and isn't escaped).
  if (md.at(off - 1) == QLatin1Char('-') && !(off >= 2 && md.at(off - 2) == QLatin1Char('\\'))) {
    applyLocalEdit(EditTransaction::Kind::InsertText, QStringLiteral("Smart Dash"),
                   off - 1, 1, smartpunct::kEnDash, CursorPosition(), off, {}, false, false);
    return true;
  }
  return false;
}

bool InputController::trySmartEllipsis(QChar ch) {
  // "..." → ellipsis. Rides on the Smart Dashes toggle (no separate option).
  if (ch != QLatin1Char('.') || smartPunctuationMode() == 0 || !smartDashesEnabled()) {
    return false;
  }
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node) {
    return false;
  }
  const PieceTable& md = ctx_.session ? ctx_.session->markdownText() : PieceTable::empty();
  const qsizetype off = context.cursorSourceOffset;
  if (off < 1 || off > md.size()) {
    return false;
  }
  // Two dots already precede the caret; the typed dot is consumed into "…". Honor a backslash
  // escape on the first dot of the run.
  if (off >= 2 && md.at(off - 1) == QLatin1Char('.') && md.at(off - 2) == QLatin1Char('.') &&
      !(off >= 3 && md.at(off - 3) == QLatin1Char('\\'))) {
    applyLocalEdit(EditTransaction::Kind::InsertText, QStringLiteral("Smart Ellipsis"),
                   off - 2, 2, smartpunct::kEllipsis, CursorPosition(), off - 1, {}, false, false);
    return true;
  }
  return false;
}

QString InputController::maybeConvertSmartPunctuation(QString text) {
  const int mode = smartPunctuationMode();
  if (mode == 0 || !smartQuotesEnabled()) {
    return text;
  }
  if (mode == 1 && text.size() != 1) {
    return text;  // "when typing" converts only single-character keystrokes
  }
  const int doubleStyle = doubleQuoteStyleIndex();
  const int singleStyle = singleQuoteStyleIndex();
  if (doubleStyle == 1 && singleStyle == 1) {
    return text;  // both quote styles set to straight → nothing to convert
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return text;  // selection path is owned by auto-pair/wrap & replace
  }

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  const bool resolved = resolver.current(context) && context.node;
  const qsizetype off = resolved ? context.cursorSourceOffset : -1;
  const PieceTable& md = ctx_.session ? ctx_.session->markdownText() : PieceTable::empty();

  QString out;
  out.reserve(text.size() + 4);
  QChar prev = (off > 0 && off <= md.size()) ? md.at(off - 1) : QChar();
  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    // Honor a backslash escape: a quote right after '\' stays literal.
    if (prev == QLatin1Char('\\')) {
      out += c;
      prev = c;
      continue;
    }
    if (c == QLatin1Char('"') && doubleStyle == 0) {
      out += smartpunct::isOpeningQuoteContext(prev) ? smartpunct::kLeftDoubleQuote : smartpunct::kRightDoubleQuote;
    } else if (c == QLatin1Char('\'') && singleStyle == 0) {
      out += smartpunct::isOpeningQuoteContext(prev) ? smartpunct::kLeftSingleQuote : smartpunct::kRightSingleQuote;
    } else {
      out += c;
    }
    prev = c;
  }
  return out;
}

void InputController::setEmojiProvider(const EmojiProvider* provider) {
  emojiProvider_ = provider;
  emojiColonStart_ = -1;
  if (emojiCompleter_) {
    emojiCompleter_->hide();
  }
}

EmojiCompleter* InputController::ensureEmojiCompleter() {
  if (emojiCompleter_ || !emojiProvider_ || !ctx_.view) {
    return emojiCompleter_;
  }
  emojiCompleter_ = new EmojiCompleter(ctx_.view->viewport(), emojiProvider_, this);
  connect(emojiCompleter_, &EmojiCompleter::accepted, this, [this](const QString& glyph) {
    insertEmoji(glyph);
  });
  return emojiCompleter_;
}

void InputController::hideEmojiPopup() {
  emojiColonStart_ = -1;
  if (emojiCompleter_) {
    emojiCompleter_->hide();
  }
}

// Re-scan the line up to the caret for a ":shortcode" trigger and show/hide the popup accordingly.
// State is derived from the document source on every keystroke (no fragile accumulated buffer).
void InputController::maybeUpdateEmojiPopup() {
  if (!emojiProvider_ || !ctx_.hasSession() || !ctx_.selection) {
    return;
  }
  if (!QSettings().value(QStringLiteral("editor/emojiAutocomplete"), true).toBool() ||
      hasActiveLiteralEditor() || (tableController_ && tableController_->currentCell().isValid())) {
    hideEmojiPopup();
    return;
  }
  const CursorPosition cursor = ctx_.selection->cursorPosition();
  const qsizetype caret = cursor.text.sourceOffset;
  const PieceTable& markdown = ctx_.session->markdownText();
  if (!cursor.isValid() || caret < 0 || caret > markdown.size()) {
    hideEmojiPopup();
    return;
  }
  qsizetype lineStart = caret;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  qsizetype colon = -1;
  for (qsizetype i = caret - 1; i >= lineStart; --i) {
    if (markdown.at(i) == QLatin1Char(':')) {
      colon = i;
      break;
    }
  }
  if (colon < 0) {
    hideEmojiPopup();
    return;
  }
  const QString code = markdown.mid(colon + 1, caret - colon - 1);
  // A valid partial shortcode is non-empty and only [A-Za-z0-9_+-]; anything else (a space, the
  // closing ':', etc.) breaks the trigger.
  if (code.isEmpty()) {
    hideEmojiPopup();
    return;
  }
  for (const QChar c : code) {
    const ushort u = c.unicode();
    const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
                    u == '_' || u == '+' || u == '-';
    if (!ok) {
      hideEmojiPopup();
      return;
    }
  }

  emojiColonStart_ = colon;
  EmojiCompleter* completer = ensureEmojiCompleter();
  if (!completer || !ctx_.view) {
    return;
  }
  const QRectF caretDocRect = ctx_.selection->currentHit().cursorRect;
  const QPointF caretViewport = ctx_.view->mapDocumentToViewport(
      caretDocRect.isEmpty() ? QPointF(0, 0) : QPointF(caretDocRect.left(), caretDocRect.bottom()));
  completer->present(code, QPoint(qRound(caretViewport.x()), qRound(caretViewport.y())));
}

void InputController::insertEmoji(const QString& glyph) {
  if (emojiColonStart_ < 0 || !ctx_.hasSession()) {
    return;
  }
  const qsizetype caret = ctx_.selection ? ctx_.selection->cursorPosition().text.sourceOffset : -1;
  if (caret < emojiColonStart_) {
    return;
  }
  const qsizetype removedLength = caret - emojiColonStart_;
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Emoji"),
      emojiColonStart_,
      removedLength,
      glyph,
      CursorPosition(),
      emojiColonStart_ + glyph.size(),
      {},
      false,
      false);
  emojiColonStart_ = -1;
}

bool InputController::insertParagraphBreak() {
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
    const auto& siblings = node->parent()->children();
    for (size_t i = 0; i < siblings.size(); ++i) {
      if (siblings.at(i).get() == node) {
        if (i + 1 < siblings.size()) {
          following = siblings.at(i + 1).get();
        }
        break;
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
      // For a literal block (code/math/html) the new paragraph sits AFTER the separator, so the caret
      // lands at insertOffset + size. But for a non-text leaf like a rule, the new "paragraph" is a
      // virtual empty paragraph at the START of the inserted blank-line run (the parser places the VEP
      // on the first blank line of the gap). Pointing the caret past the "\n\n" there landed in a
      // blank gap no block covers, so cursorForSourceOffset failed and the caret fell back to
      // END-OF-DOCUMENT — which looked like it vanished ("press Enter below the rule → caret
      // disappears"). With preferLaterEmptyAtOffset=true (passed below) the VEP at insertOffset
      // resolves cleanly.
      caretOffset = isLiteralBlock ? (insertOffset + insertedText.size()) : insertOffset;
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

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();
  const BlockType removedNodeType = node->type();

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
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            removedNodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
    // Flush synchronously so the layout reflects this deletion before control returns to
    // the event loop — mirrors applyLocalEdit (see InputControllerEdit.cpp). Without it,
    // BrushQueue defers via a 0-ms timer and the view paints one frame of stale layout
    // (following blocks still at their pre-deletion Y).
    ctx_.brushQueue->flush();
  }
  return true;
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

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();

  // Exit edit mode before removing the block to clear stale editing state
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    codeFenceController_->exitEditMode();
  } else if (LiteralBlockController* math = ctx_.literalEditors.value(static_cast<int>(BlockType::MathBlock))) {
    if (math->isEditing()) {
      math->exitEditMode();
    }
  }

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, nodeType});
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
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            nodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
    // Flush synchronously so the layout reflects this deletion before control returns to
    // the event loop — mirrors applyLocalEdit (see InputControllerEdit.cpp). Without it,
    // BrushQueue defers via a 0-ms timer and the view paints one frame of stale layout
    // (following blocks still at their pre-deletion Y).
    ctx_.brushQueue->flush();
  }
  return true;
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

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();
  const BlockType removedNodeType = node->type();

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
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            removedNodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
    // Flush synchronously so the layout reflects this deletion before control returns to
    // the event loop — mirrors applyLocalEdit (see InputControllerEdit.cpp). Without it,
    // BrushQueue defers via a 0-ms timer and the view paints one frame of stale layout
    // (following blocks still at their pre-deletion Y).
    ctx_.brushQueue->flush();
  }
  return true;
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

  if (!ctx_.hasSession() || !ctx_.hasSelection() || !ctx_.view || event->modifiers().testFlag(Qt::ControlModifier) ||
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
      return outdentListItem();
    case Qt::Key_Backspace:
      return deleteBackward();
    case Qt::Key_Delete:
      return deleteForward();
    case Qt::Key_Left:
      return moveCursorHorizontal(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Right:
      return moveCursorHorizontal(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Up:
      return moveCursorVertical(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Down:
      return moveCursorVertical(1, event->modifiers().testFlag(Qt::ShiftModifier));
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

  // A source offset that lands inside a literal block (code/math/HTML/front matter) resolves to
  // that block. nodeAtContentSourceOffset only matches inline-editable text, so without this a
  // freshly committed "```"/"$$" block would leave the post-edit caret unresolvable.
  qsizetype literalContentStart = -1;
  if (MarkdownNode* literal =
          resolver.literalBlockAtSourceOffset(ctx_.session->document().root(), sourceOffset, literalContentStart)) {
    cursor.blockId = literal->id();
    cursor.text.nodeId = literal->id();
    cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - literalContentStart, literal->literal().size());
    cursor.text.sourceOffset = sourceOffset;
    return cursor;
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
  if (MarkdownNode* host = ctx_.session->document().topLevelBlockAtOffset(sourceOffset)) {
    if (!resolver.firstEditableDescendant(*host)) {
      return cursorForBlockAfter(*host, sourceOffset);
    }
  }

  MarkdownNode* node = paragraphAtSourceOffset(ctx_.session->document().root(), sourceOffset, preferLaterEmptyAtOffset);
  if (!node) {
    return cursor;
  }

  BlockEditContext context;
  if (!resolver.fill(*node, context)) {
    return cursor;
  }
  const qsizetype localSourceOffset = qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
  qsizetype visibleOffset = -1;
  if (context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
    CursorPosition cursorForVisible = cursorFor(node->id(), visibleOffset);
    cursorForVisible.text.sourceOffset = sourceOffset;
    return cursorForVisible;
  }
  CursorPosition fallbackCursor = cursorFor(node->id(), qBound<qsizetype>(0, localSourceOffset, context.visibleText.size()));
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

bool InputController::moveCursorHorizontal(int direction, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
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

  setCursorOrExtend(cursorForNode(*node, nextOffset), extendSelection);
  return true;
}

bool InputController::moveCursorVertical(int direction, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  MarkdownNode* target = selectableBlockByDirection(ctx_.selection->cursorPosition().blockId, direction);
  if (!target) {
    return false;
  }
  const qsizetype offset = direction > 0 ? 0 : selectableTextLength(*target);
  setCursorOrExtend(cursorForNode(*target, offset), extendSelection);
  return true;
}

bool InputController::moveJump(JumpTarget target, bool extendSelection) {
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
