#pragma once

#include "parser/MarkdownParser.h"

extern "C" {
#include "cmark-gfm-extension_api.h"
}

namespace muffin {

class LineStartOffsetCache;

class CmarkGfmParser final : public MarkdownParser {
public:
  CmarkGfmParser();

  ParseResult parseDocument(QStringView markdown, const ParseOptions& options) override;
  ParseResult parseDocumentProfiled(QStringView markdown, const ParseOptions& options);
  ParseResult parseBlock(QStringView markdown, BlockType context, const ParseOptions& options) override;

private:
  ParseResult parseDocumentImpl(
      QStringView markdown,
      const ParseOptions& options,
      bool collectPerformanceMetrics);
  void ensureExtensionsRegistered();
  void attachExtensions(cmark_parser* parser, const ParseOptions& options);
  void insertVirtualEmptyParagraphs(QStringView markdown, MarkdownNode& root, const LineStartOffsetCache& lineOffsets) const;
  void insertVirtualEmptyParagraphsInBlockQuotes(QStringView markdown, MarkdownNode& root, const LineStartOffsetCache& lineOffsets) const;
  void insertVirtualEmptyParagraphsInLists(QStringView markdown, MarkdownNode& root, const LineStartOffsetCache& lineOffsets) const;
  std::unique_ptr<MarkdownNode> createVirtualEmptyParagraph(int line) const;
  std::unique_ptr<MarkdownNode> createVirtualEmptyParagraph(int line, int column, qsizetype sourceOffset) const;
};

}  // namespace muffin
