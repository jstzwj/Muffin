#pragma once

#include "document/DocumentSession.h"
#include "document/NodeId.h"
#include "editor/BlockEditContext.h"

#include <QtGlobal>

namespace muffin {

// Source offset <-> caret, written once.
//
// Edits, selections and the parser speak in source offsets of the Markdown text. A CursorPosition
// speaks in offsets of the text that is shown: the inline projection hides the Markdown markers
// around it. Converting between the two means finding the block that owns the offset and mapping
// through that block's projection.
//
// The conversion used to exist four times. The copy shared by the style and paragraph commands
// wrote the source offset straight into textOffset, as if the block held no marker: behind a
// `**bold**` word the caret landed too far right, and undo — which used the correct mapping —
// then put it somewhere else than the command had.

// The caret in `blockId` at visible `offset`. A table cell owns its text while its table owns
// the rendered block, so the caret names both, the same way hit testing does.
CursorPosition cursorForBlock(NodeId blockId, qsizetype offset, const DocumentSession* session);

// The caret for a source offset. An offset inside a literal block (code, math, HTML, front
// matter) resolves to that block, and one on a block that holds no inline text resolves to a
// caret behind that block. The result is invalid when no block claims the offset.
//
// `preferLaterEmptyAtOffset` resolves a boundary that blocks share to the empty paragraph
// starting there, which keeps a caret that is typing into that paragraph inside it.
CursorPosition cursorForSourceOffset(const BlockEditContextResolver& resolver, qsizetype sourceOffset,
                                     bool preferLaterEmptyAtOffset = false);

// The source offset mapped through the edit context of a block that is already known.
CursorPosition cursorForSourceInNode(const BlockEditContextResolver& resolver, MarkdownNode& node,
                                     qsizetype sourceOffset);
CursorPosition cursorForSourceInContext(const BlockEditContextResolver& resolver, const BlockEditContext& context,
                                        qsizetype sourceOffset);

// A caret behind a top-level block that has no editable text of its own (a thematic break).
CursorPosition blockAfterCursor(const MarkdownNode& host, qsizetype sourceOffset);

// Where the caret goes after an edit: the preferred caret, resolved again through its source
// offset so that it follows the text, or else the caret at `fallbackSourceOffset`.
CursorPosition cursorAfterEdit(const BlockEditContextResolver& resolver, CursorPosition preferredCursor,
                               qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset = false);

} // namespace muffin
