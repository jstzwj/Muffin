#include "document/SourceRangeUtil.h"

#include "document/MarkdownNode.h"

namespace muffin {

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown) {
  SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return range;
  }

  if (node.type() == BlockType::MathBlock) {
    if (range.byteEnd + 3 <= markdown.size() && markdown.mid(range.byteEnd, 3) == QStringLiteral("\n$$")) {
      range.byteEnd += 3;
    } else if (range.byteEnd + 2 <= markdown.size() && markdown.mid(range.byteEnd, 2) == QStringLiteral("$$")) {
      range.byteEnd += 2;
    }
  }

  return range;
}

}  // namespace muffin
