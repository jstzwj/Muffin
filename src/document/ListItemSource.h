#pragma once

#include "document/SourceRangeUtil.h"

namespace muffin {

class PieceTable;

// A borrowed, revision-local view of an AST list item. Offsets are UTF-16 source offsets,
// like PieceTable and SourceRange; marker's offsets are relative to firstLine.start.
// Resolve on demand, rather than keeping another index or text copy for every AST node.
struct ListItemSource {
  ListLineInfo marker;
  const MarkdownNode* node = nullptr;
  const MarkdownNode* paragraph = nullptr;
  InlineRange firstLine;
  InlineRange markerToken; // Bullet or digits plus delimiter, without padding / checkbox.
  InlineRange item;
  InlineRange content;
  InlineRange children;

  bool isValid() const { return marker.valid && item.isValid() && content.isValid(); }
  qsizetype markerStart() const { return firstLine.start + marker.markerStart; }
  qsizetype markerContentStart() const { return firstLine.start + marker.contentStart; }
  qsizetype textStart() const { return marker.task ? firstLine.start + marker.taskContentStart : markerContentStart(); }
};

ListItemSource listItemSource(const PieceTable& markdown, const MarkdownNode& item);
QString listContinuationPrefix(const PieceTable& markdown, const ListItemSource& source);
// Physical-line prefix for child blocks / continuation text, including outer
// quotes and CommonMark marker padding (which does not include a task checkbox).
QString listContentPrefix(const PieceTable& markdown, const ListItemSource& source);
// Move a source slice between sibling items with different marker widths.
// The first line starts inside content; subsequent container prefixes move.
// AST-owned indented code gutters move too, retaining literal payload tabs.
QString rebaseListItemSource(const PieceTable& markdown, InlineRange range, const ListItemSource& from,
                             const ListItemSource& to);

// Only direct AST siblings belong to a numbering run. Literal blocks, nested lists and
// marker-looking continuation text must never be renumbered. An authored blank separator
// ends the run, retaining the editor's existing split-list behavior.
qsizetype listSiblingRunEnd(const PieceTable& markdown, const MarkdownNode* first);
const MarkdownNode* nextListItemInRun(const PieceTable& markdown, const MarkdownNode& item);
QString renumberListItems(const PieceTable& markdown, InlineRange source, const MarkdownNode* first, int nextNumber,
                          bool sequential);

} // namespace muffin
