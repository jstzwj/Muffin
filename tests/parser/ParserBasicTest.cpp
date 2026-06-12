#include "document/LineStartOffsetCache.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include "ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

void testLoneBulletMarkerIsParagraphNotList() {
  CmarkGfmParser parser;
  ParseOptions options;

  // cmark turns a lone bullet/ordered marker (a trailing newline satisfies its bullet check) into
  // an empty list item the editor cannot edit, because its list-marker validation requires a space
  // after the bullet. The parser must demote such lone markers to paragraphs so that only
  // `* ` / `- ` / `1. ` (marker + space) actually start a list.
  const QStringList loneMarkers = {
      QStringLiteral("*"),
      QStringLiteral("-"),
      QStringLiteral("+"),
      QStringLiteral("1."),
  };
  for (const QString& marker : loneMarkers) {
    const QString markdown = marker + QLatin1Char('\n');
    ParseResult parsed = parser.parseDocument(markdown, options);
    require(parsed.root != nullptr, QStringLiteral("Parser returned null root for lone marker %1").arg(marker));
    require(parsed.root->children().size() == 1, QStringLiteral("Lone marker %1 should produce one block").arg(marker));
    const MarkdownNode& child = childAt(*parsed.root, 0);
    require(child.type() == BlockType::Paragraph,
            QStringLiteral("Lone marker '%1' must be a paragraph, not a list").arg(marker));
    require(!child.inlines().isEmpty() && child.inlines().front().text() == marker,
            QStringLiteral("Demoted paragraph should hold the marker '%1' as plain text").arg(marker));
  }

  // A real list (marker + space + content) must still parse as a list.
  ParseResult realList = parser.parseDocument(QStringLiteral("* foo\n"), options);
  require(realList.root != nullptr, QStringLiteral("Parser returned null root for real list"));
  require(childAt(*realList.root, 0).type() == BlockType::List, QStringLiteral("'* foo' must remain a list"));

  // A marker followed by a space but no content (`* `) is a valid empty list and stays a list.
  ParseResult emptyList = parser.parseDocument(QStringLiteral("* \n"), options);
  require(childAt(*emptyList.root, 0).type() == BlockType::List,
          QStringLiteral("'* ' (marker + space) must remain a list, only a bare marker is demoted"));

  // The demotion is container-aware: a lone marker inside a block quote is also demoted.
  ParseResult quoted = parser.parseDocument(QStringLiteral("> *\n"), options);
  const MarkdownNode& quote = childAt(*quoted.root, 0);
  require(quote.type() == BlockType::BlockQuote, QStringLiteral("'> *' top block should be a block quote"));
  require(!quote.children().empty() && quote.children().front()->type() == BlockType::Paragraph,
          QStringLiteral("Lone marker inside a block quote should be demoted to a paragraph"));
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

  const QString unicode = QString::fromUtf8("a\U0001F600中\nz");
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
  const QString markdown = QString::fromUtf8("a\U0001F600 **中** again\n");

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

void testSetextHeadingParseAndSerialize() {
  CmarkGfmParser parser;
  ParseOptions options;
  const QString markdown = QString::fromUtf8("我是一级标题\n===\n\n我是二级标题\n----\n");

  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root for setext sample"));
  require(parsed.root->children().size() == 2, QStringLiteral("Setext sample should have two heading blocks"));

  const MarkdownNode& h1 = childAt(*parsed.root, 0);
  require(h1.type() == BlockType::Heading, QStringLiteral("First setext block is not a heading"));
  require(h1.headingLevel() == 1, QStringLiteral("=== underline should produce an H1"));
  require(h1.setext(), QStringLiteral("H1 from === should carry the setext flag"));
  require(h1.sourceRange().lineEnd > h1.sourceRange().lineStart,
          QStringLiteral("Setext heading source range should span text + underline lines"));

  const MarkdownNode& h2 = childAt(*parsed.root, 1);
  require(h2.type() == BlockType::Heading, QStringLiteral("Second setext block is not a heading"));
  require(h2.headingLevel() == 2, QStringLiteral("--- underline should produce an H2"));
  require(h2.setext(), QStringLiteral("H2 from --- should carry the setext flag"));

  // Editable content must exclude the underline line.
  const qsizetype h1ContentEnd = headingContentEndOffset(h1, markdown);
  const QString h1Content = markdown.mid(h1.sourceRange().byteStart, h1ContentEnd - h1.sourceRange().byteStart);
  require(h1Content == QString::fromUtf8("我是一级标题"),
          QStringLiteral("Setext heading content must not include the === underline"));
  require(!h1Content.contains(QLatin1Char('=')),
          QStringLiteral("Setext heading leaked the underline into the editable content range"));

  // Serialization should round-trip the setext underline form instead of rewriting to ATX.
  MarkdownDocument doc;
  doc.setMarkdownText(markdown, std::move(parsed.root));
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeDocument(doc);
  require(serialized.contains(QString::fromUtf8("我是一级标题\n===")),
          QStringLiteral("Serializer should preserve the H1 setext underline"));
  require(serialized.contains(QString::fromUtf8("我是二级标题\n----")),
          QStringLiteral("Serializer should preserve the H2 setext underline"));
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

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLineStartOffsetCache();
  testLoneBulletMarkerIsParagraphNotList();
  testInlineSourceRangesUseUtf8Columns();
  testFinalParagraphSourceRangeWithoutTrailingNewline();
  testEmptyDocumentHasEditableParagraph();
  testBasicParseAndSerialize();
  testSetextHeadingParseAndSerialize();
  testTableCellSourceRanges();
  return 0;
}
