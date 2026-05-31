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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testBasicParseAndSerialize();
  testMathSupport();
  testMathInHeadingsAndTables();
  testMathEdgeCases();
  testFencedCodeBlock();
  return 0;
}
