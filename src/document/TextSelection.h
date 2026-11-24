#pragma once

#include "document/NodeId.h"

namespace muffin {

struct TextPosition {
  NodeId nodeId;
  qsizetype textOffset = 0;
  qsizetype sourceOffset = -1;
  bool inMeta = false;

  bool isValid() const { return nodeId.isValid(); }

  friend bool operator==(const TextPosition&, const TextPosition&) = default;
};

struct CursorPosition {
  NodeId blockId;
  TextPosition text;
  // True when the caret sits on the virtual trailing empty paragraph below
  // this block (Zone::BlockAfter). Persisted so the trailing caret survives
  // layout rebuilds; never set for a caret inside real block content.
  bool afterBlock = false;

  bool isValid() const { return blockId.isValid(); }

  // One caret position. Every field names part of the position: table cells share a blockId but
  // not a text node, and a caret behind a block is not the caret at the block's end.
  friend bool operator==(const CursorPosition&, const CursorPosition&) = default;
};

struct SelectionRange {
  CursorPosition anchor;
  CursorPosition focus;

  // Document selection includes syntax outside editable text (list/heading
  // markers, fences and blank lines). Its explicit source endpoints remain
  // authoritative even when a rendered endpoint lies inside a container.
  bool coversDocument(qsizetype sourceLength) const {
    return sourceLength > 0 && anchor.isValid() && focus.isValid() &&
           qMin(anchor.text.sourceOffset, focus.text.sourceOffset) == 0 &&
           qMax(anchor.text.sourceOffset, focus.text.sourceOffset) == sourceLength;
  }

  // Collapsed means anchor and focus are one caret position. Comparing every field matters:
  // table cells share their table's blockId but have per-cell text nodes, so two cells at equal
  // offsets are NOT one position.
  bool isCollapsed() const { return anchor == focus; }

  bool isSingleBlock() const { return anchor.blockId.isValid() && anchor.blockId == focus.blockId; }

  // True when anchor and focus address the same text node — the ONLY case where
  // startOffset()/endOffset() (min/max of the two textOffsets) are comparable quantities.
  // Different cells of a table share a blockId but not a text node; their offsets must never
  // be min/max'd into one range.
  bool isSingleTextNode() const {
    return anchor.blockId.isValid() && anchor.blockId == focus.blockId && anchor.text.nodeId == focus.text.nodeId;
  }

  qsizetype startOffset() const { return qMin(anchor.text.textOffset, focus.text.textOffset); }

  qsizetype endOffset() const { return qMax(anchor.text.textOffset, focus.text.textOffset); }
};

} // namespace muffin
