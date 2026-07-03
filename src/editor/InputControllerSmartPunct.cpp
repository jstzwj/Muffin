#include "editor/InputController.h"

#include "document/DocumentSession.h"
#include "edit/EditTransaction.h"
#include "editor/BlockEditContext.h"
#include "editor/SelectionController.h"
#include "editor/SmartPunctuation.h"

#include <QSettings>
#include <QString>

namespace muffin {
namespace {

// markdown/* smart-punctuation settings. Read raw at call time so toggling takes effect on the next
// keystroke, matching editor/matchBrackets.
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

}  // namespace

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

}  // namespace muffin
