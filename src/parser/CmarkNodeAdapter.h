#pragma once

#include "document/MarkdownNode.h"

#include <QString>

extern "C" {
#include "cmark-gfm.h"
}

namespace muffin {

class LineStartOffsetCache;

class CmarkNodeAdapter {
public:
  CmarkNodeAdapter() = default;
  CmarkNodeAdapter(const LineStartOffsetCache* lineOffsets, QStringView markdown);

  std::unique_ptr<MarkdownNode> convertBlock(cmark_node* node);
  InlineNode convertInline(cmark_node* node);

private:
  BlockType mapBlockType(cmark_node* node) const;
  InlineType mapInlineType(cmark_node* node) const;
  SourceRange readSourceRange(cmark_node* node) const;
  QVector<InlineNode> convertInlineChildren(cmark_node* node);
  void annotateInlineSource(cmark_node* cmarkNode, InlineNode& inlineNode) const;
  void readBlockMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode);
  void readTableMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode);

  const LineStartOffsetCache* lineOffsets_ = nullptr;
  QStringView markdown_;
};

}  // namespace muffin
