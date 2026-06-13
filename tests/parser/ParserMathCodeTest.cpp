#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include "ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

QString serializeMarkdown(QString markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  MarkdownSerializer serializer;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for serialization helper"));
  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  return serializer.serializeDocument(document);
}

void testMathSupport() {
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

  require(sawInlineMath, QStringLiteral("Inline math was not parsed"));
  require(sawMathBlock, QStringLiteral("Math block was not parsed"));

  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QStringLiteral("$x^2$")), QStringLiteral("Serialized inline math missing"));
  require(serialized.contains(QStringLiteral("$$\ny=x\n$$")), QStringLiteral("Serialized math block missing"));
}

void testMathInHeadingsAndTables() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "# H1 $a_1$\n"
      "## H2 $a_2$\n"
      "### H3 $a_3$\n"
      "#### H4 $a_4$\n"
      "##### H5 $a_5$\n"
      "###### H6 $a_6$\n\n"
      "| Formula | Value |\n"
      "| --- | ---: |\n"
      "| $x+y$ | $42$ |\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for heading/table math sample"));
  require(parsed.root->children().size() == 7, QStringLiteral("Unexpected heading/table block count"));

  for (int i = 0; i < 6; ++i) {
    const MarkdownNode& heading = childAt(*parsed.root, i);
    require(heading.type() == BlockType::Heading, QStringLiteral("Expected heading block"));
    require(heading.headingLevel() == i + 1, QStringLiteral("Heading level mismatch"));
    require(countInlineMath(heading) == 1,
            QStringLiteral("Heading level %1 math was not parsed").arg(i + 1));
    require(containsInlineMathText(heading, QStringLiteral("a_%1").arg(i + 1)),
            QStringLiteral("Heading level %1 math text mismatch").arg(i + 1));
  }

  const MarkdownNode& table = childAt(*parsed.root, 6);
  require(table.type() == BlockType::Table, QStringLiteral("Expected table block"));
  require(countInlineMath(table) == 2, QStringLiteral("Table inline math was not parsed"));
  require(containsInlineMathText(table, QStringLiteral("x+y")), QStringLiteral("Table formula cell missing"));
  require(containsInlineMathText(table, QStringLiteral("42")), QStringLiteral("Table value cell missing"));

  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QStringLiteral("# H1 $a_1$")), QStringLiteral("Serialized H1 math missing"));
  require(serialized.contains(QStringLiteral("###### H6 $a_6$")), QStringLiteral("Serialized H6 math missing"));
  require(serialized.contains(QStringLiteral("| $x+y$ | $42$ |")),
          QStringLiteral("Serialized table math missing"));
}

void testMathEdgeCases() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "Escaped \\$x$ stays literal.\n\n"
      "Spaced opening $ x$ stays literal.\n\n"
      "Spaced closing $x $ stays literal.\n\n"
      "Unclosed $x stays literal.\n\n"
      "Double dollars $$x$$ stay literal inline.\n\n"
      "$$\n"
      "a &= b + c\n"
      "d &= e\n"
      "$$\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for math edge cases"));
  require(countInlineMath(*parsed.root) == 0, QStringLiteral("Invalid inline math edge case was parsed"));
  require(countMathBlocks(*parsed.root) == 1, QStringLiteral("Multiline math block was not parsed"));

  const MarkdownNode& mathBlock = childAt(*parsed.root, 5);
  require(mathBlock.type() == BlockType::MathBlock, QStringLiteral("Expected final math block"));
  require(mathBlock.literal().contains(QStringLiteral("a &= b + c")),
          QStringLiteral("Math block first line missing"));
  require(mathBlock.literal().contains(QStringLiteral("d &= e")),
          QStringLiteral("Math block second line missing"));

  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QStringLiteral("$$\na &= b + c\nd &= e\n$$")),
          QStringLiteral("Serialized multiline math block missing"));
}

void testLegacyBracketMathBlock() {
  CmarkGfmParser parser;
  ParseOptions options;

  const QString markdown = QStringLiteral(
      "\\[\n"
      "y = x\n"
      "\\]\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for bracket math sample"));
  require(countMathBlocks(*parsed.root) == 1, QStringLiteral("Bracket math block was not parsed"));
  const MarkdownNode& mathBlock = childAt(*parsed.root, 0);
  require(mathBlock.type() == BlockType::MathBlock, QStringLiteral("Expected bracket math block"));
  require(mathBlock.mathDelimiter() == MathDelimiter::Bracket, QStringLiteral("Bracket math delimiter flag missing"));
  require(mathBlock.literal() == QStringLiteral("y = x"), QStringLiteral("Bracket math literal mismatch"));

  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QStringLiteral("\\[\ny = x\n\\]")), QStringLiteral("Serialized bracket math block missing"));

  const QString twice = serializeMarkdown(serialized);
  require(twice == serialized, QStringLiteral("Bracket math serialization is not stable"));

  const QString dollar = QStringLiteral("$$\ny = x\n$$\n");
  ParseResult dollarParsed = parser.parseDocument(dollar, options);
  require(countMathBlocks(*dollarParsed.root) == 1, QStringLiteral("Dollar math block was not parsed"));
  require(childAt(*dollarParsed.root, 0).mathDelimiter() == MathDelimiter::Dollar,
          QStringLiteral("Dollar math delimiter should remain Dollar"));
}

void testLegacyBracketMathInsideCodeFenceUntouched() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "```\n"
      "\\[\n"
      "not math\n"
      "\\]\n"
      "```\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for fenced code bracket sample"));
  require(countMathBlocks(*parsed.root) == 0, QStringLiteral("Bracket lines inside a fenced code block must not become math"));
  const MarkdownNode& code = childAt(*parsed.root, 0);
  require(code.type() == BlockType::CodeFence, QStringLiteral("Expected fenced code block"));
  require(code.literal().contains(QStringLiteral("\\[")), QStringLiteral("Bracket opener inside code fence should be preserved verbatim"));
}

void testLegacyBracketMathInsideDollarMathUntouched() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\[\n"
      "not text\n"
      "\\]\n"
      "$$\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for $$ math bracket sample"));
  require(countMathBlocks(*parsed.root) == 1, QStringLiteral("Expected exactly one math block around $$ delimiters"));
  const MarkdownNode& mathBlock = childAt(*parsed.root, 0);
  require(mathBlock.type() == BlockType::MathBlock, QStringLiteral("Expected math block"));
  require(mathBlock.literal().contains(QStringLiteral("\\[")),
          QStringLiteral("\\[ inside $$ math block should be preserved verbatim"));
  require(mathBlock.literal().contains(QStringLiteral("\\]")),
          QStringLiteral("\\] inside $$ math block should be preserved verbatim"));
}

void testLegacyBracketMathStateMachineEdges() {
  CmarkGfmParser parser;
  ParseOptions options;

  const QString unmatchedClose = QStringLiteral(
      "\\]\n"
      "not math\n");
  ParseResult unmatchedParsed = parser.parseDocument(unmatchedClose, options);
  require(unmatchedParsed.root != nullptr, QStringLiteral("Parser returned null root for unmatched bracket close"));
  require(countMathBlocks(*unmatchedParsed.root) == 0, QStringLiteral("Unmatched \\] must not open a math block"));

  const QString dollarInsideBracket = QStringLiteral(
      "\\[\n"
      "$$\n"
      "still math\n"
      "\\]\n");
  ParseResult dollarParsed = parser.parseDocument(dollarInsideBracket, options);
  require(dollarParsed.root != nullptr, QStringLiteral("Parser returned null root for dollar-inside-bracket math"));
  require(countMathBlocks(*dollarParsed.root) == 1, QStringLiteral("Dollar line inside bracket math must not split the block"));
  const MarkdownNode& dollarMath = childAt(*dollarParsed.root, 0);
  require(dollarMath.type() == BlockType::MathBlock, QStringLiteral("Expected bracket math block"));
  require(dollarMath.mathDelimiter() == MathDelimiter::Bracket, QStringLiteral("Bracket delimiter flag missing"));
  require(dollarMath.literal().contains(QStringLiteral("$$")), QStringLiteral("Dollar line should remain math literal"));
  require(dollarMath.literal().contains(QStringLiteral("still math")), QStringLiteral("Bracket math body was truncated"));

  const QString bracketInsideBracket = QStringLiteral(
      "\\[\n"
      "\\[\n"
      "inner text\n"
      "\\]\n");
  ParseResult bracketParsed = parser.parseDocument(bracketInsideBracket, options);
  require(bracketParsed.root != nullptr, QStringLiteral("Parser returned null root for bracket-inside-bracket math"));
  require(countMathBlocks(*bracketParsed.root) == 1, QStringLiteral("Nested-looking \\[ line must stay inside bracket math body"));
  const MarkdownNode& bracketMath = childAt(*bracketParsed.root, 0);
  require(bracketMath.literal().contains(QStringLiteral("\\[")), QStringLiteral("Inner \\[ should remain literal text"));
  require(bracketMath.literal().contains(QStringLiteral("inner text")), QStringLiteral("Bracket math body missing inner text"));
}

void testLegacyBracketMathWithLeadingSpaces() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "   \\[\n"
      "y = x\n"
      "   \\]\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for indented bracket math sample"));
  require(countMathBlocks(*parsed.root) == 1, QStringLiteral("Indented bracket math block was not parsed"));
  const MarkdownNode& mathBlock = childAt(*parsed.root, 0);
  require(mathBlock.type() == BlockType::MathBlock, QStringLiteral("Expected indented bracket math block"));
  require(mathBlock.mathDelimiter() == MathDelimiter::Bracket, QStringLiteral("Indented bracket math delimiter flag missing"));
  require(mathBlock.literal() == QStringLiteral("y = x"), QStringLiteral("Indented bracket math literal mismatch"));
}

void testLegacyBracketMathInBlockQuoteAndList() {
  CmarkGfmParser parser;
  ParseOptions options;

  const QString quoteMarkdown = QStringLiteral(
      "> \\[\n"
      "> y = x\n"
      "> \\]\n");
  ParseResult quoteParsed = parser.parseDocument(quoteMarkdown, options);
  require(quoteParsed.root != nullptr, QStringLiteral("Parser returned null root for quote bracket math sample"));
  require(countMathBlocks(*quoteParsed.root) == 1, QStringLiteral("Block quote bracket math block was not parsed"));
  const MarkdownNode& quote = childAt(*quoteParsed.root, 0);
  require(quote.type() == BlockType::BlockQuote, QStringLiteral("Expected block quote wrapper"));
  const MarkdownNode& quoteMath = childAt(quote, 0);
  require(quoteMath.type() == BlockType::MathBlock, QStringLiteral("Expected bracket math inside block quote"));
  require(quoteMath.mathDelimiter() == MathDelimiter::Bracket, QStringLiteral("Quote bracket math delimiter flag missing"));
  require(quoteMath.literal() == QStringLiteral("y = x"),
          QStringLiteral("Quote bracket math literal mismatch: '%1'").arg(quoteMath.literal()));

  const QString listMarkdown = QStringLiteral(
      "- \\[\n"
      "  y = x\n"
      "  \\]\n");
  ParseResult listParsed = parser.parseDocument(listMarkdown, options);
  require(listParsed.root != nullptr, QStringLiteral("Parser returned null root for list bracket math sample"));
  require(countMathBlocks(*listParsed.root) == 1, QStringLiteral("List bracket math block was not parsed"));
  const MarkdownNode& list = childAt(*listParsed.root, 0);
  require(list.type() == BlockType::List, QStringLiteral("Expected list wrapper"));
  const MarkdownNode& listItem = childAt(list, 0);
  require(countMathBlocks(listItem) == 1, QStringLiteral("Expected bracket math inside list item"));
  const MarkdownNode& listMath = childAt(listItem, 0);
  require(listMath.type() == BlockType::MathBlock, QStringLiteral("Expected bracket math as first list item child"));
  require(listMath.literal() == QStringLiteral("y = x"),
          QStringLiteral("List bracket math literal mismatch: '%1'").arg(listMath.literal()));
}

void testFencedCodeBlock() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "```powershell\n"
      "conan install . -s build_type=Release\n"
      "cmake --build --preset conan-release\n"
      "```\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for code fence sample"));
  require(parsed.root->children().size() == 1, QStringLiteral("Unexpected code fence block count"));

  const MarkdownNode& codeBlock = childAt(*parsed.root, 0);
  require(codeBlock.type() == BlockType::CodeFence, QStringLiteral("Expected fenced code block"));
  require(codeBlock.codeLanguage() == QStringLiteral("powershell"), QStringLiteral("Fence language missing"));
  require(codeBlock.literal().contains(QStringLiteral("conan install")),
          QStringLiteral("Fence literal missing first command"));
  require(codeBlock.literal().contains(QStringLiteral("cmake --build")),
          QStringLiteral("Fence literal missing second command"));
}

void testCodeFenceSerializationDoesNotGrowTrailingBlankLines() {
  CmarkGfmParser parser;
  ParseOptions options;
  MarkdownSerializer serializer;
  const QString markdown = QStringLiteral("```cpp\nreturn 0;\n```");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for code fence growth sample"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  const QString once = serializer.serializeDocument(document);
  require(once == markdown, QStringLiteral("Code fence first serialization added blank lines"));

  ParseResult reparsed = parser.parseDocument(once, options);
  require(reparsed.root != nullptr, QStringLiteral("Parser returned null root after code fence serialization"));
  MarkdownDocument reparsedDocument;
  reparsedDocument.setMarkdownText(once, std::move(reparsed.root));
  const QString twice = serializer.serializeDocument(reparsedDocument);
  require(twice == once, QStringLiteral("Code fence repeated serialization added blank lines"));
}

void testYamlFrontMatter() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "---\n"
      "title: Muffin\n"
      "tags:\n"
      "  - markdown\n"
      "---\n"
      "# Heading\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for YAML front matter"));
  require(parsed.root->children().size() == 2, QStringLiteral("Unexpected YAML front matter child count"));

  const MarkdownNode& frontMatter = childAt(*parsed.root, 0);
  require(frontMatter.type() == BlockType::FrontMatter, QStringLiteral("Expected YAML front matter block"));
  require(frontMatter.frontMatterFormat() == FrontMatterFormat::Yaml, QStringLiteral("YAML front matter format mismatch"));
  require(frontMatter.literal().contains(QStringLiteral("title: Muffin")), QStringLiteral("YAML front matter literal missing title"));
  require(frontMatter.sourceRange().byteStart == 0, QStringLiteral("YAML front matter source should start at zero"));
  require(sourceTextForNode(markdown, frontMatter).startsWith(QStringLiteral("---\n")), QStringLiteral("YAML front matter source missing opening fence"));
  require(childAt(*parsed.root, 1).type() == BlockType::Heading, QStringLiteral("YAML front matter should preserve following heading"));
}

void testTomlFrontMatter() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "+++\n"
      "title = \"Muffin\"\n"
      "draft = false\n"
      "+++\n"
      "Body\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for TOML front matter"));
  require(parsed.root->children().size() == 2, QStringLiteral("Unexpected TOML front matter child count"));

  const MarkdownNode& frontMatter = childAt(*parsed.root, 0);
  require(frontMatter.type() == BlockType::FrontMatter, QStringLiteral("Expected TOML front matter block"));
  require(frontMatter.frontMatterFormat() == FrontMatterFormat::Toml, QStringLiteral("TOML front matter format mismatch"));
  require(frontMatter.literal().contains(QStringLiteral("draft = false")), QStringLiteral("TOML front matter literal missing draft"));
  require(childAt(*parsed.root, 1).type() == BlockType::Paragraph, QStringLiteral("TOML front matter should preserve following paragraph"));
}

void testJsonFrontMatter() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "{\n"
      "  \"title\": \"Muffin\",\n"
      "  \"nested\": { \"ok\": true },\n"
      "  \"text\": \"brace } in string\"\n"
      "}\n"
      "# Heading\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for JSON front matter"));
  require(parsed.root->children().size() == 2, QStringLiteral("Unexpected JSON front matter child count"));

  const MarkdownNode& frontMatter = childAt(*parsed.root, 0);
  require(frontMatter.type() == BlockType::FrontMatter, QStringLiteral("Expected JSON front matter block"));
  require(frontMatter.frontMatterFormat() == FrontMatterFormat::Json, QStringLiteral("JSON front matter format mismatch"));
  require(frontMatter.literal().contains(QStringLiteral("brace } in string")), QStringLiteral("JSON front matter string scanning failed"));
  require(childAt(*parsed.root, 1).type() == BlockType::Heading, QStringLiteral("JSON front matter should preserve following heading"));
}

void testFrontMatterFalsePositives() {
  CmarkGfmParser parser;
  ParseOptions options;

  ParseResult leadingBlank = parser.parseDocument(QStringLiteral("\n---\ntitle: x\n---\n"), options);
  require(leadingBlank.root != nullptr, QStringLiteral("Parser returned null root for leading blank front matter false positive"));
  require(childAt(*leadingBlank.root, 0).type() != BlockType::FrontMatter, QStringLiteral("Front matter should require absolute document start"));

  ParseResult unclosedYaml = parser.parseDocument(QStringLiteral("---\ntitle: x\n"), options);
  require(unclosedYaml.root != nullptr, QStringLiteral("Parser returned null root for unclosed YAML front matter"));
  require(childAt(*unclosedYaml.root, 0).type() != BlockType::FrontMatter, QStringLiteral("Unclosed YAML fence should not be front matter"));

  ParseResult invalidJson = parser.parseDocument(QStringLiteral("{not json}\nBody"), options);
  require(invalidJson.root != nullptr, QStringLiteral("Parser returned null root for invalid JSON front matter"));
  require(childAt(*invalidJson.root, 0).type() != BlockType::FrontMatter, QStringLiteral("Invalid JSON should not be front matter"));

  ParseOptions disabledOptions;
  disabledOptions.enableFrontMatter = false;
  ParseResult disabled = parser.parseDocument(QStringLiteral("---\ntitle: x\n---\n"), disabledOptions);
  require(disabled.root != nullptr, QStringLiteral("Parser returned null root for disabled front matter"));
  require(childAt(*disabled.root, 0).type() != BlockType::FrontMatter, QStringLiteral("Disabled front matter option should leave cmark output"));
}

void testCodeFenceEmptySerializationIsIdempotent() {
  CmarkGfmParser parser;
  ParseOptions options;

  const QString markdown = QStringLiteral("```cpp\nreturn 0;\n```");
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for code fence literal test"));
  require(parsed.root->children().size() == 1, QStringLiteral("Expected one child for code fence literal test"));
  const MarkdownNode& codeBlock = childAt(*parsed.root, 0);
  require(codeBlock.type() == BlockType::CodeFence, QStringLiteral("Expected CodeFence block"));
  require(codeBlock.literal() == QStringLiteral("return 0;"),
          QStringLiteral("Code fence literal should not include trailing newline, got: '%1'").arg(codeBlock.literal()));

  const QVector<QString> samples{
      QStringLiteral("```cpp\nreturn 0;\n```"),
      QStringLiteral("```\nhello\n```"),
      QStringLiteral("```\nhello\n\nworld\n```"),
  };
  for (const QString& sample : samples) {
    const QString first = serializeMarkdown(sample);
    const QString second = serializeMarkdown(first);
    require(first == sample, QStringLiteral("Code fence first serialization changed source: '%1' -> '%2'").arg(sample, first));
    require(second == first, QStringLiteral("Code fence repeated serialization is not stable: '%1' -> '%2'").arg(first, second));
  }
}

void testFrontMatterSerializationDoesNotGrowTrailingBlankLines() {
  const QVector<QString> samples{
      QStringLiteral("---\ntitle: Muffin\n---"),
      QStringLiteral("---\ntitle: Muffin\n\n---"),
      QStringLiteral("+++\ntitle = \"Muffin\"\n+++"),
      QStringLiteral("+++\ntitle = \"Muffin\"\n\n+++"),
      QStringLiteral("{\n  \"title\": \"Muffin\"\n}")};
  for (const QString& sample : samples) {
    const QString once = serializeMarkdown(sample);
    const QString twice = serializeMarkdown(once);
    require(once == sample, QStringLiteral("Front matter first serialization changed source"));
    require(twice == once, QStringLiteral("Front matter repeated serialization added blank lines"));
  }
}

void testTaskListMetadata() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "- [x] done\n"
      "- [ ] pending\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for task list sample"));
  require(parsed.root->children().size() == 1, QStringLiteral("Unexpected task list block count"));

  const MarkdownNode& list = childAt(*parsed.root, 0);
  require(list.type() == BlockType::List, QStringLiteral("Expected task list to parse as list"));
  require(list.children().size() == 2, QStringLiteral("Unexpected task item count"));
  require(childAt(list, 0).taskChecked(), QStringLiteral("Checked task metadata missing"));
  require(!childAt(list, 1).taskChecked(), QStringLiteral("Unchecked task metadata should be false"));
}

void testNodeIndexPreservesDocumentOrder() {
  auto root = std::make_unique<MarkdownNode>(BlockType::Document);
  MarkdownNode& heading = root->appendChild(std::make_unique<MarkdownNode>(BlockType::Heading, NodeId::fromString(QStringLiteral("heading"))));
  MarkdownNode& list = root->appendChild(std::make_unique<MarkdownNode>(BlockType::List, NodeId::fromString(QStringLiteral("list"))));
  MarkdownNode& item = list.appendChild(std::make_unique<MarkdownNode>(BlockType::ListItem, NodeId::fromString(QStringLiteral("item"))));
  MarkdownNode& paragraph = root->appendChild(std::make_unique<MarkdownNode>(BlockType::Paragraph, NodeId::fromString(QStringLiteral("paragraph"))));

  MarkdownDocument document;
  document.setMarkdownText(QStringLiteral("# Heading\n\n- Item\n\nParagraph"), std::move(root));

  require(document.index().firstBlock() == &heading, QStringLiteral("NodeIndex firstBlock should return first document-order block"));
  require(document.index().lastBlock() == &paragraph, QStringLiteral("NodeIndex lastBlock should return last document-order block"));
  require(document.index().find(list.id()) == &list, QStringLiteral("NodeIndex should still find intermediate blocks"));
  require(document.index().find(item.id()) == &item, QStringLiteral("NodeIndex should still find nested blocks"));

  document.index().removeSubtree(paragraph);
  require(document.index().lastBlock() == &item, QStringLiteral("NodeIndex removeSubtree should update ordered blocks"));
  require(!document.index().contains(paragraph.id()), QStringLiteral("NodeIndex removeSubtree should remove lookup entry"));
}

// Walks a subtree looking for the first CodeFence node.
const MarkdownNode* firstCodeFenceIn(const MarkdownNode& root) {
  if (root.type() == BlockType::CodeFence) {
    return &root;
  }
  for (const auto& child : root.children()) {
    if (const MarkdownNode* found = firstCodeFenceIn(*child)) {
      return found;
    }
  }
  return nullptr;
}

// A code block's indented-vs-fenced style must be detected from cmark's authoritative
// `fenced` flag — NOT by inspecting whether the source line happens to start with a fence
// character. The latter breaks for a fenced block nested inside a list/block-quote, where
// the opening fence is itself indented (so column 1 is a space) yet the block is genuinely
// fenced and must round-trip as a fence.
void testIndentedVsFencedCodeDetection() {
  CmarkGfmParser parser;
  ParseOptions options;

  // Top-level indented code block -> indented.
  const QString indented = QStringLiteral("    conan install\n    cmake --build");
  ParseResult indentedParsed = parser.parseDocument(indented, options);
  require(indentedParsed.root != nullptr, QStringLiteral("null root for indented code"));
  const MarkdownNode* indentedCode = firstCodeFenceIn(*indentedParsed.root);
  require(indentedCode != nullptr, QStringLiteral("indented code block not parsed"));
  require(indentedCode->isIndentedCode(), QStringLiteral("top-level indented code must be flagged indented"));

  // Top-level fenced code block -> fenced.
  const QString fenced = QStringLiteral("```python\nprint(1)\n```");
  ParseResult fencedParsed = parser.parseDocument(fenced, options);
  require(fencedParsed.root != nullptr, QStringLiteral("null root for fenced code"));
  const MarkdownNode* fencedCode = firstCodeFenceIn(*fencedParsed.root);
  require(fencedCode != nullptr, QStringLiteral("fenced code block not parsed"));
  require(!fencedCode->isIndentedCode(), QStringLiteral("top-level fenced code must NOT be flagged indented"));

  // Fenced code INSIDE a list item -> the fence is indented by the list, but the block is
  // still fenced. Source-line inspection would misread it as indented code.
  const QString listFenced = QStringLiteral("- item:\n  ```python\n  print(1)\n  ```");
  ParseResult listParsed = parser.parseDocument(listFenced, options);
  require(listParsed.root != nullptr, QStringLiteral("null root for list+code sample"));
  const MarkdownNode* listCode = firstCodeFenceIn(*listParsed.root);
  require(listCode != nullptr, QStringLiteral("fenced code inside list not parsed"));
  require(!listCode->isIndentedCode(),
          QStringLiteral("fenced code inside a list must stay fenced, not be rewritten as indented"));

  // Fenced code INSIDE a block quote -> same reasoning.
  const QString quoteFenced = QStringLiteral("> ```python\n> print(1)\n> ```");
  ParseResult quoteParsed = parser.parseDocument(quoteFenced, options);
  require(quoteParsed.root != nullptr, QStringLiteral("null root for quote+code sample"));
  const MarkdownNode* quoteCode = firstCodeFenceIn(*quoteParsed.root);
  require(quoteCode != nullptr, QStringLiteral("fenced code inside block quote not parsed"));
  require(!quoteCode->isIndentedCode(),
          QStringLiteral("fenced code inside a block quote must stay fenced"));

  // Round-trip stability: a list-with-fenced-code must serialize back to a fence (not 4-space
  // indented text), otherwise the list structure collapses.
  MarkdownDocument listDoc;
  listDoc.setMarkdownText(listFenced, std::move(listParsed.root));
  const QString listSerialized = MarkdownSerializer().serializeDocument(listDoc);
  require(listSerialized.contains(QStringLiteral("```python")),
          QStringLiteral("list+code must serialize with a fence, got: '%1'").arg(listSerialized));
}

// Documents the CommonMark constraint that motivates the phantom-line behavior: an indented
// code block cannot carry a trailing empty line — cmark strips every trailing whitespace-only
// line — whereas a fenced block keeps it. This is why Enter-at-end of an indented block is held
// as a phantom line in the node rather than committed to the source.
void testIndentedCodeCannotHoldTrailingEmptyLine() {
  CmarkGfmParser parser;
  ParseOptions options;

  auto literalOf = [&](const QString& md) -> QString {
    ParseResult parsed = parser.parseDocument(md, options);
    const MarkdownNode* code = firstCodeFenceIn(*parsed.root);
    return code ? code->literal() : QStringLiteral("<none>");
  };

  require(literalOf(QStringLiteral("    code")) == QStringLiteral("code"), "indented baseline literal");
  require(literalOf(QStringLiteral("    code\n")) == QStringLiteral("code"), "trailing newline must be stripped");
  require(literalOf(QStringLiteral("    code\n    ")) == QStringLiteral("code"), "trailing 4-space line must be stripped");
  require(literalOf(QStringLiteral("    code\n    \n")) == QStringLiteral("code"), "trailing blank+indent must be stripped");

  // Fenced code keeps a trailing blank line because the closing fence is a sentinel.
  require(literalOf(QStringLiteral("```\ncode\n```")) == QStringLiteral("code"), "fenced baseline literal");
  require(literalOf(QStringLiteral("```\ncode\n\n```")) == QStringLiteral("code\n"), "fenced trailing blank must be preserved");
}

// B1 regression: a phantom trailing newline held in an indented-code node's literal (after Enter
// at the end of the block) must not leak into serialization — otherwise undo/save/snapshot would
// write a stray blank line. Because cmark never produces a trailing newline on an indented-code
// literal, the serializer drops any it sees. Also covers B2 (empty indented code collapses).
void testIndentedCodeSerializationDropsPhantomTrailingNewline() {
  MarkdownSerializer serializer;

  MarkdownNode node(BlockType::CodeFence);
  node.setIndentedCode(true);

  // A single phantom trailing newline is dropped before re-indenting.
  node.setLiteral(QStringLiteral("line1\nline2\n"));
  require(serializer.serializeBlock(node) == QStringLiteral("    line1\n    line2"),
          QStringLiteral("phantom trailing newline must not leak into serialization: '%1'")
              .arg(serializer.serializeBlock(node)));

  // Multiple trailing newlines are all dropped (cmark strips every trailing blank line).
  node.setLiteral(QStringLiteral("line1\n\n"));
  require(serializer.serializeBlock(node) == QStringLiteral("    line1"),
          QStringLiteral("all trailing newlines must be stripped: '%1'")
              .arg(serializer.serializeBlock(node)));

  // B2: an empty (or newline-only) indented-code literal collapses to nothing — a whitespace-only
  // line is just a blank line in CommonMark, so no dead "    " is emitted.
  node.setLiteral(QString());
  require(serializer.serializeBlock(node) == QString(),
          QStringLiteral("empty indented code must serialize to nothing: '%1'")
              .arg(serializer.serializeBlock(node)));
  node.setLiteral(QStringLiteral("\n"));
  require(serializer.serializeBlock(node) == QString(),
          QStringLiteral("newline-only indented code must serialize to nothing: '%1'")
              .arg(serializer.serializeBlock(node)));

  // Sanity: a fenced block keeps a real trailing blank line (no phantom concept there).
  MarkdownNode fenced(BlockType::CodeFence);
  fenced.setLiteral(QStringLiteral("code\n"));
  require(serializer.serializeBlock(fenced) == QStringLiteral("```\ncode\n\n```"),
          QStringLiteral("fenced trailing blank line should be preserved: '%1'")
              .arg(serializer.serializeBlock(fenced)));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testMathSupport();
  testMathInHeadingsAndTables();
  testMathEdgeCases();
  testLegacyBracketMathBlock();
  testLegacyBracketMathInsideCodeFenceUntouched();
  testLegacyBracketMathInsideDollarMathUntouched();
  testLegacyBracketMathStateMachineEdges();
  testLegacyBracketMathWithLeadingSpaces();
  testLegacyBracketMathInBlockQuoteAndList();
  testFencedCodeBlock();
  testCodeFenceSerializationDoesNotGrowTrailingBlankLines();
  testYamlFrontMatter();
  testTomlFrontMatter();
  testJsonFrontMatter();
  testFrontMatterFalsePositives();
  testCodeFenceEmptySerializationIsIdempotent();
  testFrontMatterSerializationDoesNotGrowTrailingBlankLines();
  testTaskListMetadata();
  testNodeIndexPreservesDocumentOrder();
  testIndentedVsFencedCodeDetection();
  testIndentedCodeCannotHoldTrailingEmptyLine();
  testIndentedCodeSerializationDropsPhantomTrailingNewline();
  return 0;
}
