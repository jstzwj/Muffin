// Virtual empty paragraphs (VEPs).
//
// Between the children of a container, every two empty lines stand for one blank line the
// author can click into and type on. The paragraph that makes that line editable does not exist
// in the Markdown — the parser inserts it. The rule was written three times, once for the
// document, for block quotes and for lists, and the copies drifted: a correction made to the
// top-level copy (count the lines that are really blank, because cmark under-reports the end of
// a display-math block, and use the block's last non-blank line, because cmark over-reports the
// end of a thematic break) never reached the block-quote copy.

#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include <QCoreApplication>

#include "ParserTestUtils.h"

using namespace muffin;

namespace {

// What the VEP passes insert: a paragraph holding a single empty text inline, at a zero-width
// source range.
bool isVirtualEmptyParagraph(const MarkdownNode& node) {
  return node.type() == BlockType::Paragraph && node.inlines().size() == 1 &&
         node.inlines().front().type() == InlineType::Text && node.inlines().front().text().isEmpty() &&
         node.sourceRange().byteStart >= 0 && node.sourceRange().byteEnd == node.sourceRange().byteStart;
}

qsizetype countVirtualEmptyParagraphs(const MarkdownNode& node) {
  qsizetype count = isVirtualEmptyParagraph(node) ? 1 : 0;
  for (const auto& child : node.children())
    count += countVirtualEmptyParagraphs(*child);
  return count;
}

struct Parsed {
  ParseResult result;
  const MarkdownNode* root = nullptr;
};

Parsed parse(const QString& markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  Parsed parsed;
  parsed.result = parser.parseDocument(markdown, options);
  parsed.root = parsed.result.root.get();
  return parsed;
}

const MarkdownNode& onlyChild(const MarkdownNode& node, const QString& label) {
  require(node.children().size() == 1, QStringLiteral("%1: expected one child, got %2").arg(label).arg(node.children().size()));
  return *node.children().front();
}

// The rule at the top level: one empty paragraph per two blank lines, counting only lines that
// are really blank and the content end of the block before them.
void testTopLevelCountsRealBlankLines() {
  struct Case {
    QString markdown;
    qsizetype expected;
    const char* label;
  };
  const QVector<Case> cases{
      {QStringLiteral("a\n\nb"), 0, "one blank line"},
      {QStringLiteral("a\n\n\nb"), 1, "two blank lines"},
      {QStringLiteral("a\n\n\n\n\nb"), 2, "four blank lines"},
      // cmark leaves the closing $$ line out of the math block's range; it is still not blank.
      {QStringLiteral("$$\nx\n$$\n\nb"), 0, "math block then one blank line"},
      {QStringLiteral("$$\nx\n$$\n\n\nb"), 1, "math block then two blank lines"},
      // cmark reports a thematic break's end behind its trailing blank lines.
      {QStringLiteral("---\n\n\nb"), 1, "thematic break then two blank lines"},
  };
  for (const Case& item : cases) {
    const Parsed parsed = parse(item.markdown);
    require(parsed.root != nullptr, QStringLiteral("%1: the document should parse").arg(item.label));
    require(countVirtualEmptyParagraphs(*parsed.root) == item.expected,
            QStringLiteral("top level, %1: expected %2 empty paragraphs, got %3")
                .arg(item.label).arg(item.expected).arg(countVirtualEmptyParagraphs(*parsed.root)));
  }
}

// Inside a block quote the empty line is a line holding the quote marker alone. The rule is the
// top-level one, applied to those lines.
void testBlockQuoteFollowsTheTopLevelRule() {
  struct Case {
    QString markdown;
    qsizetype expected;
    const char* label;
  };
  const QVector<Case> cases{
      {QStringLiteral("> a\n>\n> b"), 0, "one empty quote line"},
      {QStringLiteral("> a\n>\n>\n> b"), 1, "two empty quote lines"},
      {QStringLiteral("> a\n>\n>\n>\n> b"), 1, "three empty quote lines"},
      {QStringLiteral("> a\n>\n>\n>\n>\n> b"), 2, "four empty quote lines"},
      {QStringLiteral("> $$\n> x\n> $$\n>\n> b"), 0, "math block then one empty quote line"},
      {QStringLiteral("> $$\n> x\n> $$\n>\n>\n> b"), 1, "math block then two empty quote lines"},
      {QStringLiteral("> ---\n>\n>\n> b"), 1, "thematic break then two empty quote lines"},
  };
  for (const Case& item : cases) {
    const Parsed parsed = parse(item.markdown);
    require(parsed.root != nullptr, QStringLiteral("%1: the document should parse").arg(item.label));
    const MarkdownNode& quote = onlyChild(*parsed.root, QStringLiteral("block quote, %1").arg(item.label));
    require(quote.type() == BlockType::BlockQuote, QStringLiteral("block quote, %1: expected one block quote").arg(item.label));
    const qsizetype count = countVirtualEmptyParagraphs(quote);
    require(count == item.expected, QStringLiteral("block quote, %1: expected %2 empty paragraphs, got %3")
                                        .arg(item.label).arg(item.expected).arg(count));
  }
}

// Two blank lines between list items are how the editor writes a list split; one blank line is
// just a loose list.
void testListKeepsItsRule() {
  {
    const Parsed parsed = parse(QStringLiteral("- a\n\n- b"));
    const MarkdownNode& list = onlyChild(*parsed.root, QStringLiteral("loose list"));
    require(list.type() == BlockType::List && countVirtualEmptyParagraphs(list) == 0,
            QStringLiteral("a loose list gets no empty paragraph"));
  }
  {
    const Parsed parsed = parse(QStringLiteral("- a\n\n\n- b"));
    const MarkdownNode& list = onlyChild(*parsed.root, QStringLiteral("split list"));
    require(list.type() == BlockType::List && countVirtualEmptyParagraphs(list) == 1,
            QStringLiteral("two blank lines between items give one empty paragraph"));
  }
  {
    const Parsed parsed = parse(QStringLiteral("- a\n\n\n\n\n- b"));
    const MarkdownNode& list = onlyChild(*parsed.root, QStringLiteral("wide split list"));
    require(list.type() == BlockType::List && countVirtualEmptyParagraphs(list) == 2,
            QStringLiteral("four blank lines between items give two empty paragraphs"));
  }
}

// A container nested in another one applies the rule to its own lines: a quote inside a list
// item gets its empty paragraphs behind the list item's indentation.
void testNestedQuoteInsideAListAppliesTheQuoteRule() {
  const Parsed parsed = parse(QStringLiteral("- > a\n  >\n  >\n  > b"));
  const MarkdownNode& list = onlyChild(*parsed.root, QStringLiteral("quote in a list"));
  require(list.type() == BlockType::List, QStringLiteral("expected a list holding the quote"));
  const MarkdownNode& item = onlyChild(list, QStringLiteral("quote in a list"));
  const MarkdownNode& quote = onlyChild(item, QStringLiteral("quote in a list"));
  require(quote.type() == BlockType::BlockQuote, QStringLiteral("expected the quote inside the item"));
  const qsizetype count = countVirtualEmptyParagraphs(quote);
  require(count == 1, QStringLiteral("a quote inside a list item follows the quote rule (got %1)").arg(count));
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTopLevelCountsRealBlankLines);
  RUN_TEST(testBlockQuoteFollowsTheTopLevelRule);
  RUN_TEST(testListKeepsItsRule);
  RUN_TEST(testNestedQuoteInsideAListAppliesTheQuoteRule);
#undef RUN_TEST
  return 0;
}
