#pragma once

#include "document/DocumentSession.h"
#include "document/TextSelection.h"
#include "edit/EditTransaction.h"

#include <QString>
#include <QVector>

#include <functional>

namespace muffin {

struct EditorContext;

// A replacement of one range of the document's Markdown text.
struct TextEdit {
  qsizetype sourceStart = 0;
  qsizetype removedLength = 0;
  QString insertedText;
  // The blocks the edit is about, usually the ones holding the caret and the selection. The
  // slice parser keeps their node ids, and they are re-laid out afterwards.
  QVector<LocalEditNodeHint> nodeHints;
};

// What the editor does around an edit, beyond changing the text.
struct EditOptions {
  EditTransaction::Kind kind = EditTransaction::Kind::ReplaceDocumentText;
  QString label;

  // Where the caret goes. `cursor` is preferred: it may still name nodes of the document as it
  // was before the edit, since it is resolved again through its source offset. Otherwise the
  // caret is the one at `fallbackSourceOffset`. `preferLaterEmptyAtOffset` resolves a boundary
  // that blocks share to the empty paragraph starting there.
  CursorPosition cursor;
  qsizetype fallbackSourceOffset = -1;
  bool preferLaterEmptyAtOffset = false;

  // Select [selectionStart, selectionEnd) of the text as it is after the edit instead, with the
  // anchor at the end when `selectionBackwards`. An empty range, or one without a caret at both
  // ends, leaves the caret as described above.
  qsizetype selectionStart = -1;
  qsizetype selectionEnd = -1;
  bool selectionBackwards = false;

  // The edit replaces whole blocks rather than text inside one, so the blocks behind the edited
  // one move: they are all re-laid out.
  bool structureEdit = false;

  // Undo restores a snapshot of the document instead of replaying the text change. For an edit
  // whose result the parser would not rebuild from the text alone, such as a block being
  // committed while its opening marker is still being typed.
  bool snapshotUndo = false;

  // Lay the touched blocks out before returning instead of on the brush queue's timer. Typing
  // needs it: the caret must have its geometry as soon as the key is handled.
  bool layoutImmediately = false;

  // Replaces the undo record the editor would write, for a command that stores what it did in its
  // own terms, such as a removed node that undo puts back.
  std::function<EditTransaction(const struct EditOutcome&)> undoRecord;
};

struct EditOutcome {
  // False when nothing changed: editing is locked, or the range is not inside the document.
  bool applied = false;
  // The slice parser could not take the edit, so the whole document was re-parsed. Node ids
  // noted before the edit may be gone, and every block may look different.
  bool reparsed = false;
  qsizetype sourceStart = 0;
  qsizetype removedLength = 0;
  QString removedText;
  // The selection before the edit, its ends anchored to source offsets. Undo restores it.
  SelectionRange beforeSelection;
  // The caret after the edit, or the focus of the selection the edit left.
  CursorPosition cursor;
};

// Applies an edit made in the editor. This is the one path a user edit takes: it changes the
// text through the session, puts the caret or selection where the edit leaves them, records the
// edit for undo and has the touched blocks re-laid out. An edit the session does not apply is
// dropped whole: the caret, the undo stack and the view are left as they were.
EditOutcome applyEdit(EditorContext& ctx, TextEdit edit, const EditOptions& options = {});

// Has the blocks re-laid out after an edit that was applied through DocumentSession directly.
// A full re-parse leaves no block that could be trusted, so then everything is re-laid out.
void requestEditRefresh(const EditorContext& ctx, TextEditResult result, QVector<NodeId> blocks);

} // namespace muffin
