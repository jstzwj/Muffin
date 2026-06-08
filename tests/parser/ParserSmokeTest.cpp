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

const MarkdownNode& definitionByLabel(const MarkdownNode& root, BlockType type, const QString& label) {
  for (const auto& child : root.children()) {
    if (child->type() == type && child->definition().label == label) {
      return *child;
    }
  }
  fail(QStringLiteral("definition block not found: type=%1 label=%2")
           .arg(static_cast<int>(type))
           .arg(label));
  return root;
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
  require(unicodeLines.offsetForLineByteColumn(1, 2) == 1, QStringLiteral("Unicode byte column ASCII offset mismatch"));
  require(unicodeLines.offsetForLineByteColumn(1, 3) == 1, QStringLiteral("Unicode byte column inside emoji should snap to emoji start"));
  require(unicodeLines.offsetForLineByteColumn(1, 6) == 3, QStringLiteral("Unicode byte column CJK start mismatch"));
  require(unicodeLines.offsetForLineByteColumn(1, 9) == unicode.indexOf(QLatin1Char('\n')), QStringLiteral("Unicode byte column line end mismatch"));
}

void testInlineSourceRangesUseUtf8Columns() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QString::fromUtf8("a😀 **中** again\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for inline source range sample"));
  require(parsed.root->children().size() == 1, QStringLiteral("Unexpected inline source range block count"));

  const MarkdownNode& paragraph = childAt(*parsed.root, 0);
  require(paragraph.type() == BlockType::Paragraph, QStringLiteral("Expected paragraph for inline source range sample"));
  require(paragraph.inlines().size() >= 2, QStringLiteral("Expected styled inline source range sample"));
  const InlineNode& strong = paragraph.inlines().at(1);
  require(strong.type() == InlineType::Strong, QStringLiteral("Expected strong inline in source range sample"));

  const qsizetype expectedStart = markdown.indexOf(QStringLiteral("**"));
  const qsizetype expectedEnd = expectedStart + QStringLiteral("**中**").size();
  require(strong.sourceStart() == expectedStart, QStringLiteral("Strong inline source start should use QString offset"));
  require(strong.sourceEnd() == expectedEnd, QStringLiteral("Strong inline source end should be half-open QString offset"));
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

void testDefinitionBlocks() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral(
      "[1]: 2324222 \"1232\"\n\n"
      "[2]:\n"
      "  2324222\n\n"
      "[^1]: This is the note.\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for definition sample"));
  const MarkdownNode& linkOne = definitionByLabel(*parsed.root, BlockType::LinkDefinition, QStringLiteral("1"));
  require(linkOne.type() == BlockType::LinkDefinition, QStringLiteral("First definition should be link definition"));
  require(linkOne.definition().label == QStringLiteral("1"), QStringLiteral("Link definition label mismatch"));
  require(linkOne.definition().destination == QStringLiteral("2324222"), QStringLiteral("Link definition destination mismatch"));
  require(linkOne.definition().title == QStringLiteral("1232"), QStringLiteral("Link definition title mismatch"));

  const MarkdownNode& linkTwo = definitionByLabel(*parsed.root, BlockType::LinkDefinition, QStringLiteral("2"));
  require(linkTwo.type() == BlockType::LinkDefinition, QStringLiteral("Second definition should be link definition"));
  require(linkTwo.definition().label == QStringLiteral("2"), QStringLiteral("Continuation link definition label mismatch"));
  require(linkTwo.definition().destination == QStringLiteral("2324222"), QStringLiteral("Continuation link destination mismatch"));

  const MarkdownNode& footnote = definitionByLabel(*parsed.root, BlockType::FootnoteDefinition, QStringLiteral("1"));
  require(footnote.type() == BlockType::FootnoteDefinition, QStringLiteral("Third definition should be footnote definition"));
  require(footnote.definition().label == QStringLiteral("1"), QStringLiteral("Footnote label mismatch"));
  require(footnote.definition().note == QStringLiteral("This is the note."), QStringLiteral("Footnote text mismatch"));

  ParseResult emptyTemplate = parser.parseDocument(QStringLiteral("[]:  \"\""), options);
  require(emptyTemplate.root != nullptr, QStringLiteral("Parser returned null root for empty definition template"));
  require(emptyTemplate.root->children().size() == 1, QStringLiteral("Empty link definition template should render as one block"));
  const MarkdownNode& emptyLink = childAt(*emptyTemplate.root, 0);
  require(emptyLink.type() == BlockType::LinkDefinition, QStringLiteral("Empty link definition template should not render as paragraph"));
  require(emptyLink.definition().label.isEmpty(), QStringLiteral("Empty template label should stay empty"));
  require(emptyLink.definition().destination.isEmpty(), QStringLiteral("Empty template destination should stay empty"));
  require(emptyLink.definition().title.isEmpty(), QStringLiteral("Empty template title should stay empty"));
  require(emptyLink.sourceRange().byteEnd == QStringLiteral("[]:  \"\"").size(),
          QStringLiteral("Empty template source range should include trailing quotes"));
}

void testInvalidDefinitionLikeParagraphsStayParagraphs() {
  CmarkGfmParser parser;
  ParseOptions options;

  const QString markdown = QStringLiteral(
      "[x]: url trailing\n\n"
      "[x]: <unterminated\n\n"
      "[x]:\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for invalid definition sample"));
  require(parsed.root->children().size() == 3, QStringLiteral("Invalid definition-like lines should stay as three blocks"));
  for (const auto& child : parsed.root->children()) {
    require(child->type() == BlockType::Paragraph, QStringLiteral("Invalid definition-like line should remain paragraph"));
  }
}

void testDefinitionSerializationPreservesSourceShape() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[ref]: <https://example.test/a b> ''\n\n[]:  \"\"");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for definition serialization sample"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(document);
  require(serialized.contains(QStringLiteral("[ref]: <https://example.test/a b> ''")),
          QStringLiteral("Serializer should preserve angle destination and empty quoted title"));
  require(serialized.contains(QStringLiteral("[]:  \"\"")),
          QStringLiteral("Serializer should preserve empty template quotes"));
}

void testDefinitionSerializationRebuildsWhenFieldsDivergeFromSourceText() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[ref]: <https://old.example/a b> \"\"");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for stale sourceText sample"));
  MarkdownNode& link = const_cast<MarkdownNode&>(definitionByLabel(*parsed.root, BlockType::LinkDefinition, QStringLiteral("ref")));

  DefinitionBlock definition = link.definition();
  definition.destination = QStringLiteral("https://new.example/path");
  definition.destinationRange = {definition.destinationRange.start, definition.destinationRange.start + definition.destination.size()};
  link.setDefinition(definition);

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(document);
  require(serialized == QStringLiteral("[ref]: <https://new.example/path> \"\""),
          QStringLiteral("Serializer should rebuild edited destination without losing angle or empty title shape: %1").arg(serialized));
}

void testDefinitionSerializationRebuildsEditedTitle() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[ref]: url 'old title'");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for stale title sourceText sample"));
  MarkdownNode& link = const_cast<MarkdownNode&>(definitionByLabel(*parsed.root, BlockType::LinkDefinition, QStringLiteral("ref")));

  DefinitionBlock definition = link.definition();
  definition.title = QStringLiteral("new title");
  definition.titleRange = {definition.titleRange.start, definition.titleRange.start + definition.title.size()};
  link.setDefinition(definition);

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(document);
  require(serialized == QStringLiteral("[ref]: url 'new title'"),
          QStringLiteral("Serializer should rebuild edited title with original quote shape: %1").arg(serialized));
}

void testDefinitionSerializationRebuildsParenthesizedTitleShape() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[ref]: url (old title)");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for parenthesized title sourceText sample"));
  MarkdownNode& link = const_cast<MarkdownNode&>(definitionByLabel(*parsed.root, BlockType::LinkDefinition, QStringLiteral("ref")));

  DefinitionBlock definition = link.definition();
  definition.title = QStringLiteral("new title");
  definition.titleRange = {definition.titleRange.start, definition.titleRange.start + definition.title.size()};
  link.setDefinition(definition);

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(document);
  require(serialized == QStringLiteral("[ref]: url (new title)"),
          QStringLiteral("Serializer should rebuild edited title with parenthesized shape: %1").arg(serialized));
}

void testMultiLineFootnoteKeepsCmarkRange() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[^1]: first\n    second\n\nnext");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for multiline footnote sample"));
  const MarkdownNode& footnote = definitionByLabel(*parsed.root, BlockType::FootnoteDefinition, QStringLiteral("1"));
  require(sourceTextForNode(markdown, footnote) == QStringLiteral("[^1]: first\n    second"),
          QStringLiteral("Multiline footnote source range should include continuation line"));
  require(footnote.definition().note == QStringLiteral("first"),
          QStringLiteral("Footnote editable note field should stay first-line text"));
}

void testDefinitionCommonMarkBoundaryMatrix() {
  CmarkGfmParser parser;
  ParseOptions options;

  struct Case {
    QString markdown;
    BlockType expectedType;
    QString label;
    QString destination;
    QString title;
  };

  const QVector<Case> cases{
      {QStringLiteral("[brackets]: <https://example.test/a(b)>"), BlockType::LinkDefinition,
       QStringLiteral("brackets"), QStringLiteral("https://example.test/a(b)"), QString()},
      {QStringLiteral("[escaped]: https://example.test/a\\(b\\)"), BlockType::LinkDefinition,
       QStringLiteral("escaped"), QStringLiteral("https://example.test/a\\(b\\)"), QString()},
      {QStringLiteral("[empty-title]: url \"\""), BlockType::LinkDefinition,
       QStringLiteral("empty-title"), QStringLiteral("url"), QString()},
      {QStringLiteral("[paren-title]: url (paren title)"), BlockType::LinkDefinition,
       QStringLiteral("paren-title"), QStringLiteral("url"), QStringLiteral("paren title")},
      {QStringLiteral("[continued]:\n  https://example.test/next"), BlockType::LinkDefinition,
       QStringLiteral("continued"), QStringLiteral("https://example.test/next"), QString()},
      {QStringLiteral("    [indented]: url"), BlockType::CodeFence, QString(), QString(), QString()},
      {QStringLiteral("[balanced]: https://example.test/page(1)"), BlockType::LinkDefinition,
       QStringLiteral("balanced"), QStringLiteral("https://example.test/page(1)"), QString()},
      {QStringLiteral("[escaped-gt]: <https://example.test/a\\>b>"), BlockType::LinkDefinition,
       QStringLiteral("escaped-gt"), QStringLiteral("https://example.test/a\\>b"), QString()},
      {QStringLiteral("[trailing]: url trailing"), BlockType::Paragraph, QString(), QString(), QString()},
  };

  for (const Case& item : cases) {
    ParseResult parsed = parser.parseDocument(item.markdown, options);
    require(parsed.root != nullptr, QStringLiteral("Parser returned null root for boundary case: %1").arg(item.markdown));
    require(!parsed.root->children().empty(), QStringLiteral("Boundary case should produce at least one block: %1").arg(item.markdown));
    const MarkdownNode& block = childAt(*parsed.root, 0);
    require(block.type() == item.expectedType,
            QStringLiteral("Boundary case block type mismatch for '%1': expected %2 got %3")
                .arg(item.markdown)
                .arg(static_cast<int>(item.expectedType))
                .arg(static_cast<int>(block.type())));
    if (item.expectedType == BlockType::LinkDefinition) {
      require(block.definition().label == item.label, QStringLiteral("Boundary case label mismatch for %1").arg(item.markdown));
      require(block.definition().destination == item.destination,
              QStringLiteral("Boundary case destination mismatch for %1").arg(item.markdown));
      require(block.definition().title == item.title, QStringLiteral("Boundary case title mismatch for %1").arg(item.markdown));
      require(block.definition().sourceText == item.markdown,
              QStringLiteral("Boundary case source shape should be preserved for %1").arg(item.markdown));
    }
  }
}

void testMultiLineFootnoteSerializationPreservesContinuation() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QStringLiteral("[^1]: first\n    second\n\nnext");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for multiline footnote serialization test"));
  const MarkdownNode& footnote = definitionByLabel(*parsed.root, BlockType::FootnoteDefinition, QStringLiteral("1"));
  require(footnote.type() == BlockType::FootnoteDefinition, QStringLiteral("Expected footnote definition"));
  require(!footnote.definition().sourceText.isEmpty(),
          QStringLiteral("Footnote sourceText should be preserved for serialization"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(document);
  require(serialized.contains(QStringLiteral("second")),
          QStringLiteral("Serialized footnote should preserve continuation lines: %1").arg(serialized));
}

void testNonVirtualTemplateWithTitleButNoDestination() {
  // Verify at scanner level: a definition with a title but no destination is not a virtual template.
  // The full parser (cmark) treats this as a paragraph since it lacks a destination, but the
  // scanner's virtualTemplate flag should still be false.
  const QString markdown = QStringLiteral("[foo]: \"bar\"");
  const QVector<DefinitionParseResult> defs = scanDefinitionBlocks(markdown);
  if (!defs.isEmpty()) {
    const DefinitionBlock& def = defs.first().definition;
    require(!def.virtualTemplate,
            QStringLiteral("[foo]: \"bar\" with non-empty title should not be classified as virtual template"));
  }
}

void testMixedIndentContinuationLine() {
  const QString markdown = QStringLiteral("[^1]: note\n \tcontinuation\n\nnext");
  const QVector<DefinitionParseResult> defs = scanDefinitionBlocks(markdown);
  require(defs.size() == 1, QStringLiteral("Expected one definition for mixed-indent test"));
  const DefinitionBlock& def = defs.first().definition;
  require(def.kind == DefinitionBlock::Kind::Footnote, QStringLiteral("Expected footnote"));
  require(def.sourceRange.length() > static_cast<qsizetype>(QStringLiteral("[^1]: note").size()),
          QStringLiteral("Mixed tab+space continuation should extend source range"));
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

void testCodeFenceEmptySerializationIsIdempotent() {
  // Verify code fence literal does not include cmark's trailing newline
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

  // Verify serialization round-trips for various code fence shapes
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLineStartOffsetCache();
  testInlineSourceRangesUseUtf8Columns();
  testFinalParagraphSourceRangeWithoutTrailingNewline();
  testEmptyDocumentHasEditableParagraph();
  testBasicParseAndSerialize();
  testTableCellSourceRanges();
  testDefinitionBlocks();
  testInvalidDefinitionLikeParagraphsStayParagraphs();
  testDefinitionSerializationPreservesSourceShape();
  testDefinitionSerializationRebuildsWhenFieldsDivergeFromSourceText();
  testDefinitionSerializationRebuildsEditedTitle();
  testDefinitionSerializationRebuildsParenthesizedTitleShape();
  testMultiLineFootnoteKeepsCmarkRange();
  testDefinitionCommonMarkBoundaryMatrix();
  testMultiLineFootnoteSerializationPreservesContinuation();
  testNonVirtualTemplateWithTitleButNoDestination();
  testMixedIndentContinuationLine();
  testTableCellSourceRangesWithoutOuterPipes();
  testMathSupport();
  testMathInHeadingsAndTables();
  testMathEdgeCases();
  testFencedCodeBlock();
  testCodeFenceSerializationDoesNotGrowTrailingBlankLines();
  testCodeFenceEmptySerializationIsIdempotent();
  testYamlFrontMatter();
  testTomlFrontMatter();
  testJsonFrontMatter();
  testFrontMatterFalsePositives();
  testFrontMatterSerializationDoesNotGrowTrailingBlankLines();
  testTaskListMetadata();
  testNodeIndexPreservesDocumentOrder();
  return 0;
}
