#include "document/LineStartOffsetCache.h"
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

int countInlineMath(const QVector<InlineNode>& inlines) {
  int count = 0;
  for (const InlineNode& inlineNode : inlines) {
    if (inlineNode.type() == InlineType::InlineMath) {
      ++count;
    }
    count += countInlineMath(inlineNode.children());
  }
  return count;
}

int countInlineMath(const MarkdownNode& node) {
  int count = countInlineMath(node.inlines());
  for (const auto& child : node.children()) {
    count += countInlineMath(*child);
  }
  return count;
}

QString sourceTextForNode(const QString& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  require(range.byteStart >= 0, QStringLiteral("node source range start is invalid"));
  require(range.byteEnd >= range.byteStart, QStringLiteral("node source range end is invalid"));
  require(range.byteEnd <= markdown.size(), QStringLiteral("node source range exceeds markdown size"));
  return markdown.mid(range.byteStart, range.byteEnd - range.byteStart);
}

bool containsInlineMathText(const QVector<InlineNode>& inlines, const QString& text) {
  for (const InlineNode& inlineNode : inlines) {
    if (inlineNode.type() == InlineType::InlineMath && inlineNode.text() == text) {
      return true;
    }
    if (containsInlineMathText(inlineNode.children(), text)) {
      return true;
    }
  }
  return false;
}

bool containsInlineMathText(const MarkdownNode& node, const QString& text) {
  if (containsInlineMathText(node.inlines(), text)) {
    return true;
  }
  for (const auto& child : node.children()) {
    if (containsInlineMathText(*child, text)) {
      return true;
    }
  }
  return false;
}

int countMathBlocks(const MarkdownNode& node) {
  int count = node.type() == BlockType::MathBlock ? 1 : 0;
  for (const auto& child : node.children()) {
    count += countMathBlocks(*child);
  }
  return count;
}

void testLineStartOffsetCache() {
  LineStartOffsetCache empty{QStringView(QString())};
  require(empty.lineCount() == 1, QStringLiteral("Empty text should have one line"));
  require(empty.offsetForLineColumn(1, 1) == 0, QStringLiteral("Empty text line start mismatch"));
  require(empty.lineEndOffset(1) == 0, QStringLiteral("Empty text line end mismatch"));
  require(empty.lineForOffset(0) == 1, QStringLiteral("Empty text offset line mismatch"));

  const QString single = QStringLiteral("abc");
  LineStartOffsetCache singleLine{QStringView(single)};
  require(singleLine.lineCount() == 1, QStringLiteral("Single line count mismatch"));
  require(singleLine.offsetForLineColumn(1, 2) == 1, QStringLiteral("Single line column offset mismatch"));
  require(singleLine.offsetForLineColumn(1, 99) == single.size(), QStringLiteral("Single line column should clamp to line end"));
  require(singleLine.lineEndOffset(1) == single.size(), QStringLiteral("Single line end mismatch"));

  const QString multiple = QStringLiteral("ab\ncd\n");
  LineStartOffsetCache multipleLines{QStringView(multiple)};
  require(multipleLines.lineCount() == 3, QStringLiteral("Trailing newline should create final empty line"));
  require(multipleLines.offsetForLineColumn(2, 1) == 3, QStringLiteral("Second line start mismatch"));
  require(multipleLines.lineEndOffset(2) == 5, QStringLiteral("Second line end mismatch"));
  require(multipleLines.offsetForLineColumn(3, 1) == multiple.size(), QStringLiteral("Final empty line start mismatch"));
  require(multipleLines.lineForOffset(4) == 2, QStringLiteral("Offset to line lookup mismatch"));

  const QString unicode = QString::fromUtf8("a😀中\nz");
  LineStartOffsetCache unicodeLines{QStringView(unicode)};
  require(unicodeLines.offsetForLineColumn(1, 2) == 1, QStringLiteral("Unicode BMP prefix offset mismatch"));
  require(unicodeLines.offsetForLineColumn(1, 3) == 2, QStringLiteral("Emoji UTF-16 offset mismatch"));
  require(unicodeLines.lineEndOffset(1) == unicode.indexOf(QLatin1Char('\n')), QStringLiteral("Unicode line end mismatch"));
  require(unicodeLines.offsetForLineColumn(2, 1) == unicode.indexOf(QLatin1Char('\n')) + 1, QStringLiteral("Unicode second line start mismatch"));
}

void testFinalParagraphSourceRangeWithoutTrailingNewline() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("# Title\n\nLast paragraph");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for final paragraph source range sample"));
  require(parsed.root->children().size() == 2, QStringLiteral("Unexpected final paragraph block count"));

  const MarkdownNode& paragraph = childAt(*parsed.root, 1);
  require(paragraph.type() == BlockType::Paragraph, QStringLiteral("Expected final paragraph"));
  require(paragraph.sourceRange().byteEnd == markdown.size(), QStringLiteral("Final paragraph source range should end at document size"));
  require(sourceTextForNode(markdown, paragraph) == QStringLiteral("Last paragraph"), QStringLiteral("Final paragraph source text mismatch"));
}

void testEmptyDocumentHasEditableParagraph() {
  CmarkGfmParser parser;
  ParseOptions options;

  ParseResult parsed = parser.parseDocument(QString(), options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for empty document"));
  require(parsed.root->type() == BlockType::Document, QStringLiteral("Empty document root is not a document"));
  require(parsed.root->children().size() == 1, QStringLiteral("Empty document should contain one editable paragraph"));

  const MarkdownNode& paragraph = childAt(*parsed.root, 0);
  require(paragraph.type() == BlockType::Paragraph, QStringLiteral("Empty document child is not a paragraph"));
  require(paragraph.sourceRange().byteStart == 0, QStringLiteral("Empty paragraph source start mismatch"));
  require(paragraph.sourceRange().byteEnd == 0, QStringLiteral("Empty paragraph source end mismatch"));
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

void testTableCellSourceRanges() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "| Name | Value | Note |\n"
      "| --- | --- | --- |\n"
      "| C++20 | a\\|b | [x](u) |\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for table source range sample"));
  require(parsed.root->children().size() == 1, QStringLiteral("Unexpected table source range block count"));

  const MarkdownNode& table = childAt(*parsed.root, 0);
  require(table.type() == BlockType::Table, QStringLiteral("Expected table block for source range sample"));
  require(table.children().size() == 2, QStringLiteral("Unexpected table row count for source range sample"));

  const MarkdownNode& headerRow = childAt(table, 0);
  const MarkdownNode& bodyRow = childAt(table, 1);
  require(sourceTextForNode(markdown, childAt(headerRow, 0)) == QStringLiteral("Name"), QStringLiteral("Header cell source range includes table syntax"));
  require(sourceTextForNode(markdown, childAt(headerRow, 1)) == QStringLiteral("Value"), QStringLiteral("Second header cell source range mismatch"));
  require(sourceTextForNode(markdown, childAt(bodyRow, 0)) == QStringLiteral("C++20"), QStringLiteral("Body cell source range mismatch"));
  require(sourceTextForNode(markdown, childAt(bodyRow, 1)) == QStringLiteral("a\\|b"), QStringLiteral("Escaped pipe cell source range mismatch"));
  require(sourceTextForNode(markdown, childAt(bodyRow, 2)) == QStringLiteral("[x](u)"), QStringLiteral("Inline markdown cell source range mismatch"));
}

void testTableCellSourceRangesWithoutOuterPipes() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "Name | Value\n"
      "--- | ---\n"
      "left | right\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for bare table source range sample"));
  require(parsed.root->children().size() == 1, QStringLiteral("Unexpected bare table block count"));

  const MarkdownNode& table = childAt(*parsed.root, 0);
  require(table.type() == BlockType::Table, QStringLiteral("Expected bare table block"));
  const MarkdownNode& headerRow = childAt(table, 0);
  const MarkdownNode& bodyRow = childAt(table, 1);
  require(sourceTextForNode(markdown, childAt(headerRow, 0)) == QStringLiteral("Name"), QStringLiteral("Bare first header source range mismatch"));
  require(sourceTextForNode(markdown, childAt(headerRow, 1)) == QStringLiteral("Value"), QStringLiteral("Bare second header source range mismatch"));
  require(sourceTextForNode(markdown, childAt(bodyRow, 0)) == QStringLiteral("left"), QStringLiteral("Bare first body source range mismatch"));
  require(sourceTextForNode(markdown, childAt(bodyRow, 1)) == QStringLiteral("right"), QStringLiteral("Bare second body source range mismatch"));
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLineStartOffsetCache();
  testFinalParagraphSourceRangeWithoutTrailingNewline();
  testEmptyDocumentHasEditableParagraph();
  testBasicParseAndSerialize();
  testTableCellSourceRanges();
  testTableCellSourceRangesWithoutOuterPipes();
  testMathSupport();
  testMathInHeadingsAndTables();
  testMathEdgeCases();
  testFencedCodeBlock();
  testCodeFenceSerializationDoesNotGrowTrailingBlankLines();
  testYamlFrontMatter();
  testTomlFrontMatter();
  testJsonFrontMatter();
  testFrontMatterFalsePositives();
  testFrontMatterSerializationDoesNotGrowTrailingBlankLines();
  testTaskListMetadata();
  return 0;
}
