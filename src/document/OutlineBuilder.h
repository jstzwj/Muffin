#pragma once

#include "document/NodeId.h"
#include "document/SourceRange.h"

#include <QString>
#include <QVector>

namespace muffin {

class MarkdownDocument;
class MarkdownNode;

struct OutlineEntry {
  QString title;
  int level = 1;
  NodeId nodeId;
  SourceRange sourceRange;
  int parentIndex = -1;
  NodeId topLevelId;
};

QVector<OutlineEntry> buildOutline(const MarkdownDocument& document);
QVector<OutlineEntry> buildOutlineFragment(const MarkdownNode& topLevel);

}  // namespace muffin
