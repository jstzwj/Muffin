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

  bool isValid() const {
    return blockId.isValid();
  }
};

struct SelectionRange {
  CursorPosition anchor;
  CursorPosition focus;

  bool isCollapsed() const {
    return anchor.blockId == focus.blockId && anchor.text.textOffset == focus.text.textOffset &&
           anchor.text.sourceOffset == focus.text.sourceOffset && anchor.text.inMeta == focus.text.inMeta;
  }

  bool isSingleBlock() const {
    return anchor.blockId.isValid() && anchor.blockId == focus.blockId;
  }

  qsizetype startOffset() const {
    return qMin(anchor.text.textOffset, focus.text.textOffset);
  }

  qsizetype endOffset() const {
    return qMax(anchor.text.textOffset, focus.text.textOffset);
  }
};

}  // namespace muffin
