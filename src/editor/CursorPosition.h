#pragma once

#include "document/TextSelection.h"

#include <QRectF>

namespace muffin {

struct HitTestResult {
  enum class Zone {
    None,
    Block,
    Text,
    Marker,
    TableCell,
    Code,
    Math,
    Html
  };

  Zone zone = Zone::None;
  NodeId blockId;
  NodeId textNodeId;
  qsizetype textOffset = 0;
  QRectF blockRect;
  QRectF cursorRect;
  int tableRow = -1;
  int tableColumn = -1;

  bool isValid() const {
    return blockId.isValid();
  }

  CursorPosition cursorPosition() const {
    CursorPosition position;
    position.blockId = blockId;
    position.text.nodeId = textNodeId.isValid() ? textNodeId : blockId;
    position.text.textOffset = textOffset;
    position.text.inMeta = zone == Zone::Marker;
    return position;
  }
};

}  // namespace muffin
