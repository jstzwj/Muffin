#pragma once

#include "document/MarkdownNode.h"

extern "C" {
#include "cmark-gfm.h"
}

namespace muffin {

class CmarkNodeAdapter {
public:
  std::unique_ptr<MarkdownNode> convertBlock(cmark_node* node);
  InlineNode convertInline(cmark_node* node);

private:
  BlockType mapBlockType(cmark_node* node) const;
  InlineType mapInlineType(cmark_node* node) const;
  SourceRange readSourceRange(cmark_node* node) const;
  QVector<InlineNode> convertInlineChildren(cmark_node* node);
  void readBlockMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode);
  void readTableMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode);
};

}  // namespace muffin
