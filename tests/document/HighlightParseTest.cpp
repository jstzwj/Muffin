#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

// ==highlight== (Pandoc-style). cmark-gfm has no highlight extension, so `==text==` arrives as
// literal Text and a post-parse pass (gated on enableHighlight) splits it into Highlight inlines.
// Faithful to Pandoc: only a run of exactly two `=` is a marker; `===`/lone `=` and a
// backslash-escaped `==` stay literal.

namespace {
int countInline(const QVector<InlineNode>& inlines, InlineType type) {
  int count = 0;
  for (const InlineNode& node : inlines) {
    if (node.type() == type) {
      ++count;
    }
    count += countInline(node.children(), type);
  }
  return count;
}

int countInline(const MarkdownNode& node, InlineType type) {
  int count = countInline(node.inlines(), type);
  for (const auto& child : node.children()) {
    count += countInline(*child, type);
  }
  return count;
}

const InlineNode* findInline(const QVector<InlineNode>& inlines, InlineType type) {
  for (const InlineNode& node : inlines) {
    if (node.type() == type) {
      return &node;
    }
    if (const InlineNode* found = findInline(node.children(), type)) {
      return found;
    }
  }
  return nullptr;
}

const InlineNode* findInline(const MarkdownNode& node, InlineType type) {
  if (const InlineNode* found = findInline(node.inlines(), type)) {
    return found;
  }
  for (const auto& child : node.children()) {
    if (const InlineNode* found = findInline(*child, type)) {
      return found;
    }
  }
  return nullptr;
}
}  // namespace

void testHighlightParses() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableHighlight = true;
  ParseResult result = parser.parseDocument(QStringLiteral("a ==key== b"), options);
  require(result.root != nullptr, "parse should produce a root");
  require(countInline(*result.root, InlineType::Highlight) == 1, "should find one highlight inline");
  const InlineNode* hl = findInline(*result.root, InlineType::Highlight);
  require(hl != nullptr, "highlight node should exist");
  require(hl->marker() == QStringLiteral("=="), "highlight marker should be ==");
  require(countInline(hl->children(), InlineType::Text) >= 1, "highlight should wrap the inner text");
  require(hl->children().size() == 1 && hl->children().first().text() == QStringLiteral("key"),
          "highlight child text should be 'key'");
}

void testHighlightNestedFormatting() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableHighlight = true;
  ParseResult result = parser.parseDocument(QStringLiteral("==**bold**=="), options);
  require(countInline(*result.root, InlineType::Highlight) == 1, "should wrap the bold in a highlight");
  const InlineNode* hl = findInline(*result.root, InlineType::Highlight);
  require(hl != nullptr && countInline(hl->children(), InlineType::Strong) == 1,
          "highlight should contain the strong run");
}

void testHighlightDisabledStaysLiteral() {
  CmarkGfmParser parser;
  ParseOptions options;  // enableHighlight defaults false
  ParseResult result = parser.parseDocument(QStringLiteral("a ==key== b"), options);
  require(countInline(*result.root, InlineType::Highlight) == 0,
          "with enableHighlight=false the markers must stay literal");
}

void testTripleEqualsStaysLiteral() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableHighlight = true;
  ParseResult result = parser.parseDocument(QStringLiteral("a === b"), options);
  require(countInline(*result.root, InlineType::Highlight) == 0,
          "a run of three = must not form a highlight marker");
}

void testEscapedHighlightStaysLiteral() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableHighlight = true;
  ParseResult result = parser.parseDocument(QStringLiteral("\\==not=="), options);
  require(countInline(*result.root, InlineType::Highlight) == 0,
          "a backslash-escaped == must not open a highlight");
}

void testHighlightSerializesRoundTrip() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableHighlight = true;
  ParseResult result = parser.parseDocument(QStringLiteral("a ==key== b"), options);
  require(result.root != nullptr, "parse should produce a root");
  require(result.root->children().size() >= 1, "root should have a paragraph child");
  const QString serialized = MarkdownSerializer().serializeBlock(*result.root->children().at(0));
  require(serialized == QStringLiteral("a ==key== b"),
          "highlight should round-trip to ==key== on serialize");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("HighlightParseTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testHighlightParses);
  RUN_TEST(testHighlightNestedFormatting);
  RUN_TEST(testHighlightDisabledStaysLiteral);
  RUN_TEST(testTripleEqualsStaysLiteral);
  RUN_TEST(testEscapedHighlightStaysLiteral);
  RUN_TEST(testHighlightSerializesRoundTrip);
#undef RUN_TEST
  return 0;
}
