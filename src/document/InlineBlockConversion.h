#pragma once

#include "Export.h"
#include "document/InlineNode.h"
#include "document/SourceTextChange.h"
#include "parser/MarkdownParser.h"

namespace muffin {
class MarkdownNode;
struct InlineBlockFormat {
  enum Kind { Paragraph, Heading, BulletList, OrderedList, TaskList, Quote, Code } kind = Paragraph;
  int level = 1;
  bool setext = false;
  QChar bullet = u'-';
};
struct InlineBlockConversion {
  InlineRange range;
  QString text;
  SourceTextChange change;
  bool valid = false;
};
// Convert an inline-bearing block without serializing its inlines: reference
// links, escapes and authored formatting retain their exact source spelling.
// Both explicit block targets and caret-based commands use this operation.
MUFFIN_CORE_EXPORT InlineBlockConversion convertInlineBlock(const PieceTable& text, const MarkdownNode& node,
                                                            InlineBlockFormat format, const ParseOptions& options);
}
