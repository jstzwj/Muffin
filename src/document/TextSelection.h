#pragma once

#include "document/NodeId.h"

namespace muffin {

struct TextPosition {
  NodeId nodeId;
  qsizetype textOffset = 0;
  qsizetype sourceOffset = -1;
  bool inMeta = false;

  bool isValid() const {
    return nodeId.isValid();
  }
};

struct CursorPosition {
  NodeId blockId;
  TextPosition text;
  // True when the caret sits on the virtual trailing empty paragraph below
  // this block (Zone::BlockAfter). Persisted so the trailing caret survives
  // layout rebuilds; never set for a caret inside real block content.
  bool afterBlock = false;

  bool isValid() const {
    return blockId.isValid();
  }
};

struct SelectionRange {
  CursorPosition anchor;
  CursorPosition focus;

  // Collapsed means ONE caret position. nodeId must be compared too: table cells share their
  // table's blockId but have per-cell text nodes, so two cells at equal offsets are NOT one
  // position (the historical omission made cross-cell selections masquerade as collapsed).
  bool isCollapsed() const {
    return anchor.blockId == focus.blockId && anchor.text.nodeId == focus.text.nodeId &&
           anchor.text.textOffset == focus.text.textOffset &&
           anchor.text.sourceOffset == focus.text.sourceOffset && anchor.text.inMeta == focus.text.inMeta &&
           anchor.afterBlock == focus.afterBlock;
  }

  bool isSingleBlock() const {
    return anchor.blockId.isValid() && anchor.blockId == focus.blockId;
  }

  // True when anchor and focus address the same text node — the ONLY case where
  // startOffset()/endOffset() (min/max of the two textOffsets) are comparable quantities.
  // Different cells of a table share a blockId but not a text node; their offsets must never
  // be min/max'd into one range.
  bool isSingleTextNode() const {
    return anchor.blockId.isValid() && anchor.blockId == focus.blockId && anchor.text.nodeId == focus.text.nodeId;
  }

  qsizetype startOffset() const {
    return qMin(anchor.text.textOffset, focus.text.textOffset);
  }

  qsizetype endOffset() const {
    return qMax(anchor.text.textOffset, focus.text.textOffset);
  }
};

}  // namespace muffin
