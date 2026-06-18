#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

// Subscript `H~2~O` and superscript `X^2^` (Pandoc-style). cmark-gfm has no extension for these, so
// the markers arrive as literal Text and a post-parse pass (gated on enableSubscript/enableSuperscript)
// splits them into typed inlines. Faithful to Pandoc: only a run of exactly one `~`/`^` is a marker,
// so `~~` (strikethrough) is never consumed as subscript. A backslash-escaped marker stays literal.

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

void testSubscriptParses() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSubscript = true;
  ParseResult result = parser.parseDocument(QStringLiteral("H~2~O"), options);
  require(result.root != nullptr, "parse should produce a root");
  require(countInline(*result.root, InlineType::Subscript) == 1, "should find one subscript inline");
  const InlineNode* sub = findInline(*result.root, InlineType::Subscript);
  require(sub != nullptr, "subscript node should exist");
  require(sub->marker() == QStringLiteral("~"), "subscript marker should be ~");
  require(sub->children().size() == 1 && sub->children().first().text() == QStringLiteral("2"),
          "subscript child text should be '2'");
}

void testSuperscriptParses() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSuperscript = true;
  ParseResult result = parser.parseDocument(QStringLiteral("X^2^"), options);
  require(countInline(*result.root, InlineType::Superscript) == 1, "should find one superscript inline");
  const InlineNode* sup = findInline(*result.root, InlineType::Superscript);
  require(sup != nullptr, "superscript node should exist");
  require(sup->marker() == QStringLiteral("^"), "superscript marker should be ^");
  require(sup->children().size() == 1 && sup->children().first().text() == QStringLiteral("2"),
          "superscript child text should be '2'");
}

void testSubscriptNestedFormatting() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSubscript = true;
  ParseResult result = parser.parseDocument(QStringLiteral("~**b**~"), options);
  require(countInline(*result.root, InlineType::Subscript) == 1, "should wrap the bold in a subscript");
  const InlineNode* sub = findInline(*result.root, InlineType::Subscript);
  require(sub != nullptr && countInline(sub->children(), InlineType::Strong) == 1,
          "subscript should contain the strong run");
}

// `~~x~~` is strikethrough (a cmark extension), never subscript — the run-length-1 rule plus cmark
// consuming `~~` first means the two never collide even with both enabled.
void testStrikethroughNotConsumedAsSubscript() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSubscript = true;  // enableStrikethrough defaults true
  ParseResult result = parser.parseDocument(QStringLiteral("~~struck~~"), options);
  require(countInline(*result.root, InlineType::Strikethrough) == 1, "~~x~~ should still be strikethrough");
  require(countInline(*result.root, InlineType::Subscript) == 0, "strikethrough must not become subscript");
}

void testSubscriptDisabledStaysLiteral() {
  CmarkGfmParser parser;
  ParseOptions options;  // enableSubscript defaults false, enableStrikethrough defaults true
  ParseResult result = parser.parseDocument(QStringLiteral("H~2~O"), options);
  require(countInline(*result.root, InlineType::Subscript) == 0,
          "with enableSubscript=false the markers must stay literal");
  // Guards the single-tilde fix: cmark-gfm's strikethrough extension matches a lone `~`, so without
  // the detach it turned `~2~` into a Strikethrough. We own tilde runs ourselves now, so single `~`
  // must NOT render as strikethrough when subscript is off.
  require(countInline(*result.root, InlineType::Strikethrough) == 0,
          "with subscript off, ~2~ must not be parsed as strikethrough");
}

// Strikethrough is `~~` only (GFM spec); a single `~` is reserved for subscript / literal text.
// This holds regardless of the subscript toggle: `~~x~~` is always strikethrough, `~x~` never is.
void testStrikethroughUsesDoubleTildeOnly() {
  CmarkGfmParser parser;
  ParseOptions options;  // subscript off, strikethrough on (defaults)
  ParseResult doubleTilde = parser.parseDocument(QStringLiteral("~~struck~~"), options);
  require(countInline(*doubleTilde.root, InlineType::Strikethrough) == 1,
          "~~x~~ should still parse as strikethrough with subscript off");
  ParseResult singleTilde = parser.parseDocument(QStringLiteral("~plain~"), options);
  require(countInline(*singleTilde.root, InlineType::Strikethrough) == 0,
          "single-tilde ~x~ must not be strikethrough when subscript is off");
}

// A lone `~` flanked by spaces cannot open (its first content char is a space), so `a ~ b ~ c` must
// stay literal — consistent with emphasis/highlight flank rules.
void testSpacedTildeStaysLiteral() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSubscript = true;
  ParseResult result = parser.parseDocument(QStringLiteral("a ~ b ~ c"), options);
  require(countInline(*result.root, InlineType::Subscript) == 0,
          "a space-flanked ~ must not open a subscript");
}

void testSubscriptSerializesRoundTrip() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableSubscript = true;
  ParseResult result = parser.parseDocument(QStringLiteral("H~2~O"), options);
  require(result.root != nullptr, "parse should produce a root");
  require(result.root->children().size() >= 1, "root should have a paragraph child");
  const QString serialized = MarkdownSerializer().serializeBlock(*result.root->children().at(0));
  require(serialized == QStringLiteral("H~2~O"), "subscript should round-trip to H~2~O on serialize");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("SubscriptSuperscriptParseTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSubscriptParses);
  RUN_TEST(testSuperscriptParses);
  RUN_TEST(testSubscriptNestedFormatting);
  RUN_TEST(testStrikethroughNotConsumedAsSubscript);
  RUN_TEST(testStrikethroughUsesDoubleTildeOnly);
  RUN_TEST(testSubscriptDisabledStaysLiteral);
  RUN_TEST(testSpacedTildeStaysLiteral);
  RUN_TEST(testSubscriptSerializesRoundTrip);
#undef RUN_TEST
  return 0;
}
