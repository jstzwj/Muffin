#pragma once

#include "document/NodeId.h"
#include "document/SourceRange.h"

#include <QString>
#include <QVector>

namespace muffin {

class MarkdownDocument;

struct OutlineEntry {
  QString title;
  int level = 1;
  NodeId nodeId;
  SourceRange sourceRange;
  int parentIndex = -1;
};

QVector<OutlineEntry> buildOutline(const MarkdownDocument& document);

}  // namespace muffin
