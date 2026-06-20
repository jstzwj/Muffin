// Step 1 scaffold for the block-relative offset model: exercises MarkdownNode::topLevelBlock()
// and relativizeDescendants() in isolation. NOTE: the descendant assertions check the STORED
// (relative) values via sourceRange(), which in Step 1 returns storage as-is. Once Step 2 makes
// sourceRange() resolve to absolute, these assertions flip to absolute — the differential oracle
// (RelativeOffsetOracleTest) is the stable, semantics-independent correctness net.
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"

#include "../TestUtils.h"

using namespace muffin;

void testTopLevelBlockWalksToTop() {
  MarkdownNode root(BlockType::Document);
  MarkdownNode& list = root.appendChild(std::make_unique<MarkdownNode>(BlockType::List));
  MarkdownNode& item = list.appendChild(std::make_unique<MarkdownNode>(BlockType::ListItem));
  MarkdownNode& para = item.appendChild(std::make_unique<MarkdownNode>(BlockType::Paragraph));

  require(root.topLevelBlock() == &root, "root's top-level block is itself");
  require(list.topLevelBlock() == &list, "list's top-level block is itself");
  require(item.topLevelBlock() == &list, "listItem's top-level block is the list");
  require(para.topLevelBlock() == &list, "paragraph's top-level block is the list");
}

void testRelativizeDescendantsMakesOffsetsRelative() {
  MarkdownNode root(BlockType::Document);
  MarkdownNode& list = root.appendChild(std::make_unique<MarkdownNode>(BlockType::List));
  list.setSourceRange(SourceRange{100, 130, 5, 6, 0, 0});
  MarkdownNode& item = list.appendChild(std::make_unique<MarkdownNode>(BlockType::ListItem));
  item.setSourceRange(SourceRange{105, 125, 6, 6, 0, 0});
  MarkdownNode& para = item.appendChild(std::make_unique<MarkdownNode>(BlockType::Paragraph));
  para.setSourceRange(SourceRange{108, 120, 6, 6, 0, 0});
  InlineNode inlineText = InlineNode::text(QStringLiteral("hello"));
  inlineText.setSourceRange(InlineRange{110, 115});
  para.inlines().append(std::move(inlineText));

  list.relativizeDescendants();

  // Top-level block's OWN sourceRange stays ABSOLUTE (it is the anchor).
  require(list.sourceRange().byteStart == 100, "list own byteStart stays absolute");
  require(list.sourceRange().byteEnd == 130, "list own byteEnd stays absolute");
  require(list.sourceRange().lineStart == 5, "list own lineStart stays absolute");

  // After relativize, sourceRange() RESOLVES descendants back to ABSOLUTE (storage is relative but
  // the accessor adds the top-level block's base) — round-trips to the original values.
  require(item.sourceRange().byteStart == 105, "listItem byteStart resolves to absolute");
  require(para.sourceRange().byteStart == 108, "paragraph byteStart resolves to absolute");
  require(para.sourceRange().lineStart == 6, "paragraph lineStart resolves to absolute");
  // InlineNode has no parent pointer and does NOT self-resolve: its accessor returns the STORED
  // relative value (110 - 100). Consumers add the block base explicitly.
  require(para.inlines().constFirst().sourceStart() == 10, "inline sourceStart stored relative (110 - 100)");
}

int main() {
  runTest("testTopLevelBlockWalksToTop", testTopLevelBlockWalksToTop);
  runTest("testRelativizeDescendantsMakesOffsetsRelative", testRelativizeDescendantsMakesOffsetsRelative);
  return 0;
}
