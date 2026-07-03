#include "editor/InputController.h"

#include "blocks/table/TableController.h"
#include "document/DocumentSession.h"
#include "edit/EditTransaction.h"
#include "editor/EditorView.h"
#include "editor/EmojiCompleter.h"
#include "editor/EmojiProvider.h"
#include "editor/SelectionController.h"

#include <QPoint>
#include <QRectF>
#include <QSettings>
#include <QString>

namespace muffin {

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

}  // namespace muffin
