#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include "ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

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

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
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
  return 0;
}
