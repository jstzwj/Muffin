#pragma once

#include "document/TextSelection.h"

#include <QRectF>

namespace muffin {

struct HitTestResult {
  enum class DefinitionField {
    None,
    Label,
    Destination,
    Title,
    Note
  };

  enum class Zone {
    None,
    Block,
    Text,
    Marker,
    TableCell,
    Code,
    Math,
    Html,
    FrontMatter,
    BlockAfter
  };

  Zone zone = Zone::None;
  NodeId blockId;
  NodeId textNodeId;
  qsizetype textOffset = 0;
  qsizetype sourceOffset = -1;
  QRectF blockRect;
  QRectF cursorRect;
  int tableRow = -1;
  int tableColumn = -1;
  DefinitionField definitionField = DefinitionField::None;
  QString linkHref;
  QString imageSrc;
  // True when the click landed on a task-list item's checkbox affordance (the
  // marker gutter of a task item). Consumed by the view to toggle the item
  // instead of placing the caret.
  bool taskCheckboxHit = false;

  bool isValid() const {
    return blockId.isValid();
  }

  CursorPosition cursorPosition() const {
    CursorPosition position;
    position.blockId = blockId;
    position.text.nodeId = textNodeId.isValid() ? textNodeId : blockId;
    position.text.textOffset = textOffset;
    position.text.sourceOffset = sourceOffset;
    position.text.inMeta = zone == Zone::Marker;
    position.afterBlock = zone == Zone::BlockAfter;
    return position;
  }
};

}  // namespace muffin
