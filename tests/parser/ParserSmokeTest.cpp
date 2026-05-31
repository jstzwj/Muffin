#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include <QCoreApplication>
#include <QDebug>

using namespace muffin;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) {
    fail(message);
  }
}

const MarkdownNode& childAt(const MarkdownNode& node, qsizetype index) {
  require(index >= 0 && index < node.children().size(), QStringLiteral("Missing child %1").arg(index));
  return *node.children()[index];
}

void testBasicParseAndSerialize() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "# Title\n\n"
      "Hello **bold** and ~~gone~~.\n\n"
      "| A | B |\n"
      "| :- | -: |\n"
      "| 1 | 2 |\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));
  require(parsed.root->type() == BlockType::Document, QStringLiteral("Root is not a document"));
  require(parsed.root->children().size() == 3, QStringLiteral("Unexpected root child count"));

  const MarkdownNode& heading = childAt(*parsed.root, 0);
  require(heading.type() == BlockType::Heading, QStringLiteral("First block is not heading"));
  require(heading.headingLevel() == 1, QStringLiteral("Heading level was not preserved"));
  require(!heading.inlines().isEmpty() && heading.inlines().front().text() == QStringLiteral("Title"),
          QStringLiteral("Heading text was not parsed"));

  const MarkdownNode& paragraph = childAt(*parsed.root, 1);
  require(paragraph.type() == BlockType::Paragraph, QStringLiteral("Second block is not paragraph"));
  bool sawStrong = false;
  bool sawStrike = false;
  for (const InlineNode& inlineNode : paragraph.inlines()) {
    sawStrong = sawStrong || inlineNode.type() == InlineType::Strong;
    sawStrike = sawStrike || inlineNode.type() == InlineType::Strikethrough;
  }
  require(sawStrong, QStringLiteral("Strong inline was not parsed"));
  require(sawStrike, QStringLiteral("Strikethrough inline was not parsed"));

  const MarkdownNode& table = childAt(*parsed.root, 2);
  require(table.type() == BlockType::Table, QStringLiteral("Third block is not table"));
  require(table.tableAlignments().size() == 2, QStringLiteral("Table alignments were not parsed"));
  require(table.tableAlignments()[0] == TableAlignment::Left, QStringLiteral("Left alignment missing"));
  require(table.tableAlignments()[1] == TableAlignment::Right, QStringLiteral("Right alignment missing"));

  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QStringLiteral("# Title")), QStringLiteral("Serialized heading missing"));
  require(serialized.contains(QStringLiteral("**bold**")), QStringLiteral("Serialized strong missing"));
  require(serialized.contains(QStringLiteral("~~gone~~")), QStringLiteral("Serialized strikethrough missing"));
  require(serialized.contains(QStringLiteral("| :--- | ---: |")),
          QStringLiteral("Serialized table delimiter missing"));
}

void testMathIsNotSupportedYet() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("Inline $x^2$ stays text.\n\n$$\ny=x\n$$\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for math sample"));

  bool sawInlineMath = false;
  bool sawMathBlock = false;
  for (const auto& block : parsed.root->children()) {
    sawMathBlock = sawMathBlock || block->type() == BlockType::MathBlock;
    for (const InlineNode& inlineNode : block->inlines()) {
      sawInlineMath = sawInlineMath || inlineNode.type() == InlineType::InlineMath;
    }
  }

  require(!sawInlineMath, QStringLiteral("Unexpected inline math support detected"));
  require(!sawMathBlock, QStringLiteral("Unexpected math block support detected"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testBasicParseAndSerialize();
  testMathIsNotSupportedYet();
  return 0;
}
