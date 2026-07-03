#include "editor/InputController.h"

#include "document/DocumentSession.h"
#include "edit/EditTransaction.h"
#include "editor/BlockEditContext.h"
#include "editor/SelectionController.h"

#include <QSettings>
#include <QString>
#include <QVector>

namespace muffin {
namespace {

// editor/matchBrackets (default on) and editor/matchMarkdown (default off) gate the auto-pair
// tables below. Read raw at call time so toggling the preference takes effect on the next keystroke.
bool matchBracketsEnabled() {
  return QSettings().value(QStringLiteral("editor/matchBrackets"), true).toBool();
}

bool matchMarkdownEnabled() {
  return QSettings().value(QStringLiteral("editor/matchMarkdown"), false).toBool();
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

}  // namespace

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

}  // namespace muffin
