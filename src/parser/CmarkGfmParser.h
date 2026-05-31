#pragma once

#include "parser/MarkdownParser.h"

extern "C" {
#include "cmark-gfm-extension_api.h"
}

namespace muffin {

class CmarkGfmParser final : public MarkdownParser {
public:
  CmarkGfmParser();

  ParseResult parseDocument(QStringView markdown, const ParseOptions& options) override;
  ParseResult parseBlock(QStringView markdown, BlockType context, const ParseOptions& options) override;

private:
  void ensureExtensionsRegistered();
  void attachExtensions(cmark_parser* parser, const ParseOptions& options);
  void insertVirtualEmptyParagraphs(QStringView markdown, MarkdownNode& root) const;
  std::unique_ptr<MarkdownNode> createVirtualEmptyParagraph(int line) const;
};

}  // namespace muffin
